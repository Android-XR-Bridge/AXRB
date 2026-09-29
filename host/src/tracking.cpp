#include "openxr_session.h"
#include <cstdlib>

// Pose and controller heartbeats: every 900 samples (10 s at 90 Hz) proves
// tracking is live without drowning the log; once a second was 7 lines/s.
constexpr uint64_t kPoseLogInterval = 900;

namespace axrb::host::detail {

namespace {

axrb::protocol::ViewFov to_protocol_fov(const XrFovf& fov)
{
    return {fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown};
}

axrb::protocol::SpaceVelocity to_protocol_velocity(const XrSpaceVelocity& v, XrSpaceLocationFlags poseFlags)
{
    axrb::protocol::SpaceVelocity out{};
    if ((poseFlags & 3) != 3) return out;
    auto finite = [](const XrVector3f& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    if ((v.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) && finite(v.linearVelocity)) {
        out.flags |= 1; out.linear = {v.linearVelocity.x, v.linearVelocity.y, v.linearVelocity.z};
    }
    if ((v.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) && finite(v.angularVelocity)) {
        out.flags |= 2; out.angular = {v.angularVelocity.x, v.angularVelocity.y, v.angularVelocity.z};
    }
    return out;
}

} // namespace

axrb::protocol::PoseFrame OpenXrSession::make_frame(uint64_t sequence)
{
    pump_events();

    axrb::protocol::PoseFrame frame = latest_frame(sequence);
    frame.hmd_flags = frame.local_origin_flags = 0;
    frame.hmd_velocity = frame.local_origin_velocity = {};
    frame.controllers[0] = {};
    frame.controllers[1] = {};
    for (size_t hand = 0; hand < 2; ++hand) {
        frame.grip_flags[hand] = frame.aim_flags[hand] = 0;
        frame.aim_active[hand] = 0;
        frame.grip_velocity[hand] = frame.aim_velocity[hand] = {};
    }
    frame.sequence = sequence;
    frame.monotonic_time_ns = monotonic_time_ns();
#if defined(_WIN32)
    frame.render_width = projectionWidth_;
    frame.render_height = projectionHeight_;
#endif

    if (!sessionRunning_) {
        publish_pose(frame);
        return frame;
    }

    XrTime locateTime = current_xr_time();
    XrTime frameDisplayTime = locateTime;
    [[maybe_unused]] XrDuration frameDisplayPeriod = 0;
    bool beganFrame = false, shouldRender = false;

    if (useFrameLoop_) {
        XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        XrResult result;
        {
            static axrb::protocol::PerfStats stats("host-wait-frame");
            axrb::protocol::PerfScope scope(stats);
            result = waitFrame_(session_, &waitInfo, &frameState);
        }
        if (result == XR_SUCCESS) {
            shouldRender = frameState.shouldRender;
            frameDisplayTime = frameState.predictedDisplayTime;
            locateTime = frameState.predictedDisplayTime;
            if (frameState.shouldRender && axrb::protocol::valid_display_period(frameState.predictedDisplayPeriod)) {
                frame.display_period_ns = static_cast<uint32_t>(frameState.predictedDisplayPeriod);
                frameDisplayPeriod = frameState.predictedDisplayPeriod;
            }

            XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
            {
                static axrb::protocol::PerfStats stats("host-begin-frame");
                axrb::protocol::PerfScope scope(stats);
                result = beginFrame_(session_, &beginInfo);
            }
            if (result == XR_SUCCESS) {
                beganFrame = true;
            } else if (result == XR_ERROR_CALL_ORDER_INVALID) {
                std::fprintf(
                    stderr,
                    "AXRB OpenXR: xrBeginFrame was rejected in headless mode; continuing with pose-only locate\n");
                useFrameLoop_ = false;
            } else {
                std::fprintf(stderr, "AXRB OpenXR: xrBeginFrame failed: %s (%d)\n", xr_result_name(result), result);
                publish_pose(frame);
                return frame;
            }
        } else if (result == XR_FRAME_DISCARDED) {
            publish_pose(frame);
            return frame;
        } else {
            std::fprintf(stderr, "AXRB OpenXR: xrWaitFrame failed: %s (%d)\n", xr_result_name(result), result);
            useFrameLoop_ = false;
        }
    }

    if (locateTime == 0) {
        locateTime = current_xr_time();
    }
    if (locateTime == 0) {
        publish_pose(frame);
        return frame;
    }

    const bool hadValidViewFovs = axrb::protocol::has_valid_view_fovs(frame);
    if (!hadValidViewFovs) {
        frame.view_fov[0] = {};
        frame.view_fov[1] = {};
        frame.view_fov_valid = 0;
    }
    XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocateInfo.displayTime = locateTime;
    viewLocateInfo.space = localSpace_;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    std::array<XrView, 2> opticalViews{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    uint32_t opticalViewCount = 0;
    if (locateViews_(session_, &viewLocateInfo, &viewState,
            static_cast<uint32_t>(opticalViews.size()), &opticalViewCount, opticalViews.data()) == XR_SUCCESS &&
        opticalViewCount == static_cast<uint32_t>(opticalViews.size())) {
        const auto left = to_protocol_fov(opticalViews[0].fov);
        const auto right = to_protocol_fov(opticalViews[1].fov);
        if (axrb::protocol::valid_view_fov(left) && axrb::protocol::valid_view_fov(right)) {
            frame.view_fov[0] = left;
            frame.view_fov[1] = right;
            frame.view_fov_valid = 1;
            if (!hadValidViewFovs) {
                std::fprintf(stderr,
                    "AXRB OpenXR: optical FOV L=(%.4f %.4f %.4f %.4f) R=(%.4f %.4f %.4f %.4f)\n",
                    left.angle_left, left.angle_right, left.angle_up, left.angle_down,
                    right.angle_left, right.angle_right, right.angle_up, right.angle_down);
            }
        }
    }

    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    XrSpaceLocation origin{XR_TYPE_SPACE_LOCATION};
    XrSpaceVelocity headVelocity{XR_TYPE_SPACE_VELOCITY};
    XrSpaceVelocity originVelocity{XR_TYPE_SPACE_VELOCITY};
    location.next = &headVelocity;
    origin.next = &originVelocity;
    // Some runtimes report a tracked identity pose while still entering READY.
    // Do not turn that provisional sample into a permanent eye-level origin.
    if (!localOriginInitialized_ && (sessionVisible_ || !useFrameLoop_)) {
        XrSpaceLocation initialHead{XR_TYPE_SPACE_LOCATION};
        constexpr XrSpaceLocationFlags kTrackedPoseFlags =
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
            XR_SPACE_LOCATION_POSITION_VALID_BIT |
            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
            XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        if (locateSpace_(viewSpace_, localSpace_, locateTime, &initialHead) == XR_SUCCESS &&
            (initialHead.locationFlags & kTrackedPoseFlags) == kTrackedPoseFlags) {
            XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
            info.referenceSpaceType = trackingSpaceType_;
            // Correct eye height without recentering heading or horizontal
            // position from a headset that may still be resting on a desk.
            // Use the common tracking axes: native LOCAL may be rotated
            // relative to STAGE by the runtime's seated calibration.
            info.poseInReferenceSpace.orientation.w = 1.0f;
            info.poseInReferenceSpace.position.y = initialHead.pose.position.y;
            XrSpace startupLocal = XR_NULL_HANDLE;
            if (createReferenceSpace_(session_, &info, &startupLocal) == XR_SUCCESS) {
                destroySpace_(appLocalSpace_);
                appLocalSpace_ = startupLocal;
                localOriginInitialized_ = true;
            }
        }
    }
    if (localOriginInitialized_ && locateSpace_(appLocalSpace_, localSpace_, locateTime, &origin) == XR_SUCCESS &&
        (origin.locationFlags & 3) == 3) {
        frame.local_origin = to_protocol_pose(origin.pose);
        frame.local_origin_flags = static_cast<uint32_t>(origin.locationFlags);
        frame.local_origin_velocity = to_protocol_velocity(originVelocity, origin.locationFlags);
        if (!reportedLocalOrigin_) {
            std::fprintf(stderr, "AXRB OpenXR: LOCAL origin in tracking world=(%.3f %.3f %.3f) "
                "q=(%.4f %.4f %.4f %.4f) flags=0x%llx\n",
                origin.pose.position.x, origin.pose.position.y, origin.pose.position.z,
                origin.pose.orientation.x, origin.pose.orientation.y, origin.pose.orientation.z, origin.pose.orientation.w,
                static_cast<unsigned long long>(origin.locationFlags));
            reportedLocalOrigin_ = true;
        }
    }
    XrResult result = locateSpace_(viewSpace_, localSpace_, locateTime, &location);
    if (result == XR_SUCCESS &&
        (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
        (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
        frame.hmd = to_protocol_pose(location.pose);
        frame.hmd_flags = static_cast<uint32_t>(location.locationFlags);
        frame.hmd_velocity = to_protocol_velocity(headVelocity, location.locationFlags);
        if (sequence % kPoseLogInterval == 0) {
            std::fprintf(
                stderr,
                "AXRB OpenXR: pose seq=%llu hmd=(%.3f %.3f %.3f)\n",
                static_cast<unsigned long long>(sequence),
                frame.hmd.x,
                frame.hmd.y,
                frame.hmd.z);
        }
    }

    // Test-only: benchmarks without a worn headset otherwise see invalid head
    // tracking, and many titles then stop drawing their scene. A fixed standing
    // pose keeps the normal rendering workload. Never used unless requested.
    static const bool syntheticHmd = [] {
        const char* value = std::getenv("AXRB_TEST_SYNTHETIC_HMD");
        return value && value[0] == '1';
    }();
    if (syntheticHmd && (frame.hmd_flags & 3) != 3) {
        frame.hmd = {};
        frame.hmd.y = 1.6f;
        frame.hmd.qw = 1.0f;
        frame.hmd_flags = 0xF;
        frame.hmd_velocity = {3, {}, {}};
        if ((frame.local_origin_flags & 3) != 3) {
            frame.local_origin = {};
            frame.local_origin.qw = 1.0f;
            frame.local_origin_flags = 0xF;
        }
    }

    locate_controller_spaces(frame, locateTime, sequence);
    locate_hand_joints(frame, locateTime, sequence);
    publish_pose(frame);

    if (beganFrame) {
        XrCompositionLayerQuad fpsHud{XR_TYPE_COMPOSITION_LAYER_QUAD};
#if defined(_WIN32)
        auto& layers = nativeSubmission_.layers;
        layers.clear();
        nativeSubmission_.hasGameProjection = false;
        if (projectionSwapchain_ != XR_NULL_HANDLE &&
            !update_projection_layers(locateTime, frameDisplayPeriod, nativeSubmission_))
            layers.clear();
        if (shouldRender && update_fps_hud(fpsHud, static_cast<uint32_t>(layers.size())))
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&fpsHud));
#else
        const std::vector<const XrCompositionLayerBaseHeader*> layers;
#endif
        const uint32_t layerCount = static_cast<uint32_t>(layers.size());

        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = frameDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = layerCount;
        endInfo.layers = layerCount > 0 ? layers.data() : nullptr;
        {
            static axrb::protocol::PerfStats stats("host-end-frame");
            axrb::protocol::PerfScope scope(stats);
            result = endFrame_(session_, &endInfo);
        }
        if (result != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: xrEndFrame failed: %s (%d)\n", xr_result_name(result), result);
            useFrameLoop_ = false;
        }
#if defined(_WIN32)
#if defined(AXRB_ENABLE_PERFORMANCE_OVERLAY)
        if (imageFrame_) imageFrame_->performance.submitted(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
            result == XR_SUCCESS, layerCount && nativeSubmission_.hasGameProjection,
            nativeSubmission_.gameSequence, nativeSubmission_.gameWidth, nativeSubmission_.gameHeight, frameDisplayPeriod);
#endif
        if (result == XR_SUCCESS && layerCount && nativeSubmission_.hasGameProjection) {
            if (imageFrame_) imageFrame_->submitted(nativeSubmission_.gameSequence);
            if (submittedGameFrames_.record(nativeSubmission_.gameSequence)) {
                static axrb::protocol::FrameIntervals freshStats("host-fresh-submit");
                freshStats.record();
            }
            if (submittedGameFrames_.total % 300 == 0) {
                const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                std::fprintf(stderr, "AXRB Submitted: unique=%llu repeats=%llu total=%llu regressed=%llu steady_ns=%lld sequence=%llu width=%u height=%u\n",
                    static_cast<unsigned long long>(submittedGameFrames_.unique),
                    static_cast<unsigned long long>(submittedGameFrames_.repeated),
                    static_cast<unsigned long long>(submittedGameFrames_.total),
                    static_cast<unsigned long long>(submittedGameFrames_.regressed),
                    static_cast<long long>(ns),
                    static_cast<unsigned long long>(nativeSubmission_.gameSequence),
                    nativeSubmission_.gameWidth, nativeSubmission_.gameHeight);
            }
        }
#endif
    }

    return frame;
}

axrb::protocol::PoseFrame OpenXrSession::latest_frame(uint64_t sequence)
{
    std::lock_guard<std::mutex> lock(frameMutex_);
    axrb::protocol::PoseFrame frame = latest_;
    frame.sequence = sequence;
    frame.monotonic_time_ns = monotonic_time_ns();
    return frame;
}

void OpenXrSession::publish_pose(const axrb::protocol::PoseFrame& frame)
{
    std::lock_guard<std::mutex> lock(frameMutex_);
    latest_ = frame;
}

bool OpenXrSession::create_reference_space(XrReferenceSpaceType type, XrSpace* space)
{
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = type;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    const XrResult result = createReferenceSpace_(session_, &spaceInfo, space);
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrCreateReferenceSpace failed: %s (%d)\n", xr_result_name(result), result);
        return false;
    }
    return true;
}

bool OpenXrSession::string_to_path(const char* text, XrPath* path)
{
    const XrResult result = stringToPath_(instance_, text, path);
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrStringToPath(%s) failed: %s (%d)\n", text, xr_result_name(result), result);
        return false;
    }
    return true;
}

void OpenXrSession::initialize_hand_tracking()
{
    if (!handTrackingEnabled_ || !load_func("xrCreateHandTrackerEXT", &createHandTracker_) ||
        !load_func("xrDestroyHandTrackerEXT", &destroyHandTracker_) ||
        !load_func("xrLocateHandJointsEXT", &locateHandJoints_)) return;
    for (size_t hand = 0; hand < 2; ++hand) {
        XrHandTrackingDataSourceEXT sources[] = {XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT, XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT};
        XrHandTrackingDataSourceInfoEXT sourceInfo{XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT};
        sourceInfo.requestedDataSourceCount = 2; sourceInfo.requestedDataSources = sources;
        XrHandTrackerCreateInfoEXT info{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        info.hand = hand == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
        info.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        if (handDataSourceEnabled_) info.next = &sourceInfo;
        auto result = createHandTracker_(session_, &info, &handTrackers_[hand]);
        std::fprintf(stderr, "AXRB Hands: create hand=%zu result=%d\n", hand, result);
    }
}

void OpenXrSession::locate_hand_joints(axrb::protocol::PoseFrame& frame, XrTime time, uint64_t sequence)
{
    frame.hands[0] = {}; frame.hands[1] = {};
    frame.hand_tracking_supported = handTrackers_[0] != XR_NULL_HANDLE && handTrackers_[1] != XR_NULL_HANDLE;
    for (size_t hand = 0; hand < 2; ++hand) {
        if (!handTrackers_[hand]) continue;
        XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT]{};
        XrHandTrackingDataSourceStateEXT source{XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
        XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
        locations.jointCount = XR_HAND_JOINT_COUNT_EXT; locations.jointLocations = joints;
        if (handDataSourceEnabled_) locations.next = &source;
        XrHandJointsLocateInfoEXT info{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
        info.baseSpace = localSpace_; info.time = time;
        const auto result = locateHandJoints_(handTrackers_[hand], &info, &locations);
        auto& target = frame.hands[hand];
        if (result == XR_SUCCESS && locations.isActive) {
            target.active = 1;
            target.source = source.isActive ? static_cast<uint32_t>(source.dataSource) : 0;
            for (size_t joint = 0; joint < XR_HAND_JOINT_COUNT_EXT; ++joint) {
                target.joints[joint] = {joints[joint].locationFlags, to_protocol_pose(joints[joint].pose), joints[joint].radius};
            }
        }
        if (sequence % 360 == 0) std::fprintf(stderr, "AXRB Hands: hand=%zu active=%u source=%u result=%d\n", hand, target.active, target.source, result);
    }
}

void OpenXrSession::initialize_controller_actions()
{
    if (!string_to_path("/user/hand/left", &handSubactionPaths_[0]) ||
        !string_to_path("/user/hand/right", &handSubactionPaths_[1])) {
        return;
    }

    XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(actionSetInfo.actionSetName, "axrb_gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(actionSetInfo.localizedActionSetName, "AXRB gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    actionSetInfo.priority = 0;
    XrResult result = createActionSet_(instance_, &actionSetInfo, &actionSet_);
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrCreateActionSet failed: %s (%d)\n", xr_result_name(result), result);
        actionSet_ = XR_NULL_HANDLE;
        return;
    }

    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
    actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
    std::strncpy(actionInfo.actionName, "hand_pose", XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(actionInfo.localizedActionName, "Hand pose", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    actionInfo.countSubactionPaths = static_cast<uint32_t>(handSubactionPaths_.size());
    actionInfo.subactionPaths = handSubactionPaths_.data();
    result = createAction_(actionSet_, &actionInfo, &handPoseAction_);
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrCreateAction(hand_pose) failed: %s (%d)\n", xr_result_name(result), result);
        return;
    }

    std::strncpy(actionInfo.actionName, "aim_pose", XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(actionInfo.localizedActionName, "Aim pose", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    if (createAction_(actionSet_, &actionInfo, &aimPoseAction_) != XR_SUCCESS) return;

    constexpr const char* inputNames[] = {"trigger", "squeeze", "stick", "primary", "secondary", "menu",
        "stick_press", "primary_touch", "secondary_touch", "trigger_touch", "stick_touch", "thumbrest_touch"};
    for (size_t i = 0; i < InputCount; ++i) {
        actionInfo.actionType = i <= Squeeze ? XR_ACTION_TYPE_FLOAT_INPUT :
            i == Stick ? XR_ACTION_TYPE_VECTOR2F_INPUT : XR_ACTION_TYPE_BOOLEAN_INPUT;
        std::snprintf(actionInfo.actionName, sizeof(actionInfo.actionName), "%s", inputNames[i]);
        std::snprintf(actionInfo.localizedActionName, sizeof(actionInfo.localizedActionName), "%s", inputNames[i]);
        if (createAction_(actionSet_, &actionInfo, &inputActions_[i]) != XR_SUCCESS) {
            std::fprintf(stderr, "AXRB OpenXR: cannot create input action %s\n", inputNames[i]);
            return;
        }
    }

    suggest_pose_bindings("/interaction_profiles/oculus/touch_controller", "/input/grip/pose");
    suggest_pose_bindings("/interaction_profiles/valve/index_controller", "/input/grip/pose");
    suggest_pose_bindings("/interaction_profiles/htc/vive_controller", "/input/grip/pose");

    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &actionSet_;
    result = attachSessionActionSets_(session_, &attachInfo);
    if (result != XR_SUCCESS) {
        std::fprintf(stderr, "AXRB OpenXR: xrAttachSessionActionSets failed: %s (%d)\n", xr_result_name(result), result);
        return;
    }

    for (size_t i = 0; i < handSpaces_.size(); ++i) {
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = handPoseAction_;
        spaceInfo.subactionPath = handSubactionPaths_[i];
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        result = createActionSpace_(session_, &spaceInfo, &handSpaces_[i]);
        if (result != XR_SUCCESS) {
            std::fprintf(
                stderr,
                "AXRB OpenXR: xrCreateActionSpace hand %zu failed: %s (%d)\n",
                i,
                xr_result_name(result),
                result);
            handSpaces_[i] = XR_NULL_HANDLE;
        }
    }

    for (size_t i = 0; i < aimSpaces_.size(); ++i) {
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.action = aimPoseAction_; info.subactionPath = handSubactionPaths_[i];
        info.poseInActionSpace.orientation.w = 1;
        if (createActionSpace_(session_, &info, &aimSpaces_[i]) != XR_SUCCESS) return;
    }

    controllerActionsReady_ = handSpaces_[0] != XR_NULL_HANDLE || handSpaces_[1] != XR_NULL_HANDLE;
    if (controllerActionsReady_) {
        std::fprintf(stderr, "AXRB OpenXR: controller pose actions ready\n");
    }
}

void OpenXrSession::suggest_pose_bindings(const char* interactionProfilePath, const char* poseInputSuffix)
{
    if (handPoseAction_ == XR_NULL_HANDLE) {
        return;
    }

    XrPath profilePath = XR_NULL_PATH;
    if (!string_to_path(interactionProfilePath, &profilePath)) {
        return;
    }

    char leftBindingText[128]{};
    char rightBindingText[128]{};
    std::snprintf(leftBindingText, sizeof(leftBindingText), "/user/hand/left%s", poseInputSuffix);
    std::snprintf(rightBindingText, sizeof(rightBindingText), "/user/hand/right%s", poseInputSuffix);

    std::vector<XrActionSuggestedBinding> bindings(2);
    if (!string_to_path(leftBindingText, &bindings[0].binding) ||
        !string_to_path(rightBindingText, &bindings[1].binding)) {
        return;
    }
    bindings[0].action = handPoseAction_;
    bindings[1].action = handPoseAction_;

    for (const char* hand : {"left", "right"}) {
        XrPath path = XR_NULL_PATH;
        const std::string name = std::string("/user/hand/") + hand + "/input/aim/pose";
        if (string_to_path(name.c_str(), &path)) bindings.push_back({aimPoseAction_, path});
    }
    const bool touch = std::strstr(interactionProfilePath, "oculus") != nullptr;
    const bool vive = std::strstr(interactionProfilePath, "vive") != nullptr;
    for (size_t hand = 0; hand < 2; ++hand) {
        const char* primary = touch && hand == 0 ? "x" : "a";
        const char* secondary = touch && hand == 0 ? "y" : "b";
        std::array<std::string, InputCount> components{
            "trigger/value", vive ? "squeeze/click" : "squeeze/value",
            vive ? "trackpad" : "thumbstick",
            vive ? "" : std::string(primary) + "/click",
            vive ? "" : std::string(secondary) + "/click",
            vive || (touch && hand == 0) ? "menu/click" : "",
            vive ? "trackpad/click" : "thumbstick/click",
            vive ? "" : std::string(primary) + "/touch",
            vive ? "" : std::string(secondary) + "/touch",
            vive ? "" : "trigger/touch",
            vive ? "trackpad/touch" : "thumbstick/touch",
            touch ? "thumbrest/touch" : ""};
        for (size_t i = 0; i < InputCount; ++i) {
            if (components[i].empty()) continue;
            const std::string path = std::string(hand == 0 ? "/user/hand/left/input/" : "/user/hand/right/input/") + components[i];
            XrPath binding = XR_NULL_PATH;
            if (string_to_path(path.c_str(), &binding)) bindings.push_back({inputActions_[i], binding});
        }
    }

    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profilePath;
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    const XrResult result = suggestInteractionProfileBindings_(instance_, &suggested);
    if (result != XR_SUCCESS) {
        std::fprintf(
            stderr,
            "AXRB OpenXR: bindings for %s rejected: %s (%d)\n",
            interactionProfilePath,
            xr_result_name(result),
            result);
    }
}

void OpenXrSession::locate_controller_spaces(axrb::protocol::PoseFrame& frame, XrTime locateTime, uint64_t sequence)
{
    if (!controllerActionsReady_) {
        return;
    }

    XrActiveActionSet activeActionSet{};
    activeActionSet.actionSet = actionSet_;
    activeActionSet.subactionPath = XR_NULL_PATH;
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    XrResult result = syncActions_(session_, &syncInfo);
    if (result == XR_SESSION_NOT_FOCUSED) {
        menuShortcut_.reset();
        return;
    }
    if (result != XR_SUCCESS) {
        menuShortcut_.reset();
        if (!reportedSyncFailure_) {
            std::fprintf(stderr, "AXRB OpenXR: xrSyncActions failed: %s (%d)\n", xr_result_name(result), result);
            reportedSyncFailure_ = true;
        }
        return;
    }

    bool locatedAny = false;
    for (size_t i = 0; i < handSpaces_.size(); ++i) {
        if (handSpaces_[i] == XR_NULL_HANDLE) {
            continue;
        }

        XrActionStateGetInfo stateInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        stateInfo.action = aimPoseAction_;
        stateInfo.subactionPath = handSubactionPaths_[i];
        XrActionStatePose aimState{XR_TYPE_ACTION_STATE_POSE};
        if (getActionStatePose_(session_, &stateInfo, &aimState) == XR_SUCCESS && aimState.isActive) {
            frame.aim_active[i] = 1;
            XrSpaceLocation aimLocation{XR_TYPE_SPACE_LOCATION};
            XrSpaceVelocity aimVelocity{XR_TYPE_SPACE_VELOCITY};
            aimLocation.next = &aimVelocity;
            if (locateSpace_(aimSpaces_[i], localSpace_, locateTime, &aimLocation) == XR_SUCCESS) {
                frame.aim_flags[i] = aimLocation.locationFlags;
                frame.aim[i] = to_protocol_pose(aimLocation.pose);
                frame.aim_velocity[i] = to_protocol_velocity(aimVelocity, aimLocation.locationFlags);
            }
        }
        stateInfo.action = handPoseAction_;
        stateInfo.subactionPath = handSubactionPaths_[i];
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        result = getActionStatePose_(session_, &stateInfo, &state);
        if (result != XR_SUCCESS || state.isActive == XR_FALSE) {
            continue;
        }

        auto& input = frame.controllers[i];
        input.active = 1;
        constexpr uint32_t buttonBits[] = {axrb::protocol::PrimaryClick, axrb::protocol::SecondaryClick,
            axrb::protocol::MenuClick, axrb::protocol::StickClick, axrb::protocol::PrimaryTouch,
            axrb::protocol::SecondaryTouch, axrb::protocol::TriggerTouch, axrb::protocol::StickTouch,
            axrb::protocol::ThumbrestTouch};
        for (size_t action = 0; action < InputCount; ++action) {
            stateInfo.action = inputActions_[action];
            if (action <= Squeeze) {
                XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
                if (getActionStateFloat_(session_, &stateInfo, &value) == XR_SUCCESS && value.isActive)
                    (action == Trigger ? input.trigger : input.squeeze) = value.currentState;
            } else if (action == Stick) {
                XrActionStateVector2f value{XR_TYPE_ACTION_STATE_VECTOR2F};
                if (getActionStateVector2f_(session_, &stateInfo, &value) == XR_SUCCESS && value.isActive) {
                    input.stick_x = value.currentState.x; input.stick_y = value.currentState.y;
                }
            } else {
                XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
                if (getActionStateBoolean_(session_, &stateInfo, &value) == XR_SUCCESS && value.isActive && value.currentState)
                    input.buttons |= buttonBits[action - Primary];
            }
        }

        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        XrSpaceVelocity gripVelocity{XR_TYPE_SPACE_VELOCITY};
        location.next = &gripVelocity;
        result = locateSpace_(handSpaces_[i], localSpace_, locateTime, &location);
        if (result == XR_SUCCESS) frame.grip_flags[i] = location.locationFlags;
        if (result != XR_SUCCESS ||
            (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0 ||
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
            continue;
        }

        axrb::protocol::Pose& target = (i == 0) ? frame.left_controller : frame.right_controller;
        target = to_protocol_pose(location.pose);
        frame.grip_velocity[i] = to_protocol_velocity(gripVelocity, location.locationFlags);
        locatedAny = true;
    }

    menuShortcut_.apply(frame.controllers, static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()));

    if (locatedAny) {
        if (sequence % kPoseLogInterval == 0) {
            std::fprintf(
                stderr,
                "AXRB OpenXR: controllers seq=%llu left=(%.3f %.3f %.3f) right=(%.3f %.3f %.3f)\n",
                static_cast<unsigned long long>(sequence),
                frame.left_controller.x,
                frame.left_controller.y,
                frame.left_controller.z,
                frame.right_controller.x,
                frame.right_controller.y,
                frame.right_controller.z);
        }
    }
}

} // namespace axrb::host::detail

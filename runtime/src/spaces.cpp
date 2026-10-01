#include "runtime_internal.h"
#include "openxr_dispatch/space_velocity_types.h"
#include "space_velocity.h"
#include <atomic>

namespace axrb::runtime::detail {

namespace {
axrb::protocol::Pose velocity_pose(const XrPosef& p) {
    return {p.position.x,p.position.y,p.position.z,p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w};
}
axrb::protocol::SpaceVelocity world_velocity(const SpaceRecord& record, const axrb::protocol::PoseFrame& f) {
    using axrb::protocol::SpaceVelocity;
    SpaceVelocity v{};
    if (record.kind == SpaceKind::Reference) v.flags=3; // Fixed tracking-world reference.
    else if (f.version >= 7) {
        switch(record.kind) {
            case SpaceKind::Local: v=f.local_origin_velocity; break;
            case SpaceKind::View: v=f.hmd_velocity; break;
            case SpaceKind::LeftHand: v=f.grip_velocity[0]; break;
            case SpaceKind::RightHand: v=f.grip_velocity[1]; break;
            case SpaceKind::LeftAim: v=f.aim_velocity[0]; break;
            case SpaceKind::RightAim: v=f.aim_velocity[1]; break;
            default: break;
        }
    }
    auto parent=record;
    parent.offsetInParent=identity_pose();
    const auto pose=velocity_pose(world_pose_for_space(parent,f));
    const auto& p=record.offsetInParent.position;
    return axrb::protocol::offset_velocity(v,axrb::protocol::rotate(pose,{p.x,p.y,p.z}));
}
}

XrResult XRAPI_CALL xrEnumerateReferenceSpaces_impl(
    XrSession session,
    uint32_t spaceCapacityInput,
    uint32_t* spaceCountOutput,
    XrReferenceSpaceType* spaces)
{
    log_call("xrEnumerateReferenceSpaces");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (spaceCountOutput == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    constexpr XrReferenceSpaceType kSpaces[] = {
        XR_REFERENCE_SPACE_TYPE_VIEW,
        XR_REFERENCE_SPACE_TYPE_LOCAL,
        XR_REFERENCE_SPACE_TYPE_STAGE,
    };

    *spaceCountOutput = static_cast<uint32_t>(sizeof(kSpaces) / sizeof(kSpaces[0]));
    if (spaceCapacityInput > 0 && spaces != nullptr) {
        const uint32_t count = spaceCapacityInput < *spaceCountOutput ? spaceCapacityInput : *spaceCountOutput;
        for (uint32_t i = 0; i < count; ++i) {
            spaces[i] = kSpaces[i];
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrGetReferenceSpaceBoundsRect_impl(
    XrSession session, XrReferenceSpaceType type, XrExtent2Df* bounds)
{
    if (!is_valid_session(session)) return XR_ERROR_HANDLE_INVALID;
    if (!bounds) return XR_ERROR_VALIDATION_FAILURE;
    if (type != XR_REFERENCE_SPACE_TYPE_VIEW && type != XR_REFERENCE_SPACE_TYPE_LOCAL &&
        type != XR_REFERENCE_SPACE_TYPE_STAGE) return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
    *bounds = {};
    // The pose bridge does not yet carry the host's calibrated play-area bounds.
    return XR_SPACE_BOUNDS_UNAVAILABLE;
}

XrResult XRAPI_CALL xrCreateReferenceSpace_impl(
    XrSession session,
    const XrReferenceSpaceCreateInfo* createInfo,
    XrSpace* space)
{
    log_call("xrCreateReferenceSpace");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || space == nullptr || createInfo->type != XR_TYPE_REFERENCE_SPACE_CREATE_INFO) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (createInfo->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_VIEW &&
        createInfo->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL &&
        createInfo->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE) {
        return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
    }

    *space = make_space(
        createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_VIEW ? SpaceKind::View :
        createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL ? SpaceKind::Local : SpaceKind::Reference,
        createInfo->poseInReferenceSpace);
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Space", "create type=%d offset=(%.3f %.3f %.3f)",
        createInfo->referenceSpaceType, createInfo->poseInReferenceSpace.position.x,
        createInfo->poseInReferenceSpace.position.y, createInfo->poseInReferenceSpace.position.z);
#endif
    if (*space == nullptr) {
        return XR_ERROR_OUT_OF_MEMORY;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrCreateActionSpace_impl(
    XrSession session,
    const XrActionSpaceCreateInfo* createInfo,
    XrSpace* space)
{
    log_call("xrCreateActionSpace");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo == nullptr || createInfo->type != XR_TYPE_ACTION_SPACE_CREATE_INFO || space == nullptr ||
        !is_valid_action(createInfo->action)) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    auto kind = action_space_kind(createInfo->subactionPath);
    if (createInfo->subactionPath == XR_NULL_PATH) {
        for (auto path : reinterpret_cast<ActionRecord*>(createInfo->action)->bindings) {
            const std::string_view binding{path_text(path)};
            if (binding.starts_with("/user/hand/left/")) { kind = SpaceKind::LeftHand; break; }
            if (binding.starts_with("/user/hand/right/")) { kind = SpaceKind::RightHand; break; }
        }
    }
    for (auto path : reinterpret_cast<ActionRecord*>(createInfo->action)->bindings) {
        const std::string_view binding{path_text(path)};
        if (binding == "/user/hand/left/input/aim/pose" && kind == SpaceKind::LeftHand) kind = SpaceKind::LeftAim;
        if (binding == "/user/hand/right/input/aim/pose" && kind == SpaceKind::RightHand) kind = SpaceKind::RightAim;
    }
    *space = make_space(kind, createInfo->poseInActionSpace);
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "AXRB.Input", "space action=%p hand=%s kind=%d offset=(%.3f %.3f %.3f) q=(%.3f %.3f %.3f %.3f)",
        reinterpret_cast<void*>(createInfo->action), path_text(createInfo->subactionPath), static_cast<int>(kind),
        createInfo->poseInActionSpace.position.x, createInfo->poseInActionSpace.position.y, createInfo->poseInActionSpace.position.z,
        createInfo->poseInActionSpace.orientation.x, createInfo->poseInActionSpace.orientation.y,
        createInfo->poseInActionSpace.orientation.z, createInfo->poseInActionSpace.orientation.w);
#endif
    if (*space == nullptr) {
        return XR_ERROR_OUT_OF_MEMORY;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrLocateSpace_impl(
    XrSpace space,
    XrSpace baseSpace,
    XrTime time,
    XrSpaceLocation* location)
{
    log_call("xrLocateSpace");
    SpaceRecord* spaceRecord = find_space(space);
    SpaceRecord* baseRecord = find_space(baseSpace);
    if (spaceRecord == nullptr || baseRecord == nullptr) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (location == nullptr || location->type != XR_TYPE_SPACE_LOCATION) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    (void)time;
    location->locationFlags =
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    const axrb::protocol::PoseFrame& poseFrame = pose_client().latest_pose_frame();
    auto tracking_flags = [&](const SpaceRecord& record) -> XrSpaceLocationFlags {
        if (poseFrame.version >= 5 && record.kind == SpaceKind::Local) return poseFrame.local_origin_flags;
        if (poseFrame.version >= 5 && record.kind == SpaceKind::View) return poseFrame.hmd_flags;
        const bool aim = record.kind == SpaceKind::LeftAim || record.kind == SpaceKind::RightAim;
        const bool left = record.kind == SpaceKind::LeftHand || record.kind == SpaceKind::LeftAim;
        const bool right = record.kind == SpaceKind::RightHand || record.kind == SpaceKind::RightAim;
        if (!left && !right) return 15;
        const size_t hand = right ? 1 : 0;
        if (poseFrame.version < 3) return poseFrame.controllers[hand].active ? 15 : 0;
        if (!(aim ? poseFrame.aim_active[hand] : poseFrame.controllers[hand].active)) return 0;
        return aim ? poseFrame.aim_flags[hand] : poseFrame.grip_flags[hand];
    };
    location->locationFlags = tracking_flags(*spaceRecord) & tracking_flags(*baseRecord);
    const XrPosef spaceWorld = world_pose_for_space(*spaceRecord, poseFrame);
    const XrPosef baseWorld = world_pose_for_space(*baseRecord, poseFrame);
    location->pose = multiply_pose(inverse_pose(baseWorld), spaceWorld);
    struct Chain { XrStructureType type; void* next; };
    for (auto* chain=static_cast<Chain*>(location->next); chain; chain=static_cast<Chain*>(chain->next)) {
        if (chain->type != XR_TYPE_SPACE_VELOCITY) continue;
        auto* velocity=reinterpret_cast<XrSpaceVelocity*>(chain);
        axrb::protocol::SpaceVelocity v{};
        if ((location->locationFlags & 3) == 3) {
            if (space == baseSpace) v.flags=3;
            else v=axrb::protocol::relative_velocity(world_velocity(*spaceRecord,poseFrame),
                world_velocity(*baseRecord,poseFrame),velocity_pose(spaceWorld),velocity_pose(baseWorld));
        }
        velocity->velocityFlags=v.flags;
        velocity->linearVelocity={v.linear.x,v.linear.y,v.linear.z};
        velocity->angularVelocity={v.angular.x,v.angular.y,v.angular.z};
#if defined(__ANDROID__)
        // Bounded evidence that the game requests and receives velocity. No per-frame log flood.
        static std::atomic<uint64_t> calls{0};
        const auto sample=calls.fetch_add(1,std::memory_order_relaxed);
        if (sample<12 || sample%900==0) __android_log_print(ANDROID_LOG_INFO,"AXRB.Velocity",
            "request=%llu kind=%d base=%d protocol=%u velocityFlags=%llu poseFlags=%llu seq=%llu aimActive=%u/%u controllerActive=%u/%u linear=(%.3f %.3f %.3f)",
            static_cast<unsigned long long>(sample),static_cast<int>(spaceRecord->kind),static_cast<int>(baseRecord->kind),
            poseFrame.version,static_cast<unsigned long long>(v.flags),
            static_cast<unsigned long long>(location->locationFlags),static_cast<unsigned long long>(poseFrame.sequence),
            poseFrame.aim_active[0],poseFrame.aim_active[1],poseFrame.controllers[0].active,poseFrame.controllers[1].active,
            v.linear.x,v.linear.y,v.linear.z);
#endif
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrLocateViews_impl(
    XrSession session,
    const XrViewLocateInfo* viewLocateInfo,
    XrViewState* viewState,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrView* views)
{
    log_call("xrLocateViews");
    if (!is_valid_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (viewLocateInfo == nullptr || viewState == nullptr || viewCountOutput == nullptr ||
        viewLocateInfo->type != XR_TYPE_VIEW_LOCATE_INFO || viewState->type != XR_TYPE_VIEW_STATE) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (viewLocateInfo->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    if (find_space(viewLocateInfo->space) == nullptr) {
        return XR_ERROR_HANDLE_INVALID;
    }

    *viewCountOutput = 2;
    viewState->viewStateFlags =
        XR_VIEW_STATE_ORIENTATION_VALID_BIT |
        XR_VIEW_STATE_POSITION_VALID_BIT |
        XR_VIEW_STATE_ORIENTATION_TRACKED_BIT |
        XR_VIEW_STATE_POSITION_TRACKED_BIT;

    SpaceRecord* baseRecord = find_space(viewLocateInfo->space);
    const axrb::protocol::PoseFrame& poseFrame = pose_client().latest_pose_frame();
    g_lastViewPoseFrame = poseFrame;
    if (poseFrame.version >= 5) {
        viewState->viewStateFlags = poseFrame.hmd_flags & 15;
        if (baseRecord->kind == SpaceKind::Local) viewState->viewStateFlags &= poseFrame.local_origin_flags;
    }
    if (viewCapacityInput == 0 || views == nullptr) return XR_SUCCESS;
    const XrPosef hmdWorld = protocol_pose_to_xr(poseFrame.hmd);
    const XrPosef baseWorld = world_pose_for_space(*baseRecord, poseFrame);
    const uint32_t count = viewCapacityInput < 2 ? viewCapacityInput : 2;
    for (uint32_t i = 0; i < count; ++i) {
        if (views[i].type != XR_TYPE_VIEW) {
            return XR_ERROR_VALIDATION_FAILURE;
        }

        XrPosef eyeOffset = identity_pose();
        eyeOffset.position.x = i == 0 ? -kEyeHalfIpdMeters : kEyeHalfIpdMeters;
        const XrPosef eyeWorld = multiply_pose(hmdWorld, eyeOffset);
        views[i].pose = multiply_pose(inverse_pose(baseWorld), eyeWorld);
        if (axrb::protocol::has_valid_view_fovs(poseFrame)) {
            const auto& fov = poseFrame.view_fov[i];
            views[i].fov = {fov.angle_left, fov.angle_right, fov.angle_up, fov.angle_down};
        } else {
            views[i].fov = {
                -kProjectionHalfFovRadians,
                kProjectionHalfFovRadians,
                kProjectionHalfFovRadians,
                -kProjectionHalfFovRadians};
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL xrDestroySpace_impl(XrSpace space)
{
    log_call("xrDestroySpace");
    auto* record = find_space(space);
    if (!record) return XR_ERROR_HANDLE_INVALID;
    record->handle.magic = 0;
    return XR_SUCCESS;
}


} // namespace axrb::runtime::detail

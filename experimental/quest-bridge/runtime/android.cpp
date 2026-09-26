/* Arm64 Android face of the Quest Bridge runtime.

   The loader in a Quest process calls xrNegotiateLoaderRuntimeInterface and
   then the functions returned from xrGetInstanceProcAddr. This build reports
   a Quest 3 and advertises the OpenGL ES enable extension. Swapchain images
   are not wired to the Windows shared textures in this file yet: a guest
   process cannot open those NT handles. */

#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <cstdio>
#include <cstring>
#include <vector>

#define QB_EXPORT extern "C" __attribute__((visibility("default")))

struct Runtime {
    bool alive = false;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    std::vector<XrEventDataSessionStateChanged> events;
};

static Runtime g;

static void push_state(XrSessionState state) {
    g.state = state;
    XrEventDataSessionStateChanged ev{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    ev.session = reinterpret_cast<XrSession>(&g);
    ev.state = state;
    g.events.push_back(ev);
}

static XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(const char* layerName, uint32_t capacity,
                                                                  uint32_t* count, XrExtensionProperties* properties) {
    if (layerName != nullptr) return XR_ERROR_API_LAYER_NOT_PRESENT;
    if (count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 1 || properties == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    std::snprintf(properties[0].extensionName, XR_MAX_EXTENSION_NAME_SIZE, "XR_KHR_opengl_es_enable");
    properties[0].extensionVersion = 10;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo*, XrInstance* instance) {
    if (instance == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    if (g.alive) return XR_ERROR_LIMIT_REACHED;
    g = {};
    g.alive = true;
    *instance = reinterpret_cast<XrInstance>(&g);
    std::printf("qb-runtime: android instance\n");
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrDestroyInstance(XrInstance) {
    g = {};
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrGetInstanceProperties(XrInstance, XrInstanceProperties* properties) {
    if (properties == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    std::snprintf(properties->runtimeName, XR_MAX_RUNTIME_NAME_SIZE, "Quest Bridge");
    properties->runtimeVersion = XR_MAKE_VERSION(0, 1, 0);
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrGetSystem(XrInstance, const XrSystemGetInfo* info, XrSystemId* system) {
    if (info == nullptr || system == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
    *system = 1;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrGetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* properties) {
    if (properties == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    properties->systemId = 1;
    properties->vendorId = 0x2833;
    std::snprintf(properties->systemName, XR_MAX_SYSTEM_NAME_SIZE, "Meta Quest 3");
    properties->graphicsProperties.maxSwapchainImageHeight = 4096;
    properties->graphicsProperties.maxSwapchainImageWidth = 4096;
    properties->graphicsProperties.maxLayerCount = 16;
    properties->trackingProperties.orientationTracking = XR_TRUE;
    properties->trackingProperties.positionTracking = XR_TRUE;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrEnumerateViewConfigurations(XrInstance, XrSystemId, uint32_t capacity, uint32_t* count,
                                                         XrViewConfigurationType* types) {
    if (count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 1 || types == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    types[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType,
                                                             uint32_t capacity, uint32_t* count,
                                                             XrViewConfigurationView* views) {
    if (count == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *count = 2;
    if (capacity == 0) return XR_SUCCESS;
    if (capacity < 2 || views == nullptr) return XR_ERROR_SIZE_INSUFFICIENT;
    for (int i = 0; i < 2; ++i) {
        views[i].recommendedImageRectWidth = 1872;
        views[i].maxImageRectWidth = 1872;
        views[i].recommendedImageRectHeight = 2016;
        views[i].maxImageRectHeight = 2016;
        views[i].recommendedSwapchainSampleCount = 1;
        views[i].maxSwapchainSampleCount = 1;
    }
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrCreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* session) {
    if (session == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    std::fprintf(stderr, "qb-runtime: GLES session is not connected to the host textures yet\n");
    *session = reinterpret_cast<XrSession>(&g);
    push_state(XR_SESSION_STATE_IDLE);
    push_state(XR_SESSION_STATE_READY);
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrPollEvent(XrInstance, XrEventDataBuffer* event) {
    if (event == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    if (g.events.empty()) return XR_EVENT_UNAVAILABLE;
    XrEventDataSessionStateChanged ev = g.events.front();
    g.events.erase(g.events.begin());
    std::memcpy(event, &ev, sizeof(ev));
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrBeginSession(XrSession, const XrSessionBeginInfo*) {
    push_state(XR_SESSION_STATE_SYNCHRONIZED);
    push_state(XR_SESSION_STATE_VISIBLE);
    push_state(XR_SESSION_STATE_FOCUSED);
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* frame) {
    if (frame == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    frame->predictedDisplayTime = 0;
    frame->predictedDisplayPeriod = 11111111;
    frame->shouldRender = XR_FALSE;
    return XR_SUCCESS;
}

static XrResult XRAPI_CALL xrBeginFrame(XrSession, const XrFrameBeginInfo*) { return XR_SUCCESS; }

static XrResult XRAPI_CALL xrEndFrame(XrSession, const XrFrameEndInfo*) { return XR_SUCCESS; }

static XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function);

#define QB_FUNCS(X)                                                                                                    \
    X(xrEnumerateInstanceExtensionProperties)                                                                          \
    X(xrCreateInstance)                                                                                                \
    X(xrDestroyInstance)                                                                                               \
    X(xrGetInstanceProperties)                                                                                         \
    X(xrGetSystem)                                                                                                     \
    X(xrGetSystemProperties)                                                                                           \
    X(xrEnumerateViewConfigurations)                                                                                   \
    X(xrEnumerateViewConfigurationViews)                                                                               \
    X(xrCreateSession)                                                                                                 \
    X(xrPollEvent)                                                                                                     \
    X(xrBeginSession)                                                                                                  \
    X(xrWaitFrame)                                                                                                     \
    X(xrBeginFrame)                                                                                                    \
    X(xrEndFrame)                                                                                                      \
    X(xrGetInstanceProcAddr)

static XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function) {
    if (name == nullptr || function == nullptr) return XR_ERROR_VALIDATION_FAILURE;
    *function = nullptr;
#define MATCH(fn)                                                                                                      \
    if (std::strcmp(name, #fn) == 0) {                                                                                 \
        *function = reinterpret_cast<PFN_xrVoidFunction>(fn);                                                          \
        return XR_SUCCESS;                                                                                             \
    }
    QB_FUNCS(MATCH)
#undef MATCH
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

QB_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* loaderInfo,
                                                                XrNegotiateRuntimeRequest* runtimeRequest) {
    if (loaderInfo == nullptr || runtimeRequest == nullptr) return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO) return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->minApiVersion > XR_CURRENT_API_VERSION) return XR_ERROR_INITIALIZATION_FAILED;
    runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    runtimeRequest->runtimeApiVersion = XR_CURRENT_API_VERSION;
    if (loaderInfo->maxApiVersion < runtimeRequest->runtimeApiVersion)
        runtimeRequest->runtimeApiVersion = loaderInfo->maxApiVersion;
    runtimeRequest->getInstanceProcAddr = xrGetInstanceProcAddr;
    return XR_SUCCESS;
}

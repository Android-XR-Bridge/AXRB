// SPDX-License-Identifier: MIT
#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>
#include <cstring>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <sys/system_properties.h>
#include "../../src/openxr_dispatch/openxr_minimal.h"
#include "legacy_function_lookup.h"

static void* runtime = nullptr;
static PFN_xrGetInstanceProcAddr gipa = nullptr;
static PFN_xrNegotiateLoaderRuntimeInterface negotiateRuntime = nullptr;
static bool controllersOnly = false;
using EnumerateExtensions = XrResult (*)(const char*, uint32_t, uint32_t*, XrExtensionProperties*);
using GetPoseState = XrResult (*)(XrSession, const XrActionStateGetInfo*, XrActionStatePose*);
using GetFloatState = XrResult (*)(XrSession, const XrActionStateGetInfo*, XrActionStateFloat*);
static EnumerateExtensions enumerateExtensions = nullptr;
static GetPoseState poseState = nullptr;
static GetFloatState floatState = nullptr;

static XrResult filteredExtensions(const char* layer, uint32_t capacity, uint32_t* count, XrExtensionProperties* properties) {
    if (!controllersOnly || (layer && layer[0])) return enumerateExtensions(layer, capacity, count, properties);
    if (!count) return XR_ERROR_VALIDATION_FAILURE;
    uint32_t originalCount = 0;
    XrResult result = enumerateExtensions(nullptr, 0, &originalCount, nullptr);
    if (result != XR_SUCCESS) return result;
    std::vector<XrExtensionProperties> original(originalCount);
    for (auto& extension : original) extension.type = XR_TYPE_EXTENSION_PROPERTIES;
    result = enumerateExtensions(nullptr, originalCount, &originalCount, original.data());
    if (result != XR_SUCCESS) return result;
    std::vector<XrExtensionProperties> filtered;
    for (const auto& extension : original) {
        if (std::strcmp(extension.extensionName, "XR_EXT_hand_tracking") &&
            std::strcmp(extension.extensionName, "XR_EXT_hand_tracking_data_source")) filtered.push_back(extension);
    }
    *count = static_cast<uint32_t>(filtered.size());
    if (!capacity) return XR_SUCCESS;
    if (capacity < filtered.size()) return XR_ERROR_SIZE_INSUFFICIENT;
    if (!properties) return XR_ERROR_VALIDATION_FAILURE;
    for (size_t i = 0; i < filtered.size(); ++i) {
        if (properties[i].type != XR_TYPE_EXTENSION_PROPERTIES) return XR_ERROR_VALIDATION_FAILURE;
        properties[i].extensionVersion = filtered[i].extensionVersion;
        std::strcpy(properties[i].extensionName, filtered[i].extensionName);
    }
    return XR_SUCCESS;
}
static XrResult loggedPose(XrSession session, const XrActionStateGetInfo* info, XrActionStatePose* state) {
    const XrResult result = poseState(session, info, state);
    static unsigned calls = 0;
    if (++calls % 600 == 0 && state) __android_log_print(ANDROID_LOG_INFO, "AXRB-LegacyInput", "controller pose active=%u result=%d", state->isActive, result);
    return result;
}
static XrResult loggedFloat(XrSession session, const XrActionStateGetInfo* info, XrActionStateFloat* state) {
    const XrResult result = floatState(session, info, state);
    static unsigned calls = 0;
    if (++calls % 600 == 0 && state) __android_log_print(ANDROID_LOG_INFO, "AXRB-LegacyInput", "controller analog active=%u value=%.3f result=%d", state->isActive, state->currentState, result);
    return result;
}
static XrResult compatibleGipa(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    const XrResult result = gipa(instance, name, function);
    if (result != XR_SUCCESS || !name || !function || !*function) return result;
    if (!std::strcmp(name, "xrEnumerateInstanceExtensionProperties")) {
        enumerateExtensions = reinterpret_cast<EnumerateExtensions>(*function);
        *function = reinterpret_cast<PFN_xrVoidFunction>(filteredExtensions);
    } else if (!std::strcmp(name, "xrGetActionStatePose")) {
        poseState = reinterpret_cast<GetPoseState>(*function);
        *function = reinterpret_cast<PFN_xrVoidFunction>(loggedPose);
    } else if (!std::strcmp(name, "xrGetActionStateFloat")) {
        floatState = reinterpret_cast<GetFloatState>(*function);
        *function = reinterpret_cast<PFN_xrVoidFunction>(loggedFloat);
    }
    return result;
}
static XrResult compatibleNegotiate(const XrNegotiateLoaderInfo* info, XrNegotiateRuntimeRequest* request) {
    const XrResult result = negotiateRuntime(info, request);
    if (result == XR_SUCCESS && request) request->getInstanceProcAddr = compatibleGipa;
    return result;
}
static void fail(JNIEnv* env, const char* message) {
    __android_log_print(ANDROID_LOG_ERROR, "AXRB-LegacyDriver", "%s", message);
    env->ThrowNew(env->FindClass("java/lang/UnsatisfiedLinkError"), message);
}
extern "C" JNIEXPORT void JNICALL
Java_com_oculus_systemdriver_DriverLoader_nativeLoad(JNIEnv* env, jclass) {
    if (runtime) return;
    void* handle = dlopen("libopenxr_runtime.so", RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fail(env, dlerror()); return; }
    auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(dlsym(handle, "xrNegotiateLoaderRuntimeInterface"));
    if (!negotiate) { fail(env, "AXRB runtime negotiation symbol missing"); dlclose(handle); return; }
    XrNegotiateLoaderInfo info{};
    info.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    info.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
    info.structSize = sizeof(info);
    info.minInterfaceVersion = info.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    info.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.maxApiVersion = XR_MAKE_VERSION(1, 1023, 4095);
    XrNegotiateRuntimeRequest request{};
    request.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
    request.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
    request.structSize = sizeof(request);
    if (negotiate(&info, &request) != XR_SUCCESS || !request.getInstanceProcAddr) {
        fail(env, "AXRB runtime negotiation failed"); dlclose(handle); return;
    }
    gipa = request.getInstanceProcAddr;
    negotiateRuntime = negotiate;
    runtime = handle;
    char process[128]{};
    if (FILE* file = std::fopen("/proc/self/cmdline", "rb")) {
        std::fread(process, 1, sizeof(process)-1, file); std::fclose(file);
    }
    char key[256]{};
    char flag[PROP_VALUE_MAX]{};
    std::snprintf(key, sizeof(key), "persist.sys.axrb.legacy.controllers_only.%s", process);
    __system_property_get(key, flag);
    controllersOnly = !std::strcmp(flag, "1");
    __android_log_print(ANDROID_LOG_INFO, "AXRB-LegacyDriver", "controller-only compatibility=%d", controllersOnly);
    __android_log_print(ANDROID_LOG_INFO, "AXRB-LegacyDriver", "AXRB ARM64 runtime loaded; legacy interface ready");
}
extern "C" JNIEXPORT jlong JNICALL
Java_com_oculus_systemdriver_DriverLoader_nativeGetProcAddr(JNIEnv* env, jclass clazz, jstring name) {
    if (!runtime) Java_com_oculus_systemdriver_DriverLoader_nativeLoad(env, clazz);
    if (!runtime || env->ExceptionCheck() || !name) return 0;
    const char* text = env->GetStringUTFChars(name, nullptr);
    if (!text) return 0;
    void* result = nullptr;
    if (!std::strcmp(text, "xrGetInstanceProcAddr")) result = reinterpret_cast<void*>(compatibleGipa);
    else if (!std::strcmp(text, "xrNegotiateLoaderRuntimeInterface")) result = reinterpret_cast<void*>(compatibleNegotiate);
    else result = reinterpret_cast<void*>(resolve_legacy_function(text,
        reinterpret_cast<PFN_xrVoidFunction>(dlsym(runtime, text)), compatibleGipa));
    __android_log_print(ANDROID_LOG_INFO, "AXRB-LegacyDriver", "Requested %s: %s", text, result ? "available" : "unavailable");
    env->ReleaseStringUTFChars(name, text);
    return reinterpret_cast<intptr_t>(result);
}

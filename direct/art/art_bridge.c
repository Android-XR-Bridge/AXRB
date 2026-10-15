#include <jni.h>
#include <stdint.h>
#include <stdatomic.h>
#include <android/native_window_jni.h>
#include "../guest/axrb_guest.h"

extern long axrb_window_register(ANativeWindow*, int, int);
extern int register_art_media(JNIEnv*);
AXRB_HOST_FUNCTION(axrb_window_register, "art.window_register");
extern long axrb_stop_requested(void);
AXRB_HOST_FUNCTION(axrb_stop_requested, "art.stop_requested");
JNIEXPORT jboolean JNICALL Java_com_axrb_runtime_AppProcess_stopRequested(JNIEnv* env, jclass type) {
    (void)env; (void)type;
    return axrb_stop_requested() != 0;
}

static JavaVM* frame_vm;
static jclass frame_class;
static jmethodID frame_create, frame_schedule, frame_latest, frame_dispose;
static jlong display_create(JNIEnv* env, jclass type, jobject receiver, jobject event, jobject queue, jint source, jint flags, jlong layer) {
    (void)type; (void)queue; (void)source; (void)flags; (void)layer;
    return (*env)->CallStaticLongMethod(env, frame_class, frame_create, receiver, event);
}
static void display_schedule(JNIEnv* env, jclass type, jlong receiver) {
    (void)type; (*env)->CallStaticVoidMethod(env, frame_class, frame_schedule, receiver);
}
static jobject display_latest(JNIEnv* env, jclass type, jlong receiver) {
    (void)type; return (*env)->CallStaticObjectMethod(env, frame_class, frame_latest, receiver);
}
static void display_dispose(void* receiver) {
    JNIEnv* env = 0;
    int attached = (*frame_vm)->GetEnv(frame_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK;
    if (attached && (*frame_vm)->AttachCurrentThreadAsDaemon(frame_vm, &env, 0) != JNI_OK) return;
    (*env)->CallStaticVoidMethod(env, frame_class, frame_dispose, (jlong)(uintptr_t)receiver);
    if (attached) (*frame_vm)->DetachCurrentThread(frame_vm);
}
static jlong display_finalizer(JNIEnv* env, jclass type) { (void)env; (void)type; return (jlong)(uintptr_t)display_dispose; }

// The Direct WASAPI backend uses 48 kHz output and 256-frame bursts.
static jint audio_rate(JNIEnv* env, jobject type, ...) { (void)env; (void)type; return 48000; }
static jint audio_frames(JNIEnv* env, jobject type) { (void)env; (void)type; return 256; }
static jint audio_latency(JNIEnv* env, jobject type, jint stream) { (void)env; (void)type; (void)stream; return 20; }
static jint audio_connection(JNIEnv* env, jobject type, jint device, jstring address) {
    (void)env; (void)type; (void)address; return device == 2 || device == 1 ? 1 : 0;
}
static jint audio_new_id(JNIEnv* env, jobject type) {
    (void)env; (void)type; static atomic_int next = 1000; return atomic_fetch_add(&next, 1);
}
static jboolean audio_inactive(JNIEnv* env, jobject type, ...) { (void)env; (void)type; return JNI_FALSE; }
static jstring audio_parameters(JNIEnv* env, jobject type, jstring keys) { (void)type; (void)keys; return (*env)->NewStringUTF(env, ""); }
static jint audio_devices(JNIEnv* env, jobject type, jobject attributes, jobjectArray devices, jboolean volume) {
    (void)env; (void)type; (void)attributes; (void)devices; (void)volume; return 0;
}
static jint audio_patches(JNIEnv* env, jobject type, jobject list, jintArray generation) {
    (void)type;
    if (!list || !generation || (*env)->GetArrayLength(env, generation) != 1) return -2;
    jclass list_type = (*env)->GetObjectClass(env, list);
    jmethodID clear = (*env)->GetMethodID(env, list_type, "clear", "()V");
    (*env)->CallVoidMethod(env, list, clear);
    (*env)->DeleteLocalRef(env, list_type);
    jint value = 1; (*env)->SetIntArrayRegion(env, generation, 0, 1, &value);
    return 0;
}
static jclass audio_host;
static jmethodID audio_list_ports;
static jint audio_ports(JNIEnv* env, jobject type, jobject list, jintArray generation) {
    (void)type;
    return (*env)->CallStaticIntMethod(env, audio_host, audio_list_ports, list, generation);
}
// The virtual default route is stable for the lifetime of a Direct session.
// AOSP delivers initial device callbacks itself. Do not attach its native
// AudioPolicyService watcher: Direct routes PCM through WASAPI instead.
static void audio_events_setup(JNIEnv* env, jobject type, jobject weak) { (void)env; (void)type; (void)weak; }
static void audio_events_finalize(JNIEnv* env, jobject type) { (void)env; (void)type; }
static jint audio_min_buffer(JNIEnv* env, jobject type, jint rate, jint channels, jint format) {
    (void)env; (void)type;
    int bytes = format == 2 ? 2 : format == 3 ? 1 : format == 4 ? 4 : 0;
    if (rate <= 0 || channels <= 0 || !bytes) return -2;
    return ((rate / 50 + 255) / 256) * 256 * channels * bytes;
}

// Handheld Android sensors are absent on the Windows host. Headset tracking
// remains provided by OpenXR. Keep AOSP's SensorManager and report an empty
// device list instead of blocking on an unavailable Android sensor daemon.
static jlong sensor_create(JNIEnv* env, jclass type, jstring package) { (void)env; (void)type; (void)package; return 1; }
static jboolean sensor_at(JNIEnv* env, jclass type, jlong manager, jobject sensor, jint index) {
    (void)env; (void)type; (void)manager; (void)sensor; (void)index; return JNI_FALSE;
}
static void sensor_dynamic(JNIEnv* env, jclass type, jlong manager, jobject list) {
    (void)env; (void)type; (void)manager; (void)list;
}
static jboolean sensor_injection(JNIEnv* env, jclass type, jlong manager) {
    (void)env; (void)type; (void)manager; return JNI_FALSE;
}
static void sensor_runtime(JNIEnv* env, jclass type, jlong manager, jint device, jobject list) {
    (void)env; (void)type; (void)manager; (void)device; (void)list;
}
static jint sensor_unsupported(JNIEnv* env, jclass type, ...) { (void)env; (void)type; return -19; }
static void sensor_destroy_channel(JNIEnv* env, jclass type, jlong manager, jint channel) {
    (void)env; (void)type; (void)manager; (void)channel;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    JNIEnv* env = 0;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
#if defined(__x86_64__)
    // app_process initializes NativeBridge before registering framework JNI,
    // so getAppEnv cannot initialize Build at that point. Apply the translated
    // application's ABI view here, once platform JNI registration is complete.
    jclass build = (*env)->FindClass(env, "android/os/Build");
    jclass string = (*env)->FindClass(env, "java/lang/String");
    if (!build || !string) return JNI_ERR;
    jstring abi = (*env)->NewStringUTF(env, "arm64-v8a");
    jobjectArray abis = (*env)->NewObjectArray(env, 1, string, abi);
    (*env)->SetStaticObjectField(env, build, (*env)->GetStaticFieldID(env, build, "CPU_ABI", "Ljava/lang/String;"), abi);
    (*env)->SetStaticObjectField(env, build, (*env)->GetStaticFieldID(env, build, "SUPPORTED_ABIS", "[Ljava/lang/String;"), abis);
    (*env)->SetStaticObjectField(env, build, (*env)->GetStaticFieldID(env, build, "SUPPORTED_64_BIT_ABIS", "[Ljava/lang/String;"), abis);
    jclass system = (*env)->FindClass(env, "java/lang/System");
    jmethodID set_property = (*env)->GetStaticMethodID(env, system, "setProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    jstring key = (*env)->NewStringUTF(env, "os.arch"), arch = (*env)->NewStringUTF(env, "aarch64");
    jobject previous = (*env)->CallStaticObjectMethod(env, system, set_property, key, arch);
    (*env)->DeleteLocalRef(env, previous); (*env)->DeleteLocalRef(env, key); (*env)->DeleteLocalRef(env, arch);
    (*env)->DeleteLocalRef(env, system); (*env)->DeleteLocalRef(env, abis); (*env)->DeleteLocalRef(env, abi);
    (*env)->DeleteLocalRef(env, string); (*env)->DeleteLocalRef(env, build);
    if ((*env)->ExceptionCheck(env)) return JNI_ERR;
#endif
    jclass sensors = (*env)->FindClass(env, "android/hardware/SystemSensorManager");
    if (!sensors) return JNI_ERR;
    JNINativeMethod methods[] = {
        {"nativeCreate", "(Ljava/lang/String;)J", (void*)sensor_create},
        {"nativeGetSensorAtIndex", "(JLandroid/hardware/Sensor;I)Z", (void*)sensor_at},
        {"nativeGetDefaultDeviceSensorAtIndex", "(JLandroid/hardware/Sensor;I)Z", (void*)sensor_at},
        {"nativeGetDynamicSensors", "(JLjava/util/List;)V", (void*)sensor_dynamic},
        {"nativeGetRuntimeSensors", "(JILjava/util/List;)V", (void*)sensor_runtime},
        {"nativeIsDataInjectionEnabled", "(J)Z", (void*)sensor_injection},
        {"nativeIsReplayDataInjectionEnabled", "(J)Z", (void*)sensor_injection},
        {"nativeIsHalBypassReplayDataInjectionEnabled", "(J)Z", (void*)sensor_injection},
        {"nativeCreateDirectChannel", "(JIJIILandroid/hardware/HardwareBuffer;)I", (void*)sensor_unsupported},
        {"nativeDestroyDirectChannel", "(JI)V", (void*)sensor_destroy_channel},
        {"nativeConfigDirectChannel", "(JIII)I", (void*)sensor_unsupported},
        {"nativeSetOperationParameter", "(JII[F[I)I", (void*)sensor_unsupported},
    };
    if ((*env)->RegisterNatives(env, sensors, methods, sizeof methods / sizeof methods[0]) != 0) return JNI_ERR;
    frame_vm = vm;
    frame_class = (*env)->NewGlobalRef(env, (*env)->FindClass(env, "com/axrb/runtime/FrameScheduler"));
    if (!frame_class) return JNI_ERR;
    frame_create = (*env)->GetStaticMethodID(env, frame_class, "create", "(Ljava/lang/Object;Ljava/lang/Object;)J");
    frame_schedule = (*env)->GetStaticMethodID(env, frame_class, "schedule", "(J)V");
    frame_latest = (*env)->GetStaticMethodID(env, frame_class, "latest", "(J)Ljava/lang/Object;");
    frame_dispose = (*env)->GetStaticMethodID(env, frame_class, "dispose", "(J)V");
    jclass display = (*env)->FindClass(env, "android/view/DisplayEventReceiver");
    if (!display || !frame_create || !frame_schedule || !frame_latest || !frame_dispose) return JNI_ERR;
    const JNINativeMethod displays[] = {
        {"nativeInit", "(Ljava/lang/ref/WeakReference;Ljava/lang/ref/WeakReference;Landroid/os/MessageQueue;IIJ)J", (void*)display_create},
        {"nativeScheduleVsync", "(J)V", (void*)display_schedule},
        {"nativeGetLatestVsyncEventData", "(J)Landroid/view/DisplayEventReceiver$VsyncEventData;", (void*)display_latest},
        {"nativeGetDisplayEventReceiverFinalizer", "()J", (void*)display_finalizer},
    };
    if ((*env)->RegisterNatives(env, display, displays, sizeof displays / sizeof displays[0]) != 0) return JNI_ERR;
    jmethodID install = (*env)->GetStaticMethodID(env, frame_class, "installFinalizer", "(J)V");
    if (!install) return JNI_ERR;
    (*env)->CallStaticVoidMethod(env, frame_class, install, (jlong)(uintptr_t)display_dispose);
    if ((*env)->ExceptionCheck(env)) return JNI_ERR;
    jclass audio = (*env)->FindClass(env, "android/media/AudioSystem");
    audio_host = (*env)->NewGlobalRef(env, (*env)->FindClass(env, "com/axrb/runtime/HostAudio"));
    if (!audio_host) return JNI_ERR;
    audio_list_ports = (*env)->GetStaticMethodID(env, audio_host, "listPorts", "(Ljava/util/ArrayList;[I)I");
    if (!audio_list_ports) return JNI_ERR;
    const JNINativeMethod audio_methods[] = {
        {"getPrimaryOutputSamplingRate", "()I", (void*)audio_rate},
        {"getPrimaryOutputFrameCount", "()I", (void*)audio_frames},
        {"getOutputLatency", "(I)I", (void*)audio_latency},
        {"getDeviceConnectionState", "(ILjava/lang/String;)I", (void*)audio_connection},
        {"newAudioSessionId", "()I", (void*)audio_new_id},
        {"newAudioPlayerId", "()I", (void*)audio_new_id},
        {"newAudioRecorderId", "()I", (void*)audio_new_id},
        {"isStreamActive", "(II)Z", (void*)audio_inactive},
        {"isStreamActiveRemotely", "(II)Z", (void*)audio_inactive},
        {"getParameters", "(Ljava/lang/String;)Ljava/lang/String;", (void*)audio_parameters},
        {"getDevicesForAttributes", "(Landroid/media/AudioAttributes;[Landroid/media/AudioDeviceAttributes;Z)I", (void*)audio_devices},
        {"listAudioPorts", "(Ljava/util/ArrayList;[I)I", (void*)audio_ports},
        {"listAudioPatches", "(Ljava/util/ArrayList;[I)I", (void*)audio_patches},
    };
    if (!audio || (*env)->RegisterNatives(env, audio, audio_methods, sizeof audio_methods / sizeof audio_methods[0]) != 0) return JNI_ERR;
    jclass audio_events = (*env)->FindClass(env, "android/media/AudioPortEventHandler");
    const JNINativeMethod event_methods[] = {
        {"native_setup", "(Ljava/lang/Object;)V", (void*)audio_events_setup},
        {"native_finalize", "()V", (void*)audio_events_finalize},
    };
    if (!audio_events || (*env)->RegisterNatives(env, audio_events, event_methods, sizeof event_methods / sizeof event_methods[0]) != 0) return JNI_ERR;
    jclass track = (*env)->FindClass(env, "android/media/AudioTrack");
    const JNINativeMethod track_queries[] = {
        {"native_get_output_sample_rate", "(I)I", (void*)audio_rate},
        {"native_get_min_buff_size", "(III)I", (void*)audio_min_buffer},
    };
    if (!track || (*env)->RegisterNatives(env, track, track_queries, sizeof track_queries / sizeof track_queries[0]) != 0) return JNI_ERR;
    if (register_art_media(env) != 0) return JNI_ERR;
    return JNI_VERSION_1_6;
}

JNIEXPORT jlong JNICALL Java_com_axrb_runtime_HostWindow_registerSurface(
        JNIEnv* env, jclass type, jobject surface, jint width, jint height) {
    (void)type;
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return 0;
    if (ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888) != 0 ||
        axrb_window_register(window, width, height) != 0) {
        ANativeWindow_release(window);
        return 0;
    }
    // The Java host window holds this reference until its surface is destroyed.
    return (jlong)(uintptr_t)window;
}

JNIEXPORT void JNICALL Java_com_axrb_runtime_HostWindow_releaseSurface(JNIEnv* env, jclass type, jlong window) {
    (void)env; (void)type;
    if (window) ANativeWindow_release((ANativeWindow*)(uintptr_t)window);
}

JNIEXPORT jobject JNICALL Java_com_axrb_runtime_HostSurface_bufferAt(JNIEnv* env, jclass type, jlong address, jint bytes) {
    (void)type;
    if (!address || bytes <= 0) return 0;
    return (*env)->NewDirectByteBuffer(env, (void*)(uintptr_t)address, bytes);
}

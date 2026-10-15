#include <jni.h>
#include <pthread.h>

static JavaVM* vm;
struct callback { jclass cls; jint input; jint result; };

static void* worker(void* argument) {
    struct callback* call = argument;
    JNIEnv* env = 0;
    if ((*vm)->AttachCurrentThread(vm, &env, 0) != JNI_OK) return 0;
    jmethodID method = (*env)->GetStaticMethodID(env, call->cls, "twice", "(I)I");
    if (method) call->result = (*env)->CallStaticIntMethod(env, call->cls, method, call->input);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        call->result = -1;
    }
    (*vm)->DetachCurrentThread(vm);
    return 0;
}

static jint round_trip(JNIEnv* env, jclass cls, jintArray values) {
    struct callback call = { (*env)->NewGlobalRef(env, cls), 0, -1 };
    (*env)->GetIntArrayRegion(env, values, 0, 1, &call.input);
    pthread_t thread;
    if (pthread_create(&thread, 0, worker, &call) == 0) pthread_join(thread, 0);
    (*env)->DeleteGlobalRef(env, call.cls);
    (*env)->SetIntArrayRegion(env, values, 0, 1, &call.result);
    return call.result;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* runtime, void* reserved) {
    (void)reserved;
    vm = runtime;
    JNIEnv* env = 0;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass cls = (*env)->FindClass(env, "ArtSmoke");
    JNINativeMethod method = { "nativeRoundTrip", "([I)I", (void*)round_trip };
    if (!cls || (*env)->RegisterNatives(env, cls, &method, 1)) return JNI_ERR;
    return JNI_VERSION_1_6;
}

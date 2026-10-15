#include <jni.h>
#include <dlfcn.h>
#include <stdio.h>
#include <initializer_list>
#include <android/binder_ibinder.h>
#include <android/binder_ibinder_jni.h>
#include <android/binder_parcel.h>

// AOSP sp<T> is one pointer, with a non-trivial destructor (indirect AAPCS64
// return). These two process-lifetime references deliberately stay retained.
// No private object fields or vtable offsets are accessed.
struct RetainedStrong {
    void* pointer;
    ~RetainedStrong() {}
};

static JavaVM* java_vm;
static jobject java_registry;
static jclass parcel_class;
static jmethodID parcel_obtain, parcel_recycle, binder_transact;
static void* (*view_parcel)(const AParcel*);

static void* binder_symbol(const char* name) {
    if(void* symbol=dlsym(RTLD_DEFAULT,name)) return symbol;
    // app_process loads these in the platform namespace. A bridge loaded by
    // ART's class-loader namespace may need explicit handles to see them.
    static void* binder=dlopen("libbinder.so",RTLD_NOW);
    static void* runtime=dlopen("libandroid_runtime.so",RTLD_NOW);
    static void* ndk=dlopen("libbinder_ndk.so",RTLD_NOW);
    for(void* handle:{binder,runtime,ndk})
        if(handle) if(void* symbol=dlsym(handle,name)) return symbol;
    return nullptr;
}

static binder_status_t registry_transaction(AIBinder*, transaction_code_t code,
                                          const AParcel* input, AParcel* output) {
    JNIEnv* env = nullptr;
    bool attached = java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK;
    if (attached && java_vm->AttachCurrentThreadAsDaemon(&env, nullptr) != JNI_OK) return STATUS_FAILED_TRANSACTION;
    jobject in = env->CallStaticObjectMethod(parcel_class, parcel_obtain, reinterpret_cast<jlong>(view_parcel(input)));
    jobject out = env->CallStaticObjectMethod(parcel_class, parcel_obtain, reinterpret_cast<jlong>(view_parcel(output)));
    jboolean handled = env->CallBooleanMethod(java_registry, binder_transact, code, in, out, 0);
    binder_status_t result = handled ? STATUS_OK : STATUS_UNKNOWN_TRANSACTION;
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); result = STATUS_FAILED_TRANSACTION; }
    env->CallVoidMethod(in, parcel_recycle);
    env->CallVoidMethod(out, parcel_recycle);
    env->DeleteLocalRef(in);
    env->DeleteLocalRef(out);
    if (attached) java_vm->DetachCurrentThread();
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_axrb_runtime_NativeRegistry_installNative(
        JNIEnv* env, jclass, jobject registry) {
    auto convert = reinterpret_cast<RetainedStrong (*)(JNIEnv*, jobject)>(
        binder_symbol("_ZN7android20ibinderForJavaObjectEP7_JNIEnvP8_jobject"));
    auto process = reinterpret_cast<RetainedStrong (*)()>(
        binder_symbol("_ZN7android12ProcessState4selfEv"));
    auto thread = reinterpret_cast<void* (*)()>(
        binder_symbol("_ZN7android14IPCThreadState4selfEv"));
    auto context = reinterpret_cast<void (*)(void*, const RetainedStrong&)>(
        binder_symbol("_ZN7android14IPCThreadState19setTheContextObjectERKNS_2spINS_7BBinderEEE"));
    auto become = reinterpret_cast<bool (*)(void*)>(
        binder_symbol("_ZN7android12ProcessState20becomeContextManagerEv"));
    if (!convert || !process || !thread || !context || !become) {
        fprintf(stderr,"AXRB Binder registry: missing API convert=%p process=%p thread=%p context=%p become=%p\n",reinterpret_cast<void*>(convert),reinterpret_cast<void*>(process),reinterpret_cast<void*>(thread),reinterpret_cast<void*>(context),reinterpret_cast<void*>(become));
        return JNI_FALSE;
    }
    env->GetJavaVM(&java_vm);
    java_registry = env->NewGlobalRef(registry);
    parcel_class = static_cast<jclass>(env->NewGlobalRef(env->FindClass("android/os/Parcel")));
    parcel_obtain = env->GetStaticMethodID(parcel_class, "obtain", "(J)Landroid/os/Parcel;");
    parcel_recycle = env->GetMethodID(parcel_class, "recycle", "()V");
    binder_transact = env->GetMethodID(env->GetObjectClass(registry), "transact", "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z");
    view_parcel = reinterpret_cast<void* (*)(const AParcel*)>(binder_symbol("_Z26AParcel_viewPlatformParcelPK7AParcel"));
    if (!parcel_obtain || !parcel_recycle || !binder_transact || !view_parcel) return JNI_FALSE;
    static AIBinder_Class* type = AIBinder_Class_define("android.os.IServiceManager",
        [](void* data) { return data; }, [](void*) {}, registry_transaction);
    static AIBinder* native = AIBinder_new(type, nullptr);
    jobject wrapped = AIBinder_toJavaBinder(env, native);
    static RetainedStrong binder = convert(env, wrapped);
    env->DeleteLocalRef(wrapped);
    static RetainedStrong state = process();
    if (!binder.pointer || !state.pointer) return JNI_FALSE;
    context(thread(), binder);
    bool installed=become(state.pointer);
    if(!installed) fprintf(stderr,"AXRB Binder registry: becomeContextManager failed\n");
    return installed ? JNI_TRUE : JNI_FALSE;
}

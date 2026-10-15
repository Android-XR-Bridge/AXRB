// The native half of android.app.NativeActivity (frameworks/base
// core/jni/android_app_NativeActivity.cpp), as plain guest code in
// libandroid.so: the framework's NativeActivity loads this library and its
// natives resolve here. Callbacks run on the activity's main thread, as on
// Android. There is no input queue: VR input comes from OpenXR.
#include <dlfcn.h>
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct AAssetManager AAssetManager;
typedef struct AInputQueue AInputQueue;
typedef struct ARect {
    int32_t left, top, right, bottom;
} ARect;

struct ANativeActivity;
typedef struct ANativeActivityCallbacks {
    void (*onStart)(struct ANativeActivity*);
    void (*onResume)(struct ANativeActivity*);
    void* (*onSaveInstanceState)(struct ANativeActivity*, size_t* outSize);
    void (*onPause)(struct ANativeActivity*);
    void (*onStop)(struct ANativeActivity*);
    void (*onDestroy)(struct ANativeActivity*);
    void (*onWindowFocusChanged)(struct ANativeActivity*, int hasFocus);
    void (*onNativeWindowCreated)(struct ANativeActivity*, ANativeWindow*);
    void (*onNativeWindowResized)(struct ANativeActivity*, ANativeWindow*);
    void (*onNativeWindowRedrawNeeded)(struct ANativeActivity*, ANativeWindow*);
    void (*onNativeWindowDestroyed)(struct ANativeActivity*, ANativeWindow*);
    void (*onInputQueueCreated)(struct ANativeActivity*, AInputQueue*);
    void (*onInputQueueDestroyed)(struct ANativeActivity*, AInputQueue*);
    void (*onContentRectChanged)(struct ANativeActivity*, const ARect*);
    void (*onConfigurationChanged)(struct ANativeActivity*);
    void (*onLowMemory)(struct ANativeActivity*);
} ANativeActivityCallbacks;

typedef struct ANativeActivity {
    ANativeActivityCallbacks* callbacks;
    JavaVM* vm;
    JNIEnv* env;
    jobject clazz;
    const char* internalDataPath;
    const char* externalDataPath;
    int32_t sdkVersion;
    void* instance;
    AAssetManager* assetManager;
    const char* obbPath;
} ANativeActivity;

typedef void ANativeActivity_createFunc(ANativeActivity*, void* savedState, size_t savedStateSize);

ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface);
void ANativeWindow_release(ANativeWindow* window);
AAssetManager* AAssetManager_fromJava(JNIEnv* env, jobject assetManager);

// NativeCode in the AOSP implementation.
typedef struct NativeCode {
    ANativeActivity activity;  // first: ANativeActivity* and NativeCode* are interchangeable
    ANativeActivityCallbacks callbacks;
    void* dlhandle;
    ANativeWindow* window;
    jobject assets;  // global reference keeping the AssetManager alive
    char* internal_path;
    char* external_path;
    char* obb_path;
    int32_t last_window_width, last_window_height;
} NativeCode;

static char* utf(JNIEnv* env, jstring s) {
    if (!s) return NULL;
    const char* chars = (*env)->GetStringUTFChars(env, s, NULL);
    char* copy = strdup(chars ? chars : "");
    if (chars) (*env)->ReleaseStringUTFChars(env, s, chars);
    return copy;
}

static char g_dlerror[512];

static NativeCode* code_of(jlong handle) { return (NativeCode*)(intptr_t)handle; }

JNIEXPORT jlong JNICALL Java_android_app_NativeActivity_loadNativeCode(
    JNIEnv* env, jobject clazz, jstring path, jstring funcName, jobject messageQueue, jstring internalDataDir,
    jstring obbDir, jstring externalDataDir, jint sdkVersion, jobject jAssetMgr, jbyteArray savedState,
    jobject classLoader, jstring libraryPath) {
    (void)messageQueue;
    (void)classLoader;
    (void)libraryPath;
    char* lib = utf(env, path);
    char* func = utf(env, funcName);
    void* handle = dlopen(lib, RTLD_NOW);
    if (!handle) {
        const char* e = dlerror();
        strncpy(g_dlerror, e ? e : "dlopen failed", sizeof g_dlerror - 1);
        free(lib);
        free(func);
        return 0;
    }
    ANativeActivity_createFunc* create = (ANativeActivity_createFunc*)dlsym(handle, func);
    if (!create) {
        strncpy(g_dlerror, "native activity entry point not found", sizeof g_dlerror - 1);
        free(lib);
        free(func);
        return 0;
    }
    free(lib);
    free(func);

    NativeCode* code = calloc(1, sizeof *code);
    code->dlhandle = handle;
    code->activity.callbacks = &code->callbacks;
    (*env)->GetJavaVM(env, &code->activity.vm);
    code->activity.env = env;
    code->activity.clazz = (*env)->NewGlobalRef(env, clazz);
    code->internal_path = utf(env, internalDataDir);
    code->external_path = utf(env, externalDataDir);
    code->obb_path = utf(env, obbDir);
    code->activity.internalDataPath = code->internal_path;
    code->activity.externalDataPath = code->external_path;
    code->activity.obbPath = code->obb_path;
    code->activity.sdkVersion = sdkVersion;
    code->assets = jAssetMgr ? (*env)->NewGlobalRef(env, jAssetMgr) : NULL;
    code->activity.assetManager = jAssetMgr ? AAssetManager_fromJava(env, jAssetMgr) : NULL;

    jbyte* state = NULL;
    jsize state_size = 0;
    if (savedState) {
        state_size = (*env)->GetArrayLength(env, savedState);
        if (state_size > 0) {
            state = malloc(state_size);
            (*env)->GetByteArrayRegion(env, savedState, 0, state_size, state);
        }
    }
    create(&code->activity, state, (size_t)state_size);
    free(state);
    return (jlong)(intptr_t)code;
}

JNIEXPORT jstring JNICALL Java_android_app_NativeActivity_getDlError(JNIEnv* env, jobject clazz) {
    (void)clazz;
    return (*env)->NewStringUTF(env, g_dlerror);
}

JNIEXPORT void JNICALL Java_android_app_NativeActivity_unloadNativeCode(JNIEnv* env, jobject clazz, jlong handle) {
    (void)clazz;
    NativeCode* code = code_of(handle);
    if (!code) return;
    if (code->callbacks.onDestroy) code->callbacks.onDestroy(&code->activity);
    if (code->window) ANativeWindow_release(code->window);
    if (code->activity.clazz) (*env)->DeleteGlobalRef(env, code->activity.clazz);
    if (code->assets) (*env)->DeleteGlobalRef(env, code->assets);
    // Like Android, the library stays loaded: its threads may still be running.
    free(code->internal_path);
    free(code->external_path);
    free(code->obb_path);
    free(code);
}

#define LIFECYCLE(java_name, callback)                                                               \
    JNIEXPORT void JNICALL Java_android_app_NativeActivity_##java_name(JNIEnv* env, jobject clazz,     \
                                                                       jlong handle) {                \
        (void)env;                                                                                   \
        (void)clazz;                                                                                 \
        NativeCode* code = code_of(handle);                                                          \
        if (code && code->callbacks.callback) code->callbacks.callback(&code->activity);            \
    }

LIFECYCLE(onStartNative, onStart)
LIFECYCLE(onResumeNative, onResume)
LIFECYCLE(onPauseNative, onPause)
LIFECYCLE(onStopNative, onStop)
LIFECYCLE(onConfigurationChangedNative, onConfigurationChanged)
LIFECYCLE(onLowMemoryNative, onLowMemory)

JNIEXPORT jbyteArray JNICALL Java_android_app_NativeActivity_onSaveInstanceStateNative(JNIEnv* env, jobject clazz,
                                                                                       jlong handle) {
    (void)clazz;
    NativeCode* code = code_of(handle);
    if (!code || !code->callbacks.onSaveInstanceState) return NULL;
    size_t size = 0;
    jbyte* state = (jbyte*)code->callbacks.onSaveInstanceState(&code->activity, &size);
    jbyteArray array = NULL;
    if (state) {
        if (size > 0) {
            array = (*env)->NewByteArray(env, (jsize)size);
            if (array) (*env)->SetByteArrayRegion(env, array, 0, (jsize)size, state);
        }
        free(state);
    }
    return array;
}

JNIEXPORT void JNICALL Java_android_app_NativeActivity_onWindowFocusChangedNative(JNIEnv* env, jobject clazz,
                                                                                 jlong handle, jboolean focused) {
    (void)env;
    (void)clazz;
    NativeCode* code = code_of(handle);
    if (code && code->callbacks.onWindowFocusChanged) code->callbacks.onWindowFocusChanged(&code->activity, focused ? 1 : 0);
}

static void destroy_window(NativeCode* code) {
    if (!code->window) return;
    if (code->callbacks.onNativeWindowDestroyed) code->callbacks.onNativeWindowDestroyed(&code->activity, code->window);
    ANativeWindow_release(code->window);
    code->window = NULL;
}

JNIEXPORT void JNICALL Java_android_app_NativeActivity_onSurfaceCreatedNative(JNIEnv* env, jobject clazz, jlong handle,
                                                                             jobject surface) {
    (void)clazz;
    NativeCode* code = code_of(handle);
    if (!code) return;
    destroy_window(code);
    code->window = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (code->window && code->callbacks.onNativeWindowCreated)
        code->callbacks.onNativeWindowCreated(&code->activity, code->window);
}

JNIEXPORT void JNICALL Java_android_app_NativeActivity_onSurfaceChangedNative(JNIEnv* env, jobject clazz, jlong handle,
                                                                             jobject surface, jint format, jint width,
                                                                             jint height) {
    (void)clazz;
    (void)format;
    NativeCode* code = code_of(handle);
    if (!code) return;
    ANativeWindow* old = code->window;
    ANativeWindow* fresh = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (fresh && old && fresh != old) {
        // The host hands out one ANativeWindow per surface: a different one is a new surface.
        destroy_window(code);
        code->window = fresh;
        if (code->callbacks.onNativeWindowCreated) code->callbacks.onNativeWindowCreated(&code->activity, fresh);
    } else if (!old && fresh) {
        code->window = fresh;
        if (code->callbacks.onNativeWindowCreated) code->callbacks.onNativeWindowCreated(&code->activity, fresh);
    } else {
        if (fresh) ANativeWindow_release(fresh);
        if (code->window && (width != code->last_window_width || height != code->last_window_height) &&
            code->callbacks.onNativeWindowResized)
            code->callbacks.onNativeWindowResized(&code->activity, code->window);
    }
    code->last_window_width = width;
    code->last_window_height = height;
}

JNIEXPORT void JNICALL Java_android_app_NativeActivity_onSurfaceRedrawNeededNative(JNIEnv* env, jobject clazz,
                                                                                  jlong handle, jobject surface) {
    (void)env;
    (void)clazz;
    (void)surface;
    NativeCode* code = code_of(handle);
    if (code && code->window && code->callbacks.onNativeWindowRedrawNeeded)
        code->callbacks.onNativeWindowRedrawNeeded(&code->activity, code->window);
}

JNIEXPORT void JNICALL Java_android_app_NativeActivity_onSurfaceDestroyedNative(JNIEnv* env, jobject clazz,
                                                                               jlong handle) {
    (void)env;
    (void)clazz;
    NativeCode* code = code_of(handle);
    if (code) destroy_window(code);
}

JNIEXPORT void JNICALL Java_android_app_NativeActivity_onContentRectChangedNative(JNIEnv* env, jobject clazz,
                                                                                 jlong handle, jint x, jint y,
                                                                                 jint w, jint h) {
    (void)env;
    (void)clazz;
    NativeCode* code = code_of(handle);
    if (!code || !code->callbacks.onContentRectChanged) return;
    ARect rect = {x, y, x + w, y + h};
    code->callbacks.onContentRectChanged(&code->activity, &rect);
}

// --- ANativeActivity_* : calls back into the Java activity ---------------------------------

static void call_activity(ANativeActivity* activity, const char* name, const char* sig, int a, int b, int argc) {
    JNIEnv* env = NULL;
    if ((*activity->vm)->GetEnv(activity->vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*activity->vm)->AttachCurrentThread(activity->vm, &env, NULL) != JNI_OK) return;
    }
    jclass cls = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID m = (*env)->GetMethodID(env, cls, name, sig);
    if (m) {
        if (argc == 0) (*env)->CallVoidMethod(env, activity->clazz, m);
        else if (argc == 1) (*env)->CallVoidMethod(env, activity->clazz, m, a);
        else (*env)->CallVoidMethod(env, activity->clazz, m, a, b);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
}

void ANativeActivity_finish(ANativeActivity* activity) { call_activity(activity, "finish", "()V", 0, 0, 0); }

void ANativeActivity_setWindowFormat(ANativeActivity* activity, int32_t format) {
    call_activity(activity, "setWindowFormat", "(I)V", format, 0, 1);
}

void ANativeActivity_setWindowFlags(ANativeActivity* activity, uint32_t addFlags, uint32_t removeFlags) {
    call_activity(activity, "setWindowFlags", "(II)V", (int)addFlags, (int)(addFlags | removeFlags), 2);
}

void ANativeActivity_showSoftInput(ANativeActivity* activity, uint32_t flags) { (void)activity; (void)flags; }

void ANativeActivity_hideSoftInput(ANativeActivity* activity, uint32_t flags) { (void)activity; (void)flags; }

// --- input events: no queue ever exists (NativeActivity never creates one) ----------------

typedef struct AInputEvent AInputEvent;
typedef struct ALooper ALooper;

void AInputQueue_attachLooper(AInputQueue* queue, ALooper* looper, int ident, void* callback, void* data) {}
void AInputQueue_detachLooper(AInputQueue* queue) {}
int32_t AInputQueue_hasEvents(AInputQueue* queue) { return 0; }
int32_t AInputQueue_getEvent(AInputQueue* queue, AInputEvent** outEvent) {
    if (outEvent) *outEvent = NULL;
    return -1;
}
int32_t AInputQueue_preDispatchEvent(AInputQueue* queue, AInputEvent* event) { return 0; }
void AInputQueue_finishEvent(AInputQueue* queue, AInputEvent* event, int handled) {}

#define EVENT_GETTER(type, name) \
    type name(const AInputEvent* event) { return 0; }
EVENT_GETTER(int32_t, AInputEvent_getType)
EVENT_GETTER(int32_t, AInputEvent_getDeviceId)
EVENT_GETTER(int32_t, AInputEvent_getSource)
EVENT_GETTER(int32_t, AKeyEvent_getAction)
EVENT_GETTER(int32_t, AKeyEvent_getFlags)
EVENT_GETTER(int32_t, AKeyEvent_getKeyCode)
EVENT_GETTER(int32_t, AKeyEvent_getScanCode)
EVENT_GETTER(int32_t, AKeyEvent_getMetaState)
EVENT_GETTER(int32_t, AKeyEvent_getRepeatCount)
EVENT_GETTER(int64_t, AKeyEvent_getDownTime)
EVENT_GETTER(int64_t, AKeyEvent_getEventTime)
EVENT_GETTER(int32_t, AMotionEvent_getAction)
EVENT_GETTER(int32_t, AMotionEvent_getFlags)
EVENT_GETTER(int32_t, AMotionEvent_getMetaState)
EVENT_GETTER(int32_t, AMotionEvent_getButtonState)
EVENT_GETTER(int32_t, AMotionEvent_getEdgeFlags)
EVENT_GETTER(int64_t, AMotionEvent_getDownTime)
EVENT_GETTER(int64_t, AMotionEvent_getEventTime)
EVENT_GETTER(size_t, AMotionEvent_getPointerCount)
EVENT_GETTER(size_t, AMotionEvent_getHistorySize)

int32_t AMotionEvent_getPointerId(const AInputEvent* event, size_t pointerIndex) { return 0; }
int32_t AMotionEvent_getToolType(const AInputEvent* event, size_t pointerIndex) { return 0; }
float AMotionEvent_getX(const AInputEvent* event, size_t pointerIndex) { return 0; }
float AMotionEvent_getY(const AInputEvent* event, size_t pointerIndex) { return 0; }
float AMotionEvent_getRawX(const AInputEvent* event, size_t pointerIndex) { return 0; }
float AMotionEvent_getRawY(const AInputEvent* event, size_t pointerIndex) { return 0; }
float AMotionEvent_getPressure(const AInputEvent* event, size_t pointerIndex) { return 0; }
float AMotionEvent_getSize(const AInputEvent* event, size_t pointerIndex) { return 0; }
float AMotionEvent_getAxisValue(const AInputEvent* event, int32_t axis, size_t pointerIndex) { return 0; }

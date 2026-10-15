#include "../guest/axrb_guest.h"
#include <android/choreographer.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <android/looper.h>
#include <android/sensor.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>
#include <mutex>
#include <vector>

// Native frame callbacks run on the requesting Android looper, driven by a
// monotonic timer. The renderer continues to pace actual presentation.
static constexpr int64_t period = 16666667;
static int64_t now_ns() { timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
struct AChoreographerFrameCallbackData { int64_t time, id; };
struct FrameRequest {
    int64_t due;
    AChoreographer_frameCallback64 frame;
    AChoreographer_vsyncCallback vsync;
    AChoreographer_refreshRateCallback refresh;
    void* data;
};
struct AChoreographer {
    ALooper* looper;
    int timer;
    int64_t serial = 0;
    std::mutex lock;
    std::vector<FrameRequest> requests;
    void arm() {
        itimerspec value{};
        if (!requests.empty()) {
            int64_t due = std::min_element(requests.begin(), requests.end(), [](auto& a, auto& b) { return a.due < b.due; })->due;
            value.it_value.tv_sec = due / 1000000000LL;
            value.it_value.tv_nsec = due % 1000000000LL;
        }
        timerfd_settime(timer, TFD_TIMER_ABSTIME, &value, nullptr);
    }
    void post(FrameRequest request) { std::lock_guard<std::mutex> guard(lock); requests.push_back(request); arm(); }
    ~AChoreographer() { ALooper_removeFd(looper, timer); close(timer); ALooper_release(looper); }
};
static int dispatch_frames(int fd, int, void* pointer) {
    auto* c = static_cast<AChoreographer*>(pointer);
    uint64_t count; (void)read(fd, &count, sizeof count);
    std::vector<FrameRequest> ready;
    int64_t time = now_ns();
    {
        std::lock_guard<std::mutex> guard(c->lock);
        for (auto it = c->requests.begin(); it != c->requests.end();) {
            if (it->due <= time) { ready.push_back(*it); it = c->requests.erase(it); }
            else ++it;
        }
        c->arm();
    }
    AChoreographerFrameCallbackData frame{time, ++c->serial};
    for (auto& request : ready) {
        if (request.frame) request.frame(time, request.data);
        if (request.vsync) request.vsync(&frame, request.data);
        if (request.refresh) request.refresh(period, request.data);
    }
    return 1;
}
extern "C" {
AChoreographer* AChoreographer_getInstance() {
    struct Owner { AChoreographer* value = nullptr; ~Owner() { delete value; } };
    thread_local Owner owner;
    if (owner.value) return owner.value;
    ALooper* looper = ALooper_forThread();
    if (!looper) return nullptr;
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer < 0) return nullptr;
    ALooper_acquire(looper);
    auto* c = new AChoreographer{looper, timer};
    if (ALooper_addFd(looper, timer, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, dispatch_frames, c) != 1) { delete c; return nullptr; }
    return owner.value = c;
}
void AChoreographer_postFrameCallbackDelayed64(AChoreographer* c, AChoreographer_frameCallback64 callback, void* data, uint32_t delay) {
    if (c && callback) c->post({now_ns() + std::max<int64_t>(period, int64_t(delay) * 1000000), callback, nullptr, nullptr, data});
}
void AChoreographer_postFrameCallback64(AChoreographer* c, AChoreographer_frameCallback64 callback, void* data) { AChoreographer_postFrameCallbackDelayed64(c, callback, data, 0); }
void AChoreographer_postFrameCallback(AChoreographer* c, AChoreographer_frameCallback callback, void* data) { AChoreographer_postFrameCallback64(c, callback, data); }
void AChoreographer_postFrameCallbackDelayed(AChoreographer* c, AChoreographer_frameCallback callback, void* data, long delay) { AChoreographer_postFrameCallbackDelayed64(c, callback, data, std::max<long>(delay, 0)); }
void AChoreographer_postVsyncCallback(AChoreographer* c, AChoreographer_vsyncCallback callback, void* data) { if (c && callback) c->post({now_ns() + period, nullptr, callback, nullptr, data}); }
void AChoreographer_registerRefreshRateCallback(AChoreographer* c, AChoreographer_refreshRateCallback callback, void* data) { if (c && callback) c->post({now_ns() + 1, nullptr, nullptr, callback, data}); }
void AChoreographer_unregisterRefreshRateCallback(AChoreographer* c, AChoreographer_refreshRateCallback callback, void* data) {
    if (!c) return;
    std::lock_guard<std::mutex> guard(c->lock);
    c->requests.erase(std::remove_if(c->requests.begin(), c->requests.end(), [&](auto& r) { return r.refresh == callback && r.data == data; }), c->requests.end()); c->arm();
}
int64_t AChoreographerFrameCallbackData_getFrameTimeNanos(const AChoreographerFrameCallbackData* data) { return data->time; }
size_t AChoreographerFrameCallbackData_getFrameTimelinesLength(const AChoreographerFrameCallbackData*) { return 1; }
size_t AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex(const AChoreographerFrameCallbackData*) { return 0; }
AVsyncId AChoreographerFrameCallbackData_getFrameTimelineVsyncId(const AChoreographerFrameCallbackData* data, size_t) { return data->id; }
int64_t AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(const AChoreographerFrameCallbackData* data, size_t) { return data->time + period; }
int64_t AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos(const AChoreographerFrameCallbackData* data, size_t) { return data->time + period; }
}

// There are no handheld Android sensors. Headset/controller poses use OpenXR.
struct ASensorManager {};
struct ASensorEventQueue {};
extern "C" {
ASensorManager* ASensorManager_getInstance() { static ASensorManager manager; return &manager; }
ASensorManager* ASensorManager_getInstanceForPackage(const char*) { static ASensorManager manager; return &manager; }
int ASensorManager_getSensorList(ASensorManager*, ASensorList* list) { if (list) *list = nullptr; return 0; }
const ASensor* ASensorManager_getDefaultSensor(ASensorManager*, int) { return nullptr; }
const ASensor* ASensorManager_getDefaultSensorEx(ASensorManager*, int, bool) { return nullptr; }
ASensorEventQueue* ASensorManager_createEventQueue(ASensorManager*, ALooper*, int, ALooper_callbackFunc, void*) { return new ASensorEventQueue; }
int ASensorManager_destroyEventQueue(ASensorManager*, ASensorEventQueue* queue) { delete queue; return 0; }
int ASensorManager_createSharedMemoryDirectChannel(ASensorManager*, int, size_t) { return -ENODEV; }
int ASensorManager_createHardwareBufferDirectChannel(ASensorManager*, const AHardwareBuffer*, size_t) { return -ENODEV; }
void ASensorManager_destroyDirectChannel(ASensorManager*, int) {}
int ASensorManager_configureDirectReport(ASensorManager*, const ASensor*, int, int) { return -ENODEV; }
int ASensorEventQueue_registerSensor(ASensorEventQueue*, const ASensor*, int32_t, int64_t) { return -ENODEV; }
int ASensorEventQueue_enableSensor(ASensorEventQueue*, const ASensor*) { return -ENODEV; }
int ASensorEventQueue_disableSensor(ASensorEventQueue*, const ASensor*) { return -ENODEV; }
int ASensorEventQueue_setEventRate(ASensorEventQueue*, const ASensor*, int32_t) { return -ENODEV; }
int ASensorEventQueue_hasEvents(ASensorEventQueue*) { return 0; }
ssize_t ASensorEventQueue_getEvents(ASensorEventQueue*, ASensorEvent*, size_t) { return 0; }
int ASensorEventQueue_requestAdditionalInfoEvents(ASensorEventQueue*, bool) { return 0; }
}

// Windows of AXRB host surfaces (OpenXR Android-surface swapchains) are
// registered with the host, so AMediaCodec can show decoded frames on them.
extern "C" long axrb_mediandk(long op, long id, long a, long b, long c, long d);
AXRB_HOST_FUNCTION(axrb_mediandk, "art.mediandk");
extern "C" ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface) {
    using From = ANativeWindow* (*)(JNIEnv*, jobject);
    static const From original = reinterpret_cast<From>(dlsym(dlopen("/system/lib64/libaxrbndk.so", RTLD_NOW | RTLD_NOLOAD), "ANativeWindow_fromSurface"));
    ANativeWindow* window = original ? original(env, surface) : nullptr;
    if (!window || !env || !surface) return window;
    jclass type = env->GetObjectClass(surface);
    jclass classClass = type ? env->FindClass("java/lang/Class") : nullptr;
    jmethodID getName = classClass ? env->GetMethodID(classClass, "getName", "()Ljava/lang/String;") : nullptr;
    auto name = getName ? static_cast<jstring>(env->CallObjectMethod(type, getName)) : nullptr;
    const char* chars = name ? env->GetStringUTFChars(name, nullptr) : nullptr;
    if (chars && strcmp(chars, "com.axrb.runtime.HostSurface") == 0) {
        jfieldID field = env->GetFieldID(type, "mediaHandle", "J");
        if (field) axrb_mediandk(41, 0, reinterpret_cast<long>(window), static_cast<long>(env->GetLongField(surface, field)), 0, 0);
    }
    if (chars) env->ReleaseStringUTFChars(name, chars);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (name) env->DeleteLocalRef(name);
    if (classClass) env->DeleteLocalRef(classClass);
    if (type) env->DeleteLocalRef(type);
    return window;
}

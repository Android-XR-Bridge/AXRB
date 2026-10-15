// libandroid.so for AXRB Direct.
//
// Windows and assets live on the host (direct/src/ndk_android.cpp): their
// functions trap there. Loopers, sensors and tracing are plain guest code --
// a looper is epoll plus an eventfd, as in libutils, and the device has no
// motion sensors (the headset pose comes from OpenXR).
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "../axrb_guest.h"

#define HOST(name) AXRB_HOST_FUNCTION(name, "android." #name)

// --- native windows ------------------------------------------------------------------
#if defined(AXRB_NATIVE_X64_BRIDGE)
__asm__(".text\n.global ANativeWindow_fromSurface\n.type ANativeWindow_fromSurface,%function\nANativeWindow_fromSurface:\nsvc #0x380\nret\n.size ANativeWindow_fromSurface,.-ANativeWindow_fromSurface\n");
__asm__(".text\n.global ANativeWindow_acquire\n.type ANativeWindow_acquire,%function\nANativeWindow_acquire:\nsvc #0x381\nret\n.size ANativeWindow_acquire,.-ANativeWindow_acquire\n");
__asm__(".text\n.global ANativeWindow_release\n.type ANativeWindow_release,%function\nANativeWindow_release:\nsvc #0x382\nret\n.size ANativeWindow_release,.-ANativeWindow_release\n");
__asm__(".text\n.global ANativeWindow_getWidth\n.type ANativeWindow_getWidth,%function\nANativeWindow_getWidth:\nsvc #0x383\nret\n.size ANativeWindow_getWidth,.-ANativeWindow_getWidth\n");
__asm__(".text\n.global ANativeWindow_getHeight\n.type ANativeWindow_getHeight,%function\nANativeWindow_getHeight:\nsvc #0x384\nret\n.size ANativeWindow_getHeight,.-ANativeWindow_getHeight\n");
__asm__(".text\n.global ANativeWindow_setBuffersGeometry\n.type ANativeWindow_setBuffersGeometry,%function\nANativeWindow_setBuffersGeometry:\nsvc #0x385\nret\n.size ANativeWindow_setBuffersGeometry,.-ANativeWindow_setBuffersGeometry\n");
#else
HOST(ANativeWindow_fromSurface);
HOST(ANativeWindow_acquire);
HOST(ANativeWindow_release);
HOST(ANativeWindow_getWidth);
HOST(ANativeWindow_getHeight);
HOST(ANativeWindow_setBuffersGeometry);
#endif
HOST(ANativeWindow_toSurface);
HOST(ANativeWindow_getFormat);
HOST(ANativeWindow_setBuffersTransform);
HOST(ANativeWindow_setBuffersDataSpace);
HOST(ANativeWindow_getBuffersDataSpace);
HOST(ANativeWindow_setFrameRate);
HOST(ANativeWindow_setFrameRateWithChangeStrategy);
HOST(ANativeWindow_tryAllocateBuffers);
HOST(ANativeWindow_lock);
HOST(ANativeWindow_unlockAndPost);

// --- assets ------------------------------------------------------------------------------
HOST(AAssetManager_fromJava);
HOST(AAssetManager_open);
HOST(AAssetManager_openDir);
HOST(AAssetDir_getNextFileName);
HOST(AAssetDir_rewind);
HOST(AAssetDir_close);
HOST(AAsset_read);
HOST(AAsset_seek);
HOST(AAsset_seek64);
HOST(AAsset_close);
HOST(AAsset_getBuffer);
HOST(AAsset_getLength);
HOST(AAsset_getLength64);
HOST(AAsset_getRemainingLength);
HOST(AAsset_getRemainingLength64);
HOST(AAsset_openFileDescriptor);
HOST(AAsset_openFileDescriptor64);
HOST(AAsset_isAllocated);

// --- configuration (host: the session's display and locale) -----------------------------
HOST(AConfiguration_new);
HOST(AConfiguration_delete);
HOST(AConfiguration_fromAssetManager);
HOST(AConfiguration_getDensity);
HOST(AConfiguration_getOrientation);
HOST(AConfiguration_getScreenWidthDp);
HOST(AConfiguration_getScreenHeightDp);
HOST(AConfiguration_getSmallestScreenWidthDp);
HOST(AConfiguration_getScreenSize);
HOST(AConfiguration_getSdkVersion);
HOST(AConfiguration_getLanguage);
HOST(AConfiguration_getCountry);
HOST(AConfiguration_getTouchscreen);
HOST(AConfiguration_getKeyboard);
HOST(AConfiguration_getNavigation);
HOST(AConfiguration_getUiModeType);
HOST(AConfiguration_getUiModeNight);
HOST(AConfiguration_getLayoutDirection);

// --- loopers -----------------------------------------------------------------------------

typedef int (*ALooper_callbackFunc)(int fd, int events, void* data);

enum {
    ALOOPER_PREPARE_ALLOW_NON_CALLBACKS = 1,
    ALOOPER_POLL_WAKE = -1,
    ALOOPER_POLL_CALLBACK = -2,
    ALOOPER_POLL_TIMEOUT = -3,
    ALOOPER_POLL_ERROR = -4,
    ALOOPER_EVENT_INPUT = 1,
    ALOOPER_EVENT_OUTPUT = 2,
    ALOOPER_EVENT_ERROR = 4,
    ALOOPER_EVENT_HANGUP = 8,
    ALOOPER_EVENT_INVALID = 16,
};

struct looper_entry {
    int fd;
    int ident;
    int events;
    ALooper_callbackFunc callback;
    void* data;
};

typedef struct ALooper {
    int epoll_fd;
    int wake_fd;
    int refs;
    int allow_non_callbacks;
    pthread_mutex_t lock;
    struct looper_entry* entries;
    int count, capacity;
} ALooper;

static pthread_key_t g_looper_key;
static pthread_once_t g_looper_once = PTHREAD_ONCE_INIT;

static void looper_key_init(void) { pthread_key_create(&g_looper_key, NULL); }

static ALooper* looper_new(int allow_non_callbacks) {
    ALooper* l = calloc(1, sizeof *l);
    l->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    l->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    l->refs = 1;
    l->allow_non_callbacks = allow_non_callbacks;
    pthread_mutex_init(&l->lock, NULL);
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = l->wake_fd};
    epoll_ctl(l->epoll_fd, EPOLL_CTL_ADD, l->wake_fd, &ev);
    return l;
}

ALooper* ALooper_forThread(void) {
    pthread_once(&g_looper_once, looper_key_init);
    return pthread_getspecific(g_looper_key);
}

ALooper* ALooper_prepare(int opts) {
    ALooper* l = ALooper_forThread();
    if (!l) {
        l = looper_new(opts & ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
        pthread_setspecific(g_looper_key, l);
    }
    return l;
}

void ALooper_acquire(ALooper* looper) { __atomic_add_fetch(&looper->refs, 1, __ATOMIC_RELAXED); }

void ALooper_release(ALooper* looper) {
    if (__atomic_sub_fetch(&looper->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        close(looper->epoll_fd);
        close(looper->wake_fd);
        free(looper->entries);
        free(looper);
    }
}

void ALooper_wake(ALooper* looper) {
    uint64_t one = 1;
    while (write(looper->wake_fd, &one, sizeof one) < 0 && errno == EINTR) {}
}

static uint32_t to_epoll(int events) {
    uint32_t e = 0;
    if (events & ALOOPER_EVENT_INPUT) e |= EPOLLIN;
    if (events & ALOOPER_EVENT_OUTPUT) e |= EPOLLOUT;
    return e;
}

static int from_epoll(uint32_t e) {
    int events = 0;
    if (e & EPOLLIN) events |= ALOOPER_EVENT_INPUT;
    if (e & EPOLLOUT) events |= ALOOPER_EVENT_OUTPUT;
    if (e & EPOLLERR) events |= ALOOPER_EVENT_ERROR;
    if (e & EPOLLHUP) events |= ALOOPER_EVENT_HANGUP;
    return events;
}

int ALooper_addFd(ALooper* looper, int fd, int ident, int events, ALooper_callbackFunc callback, void* data) {
    if (!callback && ident < 0) return -1;
    if (callback) ident = ALOOPER_POLL_CALLBACK;
    pthread_mutex_lock(&looper->lock);
    struct epoll_event ev = {.events = to_epoll(events), .data.fd = fd};
    int i;
    for (i = 0; i < looper->count; ++i)
        if (looper->entries[i].fd == fd) break;
    if (i == looper->count) {
        if (looper->count == looper->capacity) {
            looper->capacity = looper->capacity ? looper->capacity * 2 : 8;
            looper->entries = realloc(looper->entries, sizeof(struct looper_entry) * looper->capacity);
        }
        looper->count++;
        epoll_ctl(looper->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    } else {
        epoll_ctl(looper->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
    }
    looper->entries[i] = (struct looper_entry){fd, ident, events, callback, data};
    pthread_mutex_unlock(&looper->lock);
    return 1;
}

int ALooper_removeFd(ALooper* looper, int fd) {
    pthread_mutex_lock(&looper->lock);
    int removed = 0;
    for (int i = 0; i < looper->count; ++i) {
        if (looper->entries[i].fd == fd) {
            looper->entries[i] = looper->entries[--looper->count];
            epoll_ctl(looper->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            removed = 1;
            break;
        }
    }
    pthread_mutex_unlock(&looper->lock);
    return removed;
}

static int find_entry(ALooper* looper, int fd, struct looper_entry* out) {
    pthread_mutex_lock(&looper->lock);
    int found = 0;
    for (int i = 0; i < looper->count; ++i) {
        if (looper->entries[i].fd == fd) {
            *out = looper->entries[i];
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&looper->lock);
    return found;
}

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// One wait: returns an ident, WAKE, TIMEOUT, ERROR, or CALLBACK after running callbacks.
static int poll_once(ALooper* looper, int timeout_ms, int* out_fd, int* out_events, void** out_data) {
    struct epoll_event events[16];
    int n = epoll_wait(looper->epoll_fd, events, 16, timeout_ms);
    if (n < 0) return errno == EINTR ? ALOOPER_POLL_WAKE : ALOOPER_POLL_ERROR;
    if (n == 0) return ALOOPER_POLL_TIMEOUT;
    int result = ALOOPER_POLL_WAKE;
    int ran_callback = 0;
    for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        if (fd == looper->wake_fd) {
            uint64_t value;
            while (read(looper->wake_fd, &value, sizeof value) > 0) {}
            continue;
        }
        struct looper_entry e;
        if (!find_entry(looper, fd, &e)) continue;
        int ev = from_epoll(events[i].events);
        if (e.callback) {
            if (e.callback(fd, ev, e.data) == 0) ALooper_removeFd(looper, fd);
            ran_callback = 1;
        } else if (result == ALOOPER_POLL_WAKE) {
            if (out_fd) *out_fd = fd;
            if (out_events) *out_events = ev;
            if (out_data) *out_data = e.data;
            result = e.ident;
        }
    }
    if (result == ALOOPER_POLL_WAKE && ran_callback) return ALOOPER_POLL_CALLBACK;
    return result;
}

int ALooper_pollOnce(int timeoutMillis, int* outFd, int* outEvents, void** outData) {
    ALooper* looper = ALooper_forThread();
    if (!looper) return ALOOPER_POLL_ERROR;
    return poll_once(looper, timeoutMillis, outFd, outEvents, outData);
}

int ALooper_pollAll(int timeoutMillis, int* outFd, int* outEvents, void** outData) {
    ALooper* looper = ALooper_forThread();
    if (!looper) return ALOOPER_POLL_ERROR;
    int64_t deadline = timeoutMillis > 0 ? now_ms() + timeoutMillis : 0;
    for (;;) {
        int left = timeoutMillis;
        if (timeoutMillis > 0) {
            int64_t l = deadline - now_ms();
            left = l > 0 ? (int)l : 0;
        }
        int r = poll_once(looper, left, outFd, outEvents, outData);
        if (r != ALOOPER_POLL_CALLBACK) return r;
        if (timeoutMillis == 0) return ALOOPER_POLL_CALLBACK;
        if (timeoutMillis > 0 && now_ms() >= deadline) return ALOOPER_POLL_TIMEOUT;
    }
}

// --- sensors: none -------------------------------------------------------------------------

typedef struct ASensorManager { int unused; } ASensorManager;
typedef struct ASensorEventQueue { int unused; } ASensorEventQueue;
typedef struct ASensor ASensor;
typedef ASensor const* ASensorRef;
typedef ASensorRef const* ASensorList;

static ASensorManager g_sensor_manager;
static ASensorRef g_no_sensors[1] = {NULL};

ASensorManager* ASensorManager_getInstance(void) { return &g_sensor_manager; }
ASensorManager* ASensorManager_getInstanceForPackage(const char* packageName) { return &g_sensor_manager; }

int ASensorManager_getSensorList(ASensorManager* manager, ASensorList* list) {
    if (list) *list = g_no_sensors;
    return 0;
}

ASensorRef ASensorManager_getDefaultSensor(ASensorManager* manager, int type) { return NULL; }
ASensorRef ASensorManager_getDefaultSensorEx(ASensorManager* manager, int type, int wakeUp) { return NULL; }

ASensorEventQueue* ASensorManager_createEventQueue(ASensorManager* manager, ALooper* looper, int ident,
                                                   ALooper_callbackFunc callback, void* data) {
    return calloc(1, sizeof(ASensorEventQueue));
}

int ASensorManager_destroyEventQueue(ASensorManager* manager, ASensorEventQueue* queue) {
    free(queue);
    return 0;
}

int ASensorEventQueue_registerSensor(ASensorEventQueue* queue, ASensor const* sensor, int32_t samplingPeriodUs,
                                     int64_t maxBatchReportLatencyUs) {
    return -EINVAL;
}
int ASensorEventQueue_enableSensor(ASensorEventQueue* queue, ASensor const* sensor) { return -EINVAL; }
int ASensorEventQueue_disableSensor(ASensorEventQueue* queue, ASensor const* sensor) { return 0; }
int ASensorEventQueue_setEventRate(ASensorEventQueue* queue, ASensor const* sensor, int32_t usec) { return -EINVAL; }
int ASensorEventQueue_hasEvents(ASensorEventQueue* queue) { return 0; }
ssize_t ASensorEventQueue_getEvents(ASensorEventQueue* queue, void* events, size_t count) { return 0; }
const char* ASensor_getName(ASensor const* sensor) { return ""; }
const char* ASensor_getVendor(ASensor const* sensor) { return ""; }
int ASensor_getType(ASensor const* sensor) { return -1; }
float ASensor_getResolution(ASensor const* sensor) { return 0; }
int ASensor_getMinDelay(ASensor const* sensor) { return 0; }
int ASensor_getFifoMaxEventCount(ASensor const* sensor) { return 0; }
int ASensor_getReportingMode(ASensor const* sensor) { return 0; }
int ASensor_isWakeUpSensor(ASensor const* sensor) { return 0; }

// --- tracing: disabled --------------------------------------------------------------------

int ATrace_isEnabled(void) { return 0; }
void ATrace_beginSection(const char* sectionName) {}
void ATrace_endSection(void) {}
void ATrace_beginAsyncSection(const char* sectionName, int32_t cookie) {}
void ATrace_endAsyncSection(const char* sectionName, int32_t cookie) {}
void ATrace_setCounter(const char* counterName, int64_t counterValue) {}

// --- hardware buffers ----------------------------------------------------------------------
// No gralloc: allocation fails as on a device out of buffer memory.
int AHardwareBuffer_allocate(const void* desc, void** out) {
    if (out) *out = NULL;
    return -ENOMEM;
}
void AHardwareBuffer_acquire(void* buffer) {}
void AHardwareBuffer_release(void* buffer) {}
void AHardwareBuffer_describe(const void* buffer, void* desc) {}

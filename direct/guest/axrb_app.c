// The guest-side launcher of an Android application under AXRB Direct.
//
// It is the process's main program (in place of app_process): the Android
// linker and bionic start it normally, then it hands the host the few guest
// facilities only guest code can provide -- the dynamic linker's dlopen family
// and pthread_create -- and calls into the host, which runs the Java side of
// the app on this thread (the app's main thread) and never returns.
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <android/dlext.h>

#include "axrb_guest.h"

struct axrb_guest_api {
    uint32_t version;
    uint32_t size;
    void* (*dlopen)(const char*, int);
    void* (*dlsym)(void*, const char*);
    char* (*dlerror)(void);
    int (*dlclose)(void*);
    void* (*android_dlopen_ext)(const char*, int, const android_dlextinfo*);
    int (*spawn_thread)(void* cookie, size_t stack_size);
    void* (*malloc)(size_t);
    void (*free)(void*);
    int (*dladdr)(const void*, Dl_info*);
};

// Host side (direct/src/app.cpp).
extern int axrb_app_main(const struct axrb_guest_api* api, int argc, char** argv);
extern void axrb_thread_main(void* cookie);
AXRB_HOST_FUNCTION(axrb_app_main, "axrb.app_main");
AXRB_HOST_FUNCTION(axrb_thread_main, "axrb.thread_main");

static void* thread_start(void* cookie) {
    axrb_thread_main(cookie);
    return NULL;
}

// Java threads: a detached pthread whose body is the host's thread entry.
static int spawn_thread(void* cookie, size_t stack_size) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, stack_size ? stack_size : 1024 * 1024);
    pthread_t thread;
    int r = pthread_create(&thread, &attr, thread_start, cookie);
    pthread_attr_destroy(&attr);
    return r;
}

// dlopen/dlsym resolve the caller's linker namespace from the return address,
// so these wrappers make the launcher (default namespace) the caller.
static void* app_dlopen(const char* path, int flags) { return dlopen(path, flags); }
static void* app_dlsym(void* handle, const char* name) { return dlsym(handle, name); }
static void* app_dlopen_ext(const char* path, int flags, const android_dlextinfo* info) {
    return android_dlopen_ext(path, flags, info);
}

int main(int argc, char** argv) {
    static const struct axrb_guest_api api = {
        .version = 1,
        .size = sizeof(struct axrb_guest_api),
        .dlopen = app_dlopen,
        .dlsym = app_dlsym,
        .dlerror = dlerror,
        .dlclose = dlclose,
        .android_dlopen_ext = app_dlopen_ext,
        .spawn_thread = spawn_thread,
        .malloc = malloc,
        .free = free,
        .dladdr = dladdr,
    };
    return axrb_app_main(&api, argc, argv);
}

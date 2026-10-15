#include <dlfcn.h>
#include <stdint.h>
#include <pthread.h>
#include <android/dlext.h>
#include <stdbool.h>
struct Service {uint64_t load,symbol,error,worker;};
static void* worker(void* context) {
    register void* x0 __asm__("x0")=context;
    register const char* x17 __asm__("x17")="native_bridge.worker";
    __asm__ volatile("svc #3" : "+r"(x0) : "r"(x17) : "memory");
    return 0;
}
static int create_worker(void* context) {
    pthread_t thread;int result=pthread_create(&thread,0,worker,context);
    if(!result)pthread_detach(thread);return result;
}
static struct android_namespace_t* game_namespace;
static void* load(const char* name){
    android_dlextinfo info={0};info.flags=ANDROID_DLEXT_USE_NAMESPACE;info.library_namespace=game_namespace;
    return android_dlopen_ext(name,RTLD_NOW|RTLD_LOCAL,&info);
}
int main(void) {
    typedef struct android_namespace_t* (*CreateNamespace)(const char*,const char*,const char*,uint64_t,const char*,struct android_namespace_t*,const void*);
    typedef bool (*LinkNamespace)(struct android_namespace_t*,struct android_namespace_t*,const char*);
    CreateNamespace create=(CreateNamespace)dlsym(RTLD_DEFAULT,"__loader_android_create_namespace");
    LinkNamespace link=(LinkNamespace)dlsym(RTLD_DEFAULT,"__loader_android_link_namespaces");
    if(!create || !link)return 2;
    game_namespace=create("axrb-arm-games","/data/local/tmp/native-arm","/data/local/tmp/native-arm",1,"/data",0,(void*)main);
    if(!game_namespace || !link(game_namespace,0,"libc.so:libm.so:libdl.so:liblog.so:libz.so:libc++.so:libstdc++.so:libmediandk.so:libnativewindow.so:libjnigraphics.so"))return 3;
    struct Service service={(uint64_t)load,(uint64_t)dlsym,(uint64_t)dlerror,(uint64_t)create_worker};
    register const struct Service* x0 __asm__("x0")=&service;
    register const char* x17 __asm__("x17")="native_bridge.ready";
    __asm__ volatile("svc #3" : "+r"(x0) : "r"(x17) : "memory");
    return 0;
}

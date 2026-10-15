// Android NativeBridge v8 entry point, using the pinned upstream ABI definition.
// The optional embedded service resolves full ARM libraries with real Bionic.
// The dependency-free fixture loader remains available for boundary tests.
#include "../../third_party/digitalis/native_bridge/native_bridge.h"
#include "arm_jni_bridge.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace android {
struct native_bridge_namespace_t {char* paths;char* permitted;uint64_t type;};
}
using namespace android;
namespace {
native_bridge_namespace_t system_namespace{nullptr,nullptr,0};
thread_local const char* last_error="No ARM bridge error";
bool initialize(const NativeBridgeRuntimeCallbacks*,const char*,const char* isa) {
    // -Xforce-nb-testing supplies the host ISA in a non-zygote developer run.
    bool ok=isa && (!strcmp(isa,"arm64") || !strcmp(isa,"x86_64"));
    if(ok)fprintf(stderr,"AXRB_NATIVE_BRIDGE_INITIALIZED arm64 on x86_64\n");
    return ok;
}
void* load(const char* path,int) {
    long handle=axrb_arm_load(path);
    if(handle<0){last_error="ARM ELF unsupported or unreadable; see runtime diagnostics";return nullptr;}
    fprintf(stderr,"AXRB_NATIVE_BRIDGE_LOAD %s\n",path);return reinterpret_cast<void*>(handle);
}
void* trampoline(void* handle,const char* name,const char* shorty,uint32_t length) {
    // Current ART passes zero for the legacy length parameter and supplies a
    // terminated shorty. Keep the v5 adapter compatible with both conventions.
    if(!length && shorty)length=static_cast<uint32_t>(strnlen(shorty,64));
    void* result=axrb_arm_trampoline(reinterpret_cast<long>(handle),name,shorty,length);
    if(!result)last_error="ARM symbol/signature missing or trampoline pool exhausted";
    else fprintf(stderr,"AXRB_NATIVE_BRIDGE_TRAMPOLINE %s %s\n",name,shorty?shorty:"JavaVM entry");
    return result;
}
bool supported(const char* path) {
    if(!path)return false;FILE* file=fopen(path,"rb");if(!file)return false;
    unsigned char header[20];size_t count=fread(header,1,sizeof header,file);fclose(file);
    return count==sizeof header && !memcmp(header,"\177ELF\2\1",6) && header[18]==183 && header[19]==0;
}
bool path_supported(const char* paths) {
    if(!paths)return false;
    for(const char* at=paths;*at;) {
        const char* end=strchr(at,':');if(!end)end=at+strlen(at);
        size_t size=end-at;
        if((size>=6 && !memcmp(end-6,"/arm64",6)) || (size>=10 && !memcmp(end-10,"/arm64-v8a",10)))return true;
        at=*end?end+1:end;
    }
    return false;
}
// app_process requires deferring the ARM application ABI view until platform JNI is registered.
const NativeBridgeRuntimeValues* environment(const char*){return nullptr;}
bool compatible(uint32_t version){return version>0 && version<=8;}
NativeBridgeSignalHandlerFn signal_handler(int){return nullptr;}
int unload(void*){last_error="ARM library unload is not implemented";return -1;}
const char* error(){return last_error;}
bool anonymous(const char*,const char*){last_error="anonymous ARM namespaces are not implemented";return false;}
native_bridge_namespace_t* create_namespace(const char*,const char* paths,const char* defaults,uint64_t type,const char* permitted,native_bridge_namespace_t*) {
    // Local dependency groups do not implement shared/inherited namespaces.
    if(type&2){last_error="shared ARM namespace unsupported";return nullptr;}
    auto* ns=static_cast<native_bridge_namespace_t*>(calloc(1,sizeof(native_bridge_namespace_t)));if(!ns)return nullptr;
    ns->type=type;ns->paths=strdup(paths&&*paths?paths:defaults?defaults:"");ns->permitted=strdup(permitted?permitted:"");
    if(!ns->paths || !ns->permitted){free(ns->paths);free(ns->permitted);free(ns);return nullptr;}
    return ns;
}
bool link_namespaces(native_bridge_namespace_t* from,native_bridge_namespace_t* to,const char* libraries) {
    // ART's standard system endpoint is accepted as a bootstrap declaration.
    // This does not expose x86 libraries to ARM: imports must still be satisfied
    // by sibling ARM images in the local group. Other links remain unsupported.
    return from && to==&system_namespace && libraries && *libraries;
}
bool beneath(const char* path,const char* paths) {
    if(!paths)return false;
    for(const char* at=paths;*at;) {
        const char* end=strchr(at,':');if(!end)end=at+strlen(at);size_t length=end-at;
        if(length && !strncmp(path,at,length) && path[length]=='/')return true;
        at=*end?end+1:end;
    }
    return false;
}
void* load_ext(const char* path,int flags,native_bridge_namespace_t* ns) {
    if(!path || !ns){last_error="missing ARM namespace/path";return nullptr;}
    char* canonical=realpath(path,nullptr);if(!canonical){last_error="cannot resolve ARM path";return nullptr;}
    bool allowed=ns==&system_namespace || !(ns->type&1) || beneath(canonical,ns->paths) || beneath(canonical,ns->permitted);
    void* result=allowed?load(canonical,flags):nullptr;free(canonical);
    if(!allowed)last_error="ARM library outside permitted namespace paths";
    return result;
}
native_bridge_namespace_t* vendor(){return nullptr;}
native_bridge_namespace_t* exported(const char* name) {
    return name && (!strcmp(name,"system") || !strcmp(name,"default"))?&system_namespace:nullptr;
}
void pre_fork(){fprintf(stderr,"AXRB NativeBridge: fork is not supported by the userspace kernel\n");}
void* trampoline_typed(void* handle,const char* name,const char* shorty,uint32_t length,JNICallType kind) {
    if(kind!=kJNICallTypeRegular){last_error="CriticalNative ARM calls are not supported";return nullptr;}
    return trampoline(handle,name,shorty,length);
}
void* pointer_trampoline(const void* method,const char* shorty,uint32_t length,JNICallType kind) {
    if(kind!=kJNICallTypeRegular){last_error="CriticalNative ARM calls are not supported";return nullptr;}
    if(!length && shorty)length=static_cast<uint32_t>(strnlen(shorty,64));
    return axrb_arm_pointer(method,shorty,length);
}
bool native_pointer(const void* method){return axrb_arm_is_function(method)!=0;}
}
extern "C" {
__attribute__((visibility("default"))) NativeBridgeCallbacks NativeBridgeItf={
    8,initialize,load,trampoline,supported,environment,compatible,signal_handler,unload,error,
    path_supported,anonymous,create_namespace,link_namespaces,load_ext,vendor,exported,
    pre_fork,trampoline_typed,pointer_trampoline,native_pointer
};
}

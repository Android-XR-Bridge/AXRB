#pragma once

#include "kernel.h"

#include <cstdint>
#include <string>

namespace arun::vm {
class Runtime;
class Thread;
struct Object;
struct RuntimeOptions;
struct ClassLoaderRt;
}  // namespace arun::vm

namespace axrb {

// Guest facilities the launcher (direct/guest/axrb_app.c) provides.
struct GuestApi {
    uint32_t version;
    uint32_t size;
    uint64_t dlopen, dlsym, dlerror, dlclose, android_dlopen_ext, spawn_thread, malloc, free, dladdr;
};
GuestApi& guest_api();

namespace jni {

// Hooks the managed runtime needs before it is created (guest natives, JNI
// global references as GC roots).
void configure(arun::vm::RuntimeOptions& options);
// After creation: the JNI function tables and the natives that load libraries.
void install(arun::vm::Runtime* runtime);
void set_app_loader(arun::vm::ClassLoaderRt* loader);

// JNIEnv* of the calling guest thread (attaching it to the VM if needed) and
// the process's JavaVM*.
uint64_t current_env();
uint64_t java_vm();

// Local references for host code that hands objects to guest code.
uint64_t local_ref(arun::vm::Thread* self, arun::vm::Object* object);
uint64_t global_ref(arun::vm::Object* object);
arun::vm::Object* decode(arun::vm::Thread* self, uint64_t ref);

// For host code serving guest native APIs (ANativeWindow_fromSurface...): reads
// an instance field of the object a guest JNI reference names, and keeps
// objects alive across calls as JNI global references.
bool object_field(uint64_t ref, const char* name, const char* type, uint64_t* raw);
uint64_t retain(uint64_t ref);
void release(uint64_t global);

// dlopen + JNI_OnLoad, as Runtime.nativeLoad does. Empty string on success.
std::string load_library(arun::vm::Thread* self, const std::string& path);
// dlsym across every library loaded so far.
uint64_t find_symbol(const std::string& name);

}  // namespace jni
}  // namespace axrb

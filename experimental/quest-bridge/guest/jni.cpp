/* A Java side for a native application that barely uses one.

   JNIEnv is a pointer to a table of function pointers, and the guest calls
   through it by position. So the table is built in guest memory with a thunk
   in every slot, and a call lands here knowing which slot it came from. That
   is the same trick the thread local resolver and eglGetProcAddress use.

   Objects are opaque to the guest, so a class, a method or a string is just a
   number that means something on this side. A native OpenXR application uses
   this to hand its VM and activity to the loader, to read a path or a system
   property, and little else. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "qb_env.h"
#include <windows.h>

#include "android.h"
#include "jni_table.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <cstring>

namespace {

/* Handles start high so a mistaken one is obvious rather than plausible. */
const uint64_t kFirstHandle = 0x4a000000ull;

}  // namespace

uint64_t GuestLibc::guest_alloc(uint64_t bytes) {
    GuestCpu scratch{};
    scratch.x[0] = bytes;
    call("malloc", scratch);
    return scratch.x[0];
}

uint64_t GuestLibc::guest_string(const std::string& text) {
    uint64_t where = guest_alloc(text.size() + 1);
    char* to = reinterpret_cast<char*>(guest_ptr(image->mem, where, text.size() + 1));
    if (to) std::memcpy(to, text.c_str(), text.size() + 1);
    return where;
}

uint64_t GuestLibc::handle_for(const std::string& kind, const std::string& what) {
    std::string key = kind + ":" + what;
    auto found = handle_by_name.find(key);
    if (found != handle_by_name.end()) return found->second;
    uint64_t handle = kFirstHandle + (uint64_t)handle_names.size() * 8;
    handle_by_name[key] = handle;
    handle_names[handle] = key;
    return handle;
}

void GuestLibc::start_java() {
    /* The two tables, each preceded by the word that points at it, because
       JNIEnv and JavaVM are pointers to pointers to their tables. */
    uint64_t env_table = guest_alloc(sizeof(kJniEnvSlots) / sizeof(kJniEnvSlots[0]) * 8);
    for (size_t i = 0; i < sizeof(kJniEnvSlots) / sizeof(kJniEnvSlots[0]); ++i) {
        uint64_t thunk = image->linker.thunk_for(std::string("jni.") + kJniEnvSlots[i]);
        uint8_t* slot = guest_ptr(image->mem, env_table + i * 8, 8);
        if (slot) std::memcpy(slot, &thunk, 8);
    }
    env_va = guest_alloc(8);
    uint8_t* p = guest_ptr(image->mem, env_va, 8);
    if (p) std::memcpy(p, &env_table, 8);

    uint64_t vm_table = guest_alloc(sizeof(kJavaVmSlots) / sizeof(kJavaVmSlots[0]) * 8);
    for (size_t i = 0; i < sizeof(kJavaVmSlots) / sizeof(kJavaVmSlots[0]); ++i) {
        uint64_t thunk = image->linker.thunk_for(std::string("jvm.") + kJavaVmSlots[i]);
        uint8_t* slot = guest_ptr(image->mem, vm_table + i * 8, 8);
        if (slot) std::memcpy(slot, &thunk, 8);
    }
    vm_va = guest_alloc(8);
    p = guest_ptr(image->mem, vm_va, 8);
    if (p) std::memcpy(p, &vm_table, 8);
}

bool GuestLibc::java_call(const std::string& name, GuestCpu& cpu) {
    GuestMem& mem = image->mem;
    auto arg = [&](int n) { return cpu.x[n]; };
    /* A float or double method returns in v0, not x0. Handlers give the
       number itself (90 for 90.0), which lands in whichever the caller reads. */
    const bool float_result = name.find("CallFloat") != std::string::npos ||
                              name.find("CallStaticFloat") != std::string::npos ||
                              name.find("CallNonvirtualFloat") != std::string::npos;
    const bool double_result = name.find("CallDouble") != std::string::npos ||
                               name.find("CallStaticDouble") != std::string::npos ||
                               name.find("CallNonvirtualDouble") != std::string::npos;
    auto ret = [&](uint64_t value) {
        cpu.x[0] = value;
        if (float_result) {
            float f = (float)(int64_t)value;
            uint32_t bits = 0;
            std::memcpy(&bits, &f, 4);
            cpu.q[0].lo = bits;
            cpu.q[0].hi = 0;
        } else if (double_result) {
            double d = (double)(int64_t)value;
            std::memcpy(&cpu.q[0].lo, &d, 8);
            cpu.q[0].hi = 0;
        }
    };
    auto text_at = [&](uint64_t va) -> std::string {
        const char* p = reinterpret_cast<const char*>(guest_ptr(mem, va, 1));
        return p ? std::string(p) : std::string();
    };

    /* The virtual machine side. */
    if (name == "jvm.GetEnv" || name == "jvm.AttachCurrentThread" || name == "jvm.AttachCurrentThreadAsDaemon") {
        uint8_t* out = guest_ptr(mem, arg(1), 8);
        if (out) std::memcpy(out, &env_va, 8);
        ret(0); /* JNI_OK */
        return true;
    }
    if (name == "jvm.DetachCurrentThread" || name == "jvm.DestroyJavaVM") {
        ret(0);
        return true;
    }

    if (name.compare(0, 4, "jni.") != 0) return false;
    std::string which = name.substr(4);
    static const bool trace_all = QB_ENV("QB_TRACE_JNI") != nullptr;
    if (trace_all) {
        auto described = [&](uint64_t h) {
            auto found = handle_names.find(h);
            return found == handle_names.end() ? std::string("-") : found->second;
        };
        std::printf("jni: t%llx %s %s %s %s\n", (unsigned long long)cpu.tpidr, which.c_str(), described(arg(1)).c_str(),
                    described(arg(2)).c_str(), described(arg(3)).c_str());
    }

    if (which == "GetVersion") {
        ret(0x00010006); /* JNI_VERSION_1_6 */
        return true;
    }
    if (which == "FindClass") {
        std::string wanted = text_at(arg(1));
        if (QB_ENV("QB_TRACE")) {
            if (!java_wanted.count("class " + wanted)) std::printf("java: class %s\n", wanted.c_str());
            java_wanted.insert("class " + wanted);
            std::fflush(stdout);
        }
        ret(handle_for("class", wanted));
        return true;
    }
    if (which == "GetObjectClass") {
        /* Whatever it is, it is of its own class as far as we are concerned. */
        auto found = handle_names.find(arg(1));
        ret(handle_for("class", found == handle_names.end() ? "java/lang/Object" : found->second));
        return true;
    }
    if (which == "GetMethodID" || which == "GetStaticMethodID") {
        std::string owner = handle_names.count(arg(1)) ? handle_names[arg(1)] : std::string("?");
        std::string what = owner + " " + text_at(arg(2)) + " " + text_at(arg(3));
        if (QB_ENV("QB_TRACE")) {
            if (!java_wanted.count(what)) std::printf("java: %s%s\n", which == "GetStaticMethodID" ? "static " : "",
                                                     what.c_str());
            java_wanted.insert(what);
            std::fflush(stdout);
        }
        ret(handle_for("method", text_at(arg(2)) + text_at(arg(3))));
        return true;
    }
    if (which == "GetFieldID" || which == "GetStaticFieldID") {
        /* Name and signature kept apart, so a field can be looked up by name. */
        ret(handle_for("field", text_at(arg(2)) + "|" + text_at(arg(3))));
        return true;
    }
    if (which == "NewStringUTF") {
        std::string value = text_at(arg(1));
        uint64_t handle = handle_for("string", value);
        strings_by_handle[handle] = value;
        ret(handle);
        return true;
    }
    if (which == "GetStringUTFChars") {
        auto found = strings_by_handle.find(arg(1));
        std::string value = found == strings_by_handle.end() ? std::string() : found->second;
        if (arg(2)) {
            uint8_t* copied = guest_ptr(mem, arg(2), 1);
            if (copied) *copied = 1; /* isCopy */
        }
        ret(guest_string(value));
        return true;
    }
    if (which == "NewString") {
        /* UTF-16 in: kept as UTF-8, the way every other string here is. */
        const uint16_t* chars = reinterpret_cast<const uint16_t*>(guest_ptr(mem, arg(1), 2));
        std::string value;
        for (uint64_t i = 0; chars && i < arg(2); ++i) {
            uint32_t c = chars[i];
            if (c < 0x80) value += (char)c;
            else if (c < 0x800) value += (char)(0xc0 | (c >> 6)), value += (char)(0x80 | (c & 0x3f));
            else value += (char)(0xe0 | (c >> 12)), value += (char)(0x80 | ((c >> 6) & 0x3f)),
                 value += (char)(0x80 | (c & 0x3f));
        }
        uint64_t handle = handle_for("string", value);
        strings_by_handle[handle] = value;
        ret(handle);
        return true;
    }
    if (which == "GetStringChars" || which == "GetStringCritical") {
        auto found = strings_by_handle.find(arg(1));
        std::string value = found == strings_by_handle.end() ? std::string() : found->second;
        std::vector<uint16_t> wide;
        for (size_t i = 0; i < value.size();) {
            unsigned char c = (unsigned char)value[i];
            uint32_t point = c;
            int extra = c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
            if (extra) point = c & (extra == 2 ? 0x0f : 0x1f);
            for (int k = 1; k <= extra && i + k < value.size(); ++k) point = (point << 6) | (value[i + k] & 0x3f);
            wide.push_back((uint16_t)point);
            i += 1 + extra;
        }
        wide.push_back(0);
        uint64_t va = guest_alloc(wide.size() * 2);
        uint8_t* p = guest_ptr(mem, va, wide.size() * 2);
        if (p) std::memcpy(p, wide.data(), wide.size() * 2);
        if (arg(2)) {
            uint8_t* copied = guest_ptr(mem, arg(2), 1);
            if (copied) *copied = 1;
        }
        ret(va);
        return true;
    }
    if (which == "ReleaseStringChars" || which == "ReleaseStringCritical") {
        GuestCpu freeing = cpu;
        freeing.x[0] = arg(2);
        call("free", freeing);
        ret(0);
        return true;
    }
    if (which == "GetStringUTFLength" || which == "GetStringLength") {
        auto found = strings_by_handle.find(arg(1));
        ret(found == strings_by_handle.end() ? 0 : found->second.size());
        return true;
    }
    if (which == "ReleaseStringUTFChars") {
        GuestCpu freeing = cpu;
        freeing.x[0] = arg(2);
        call("free", freeing);
        ret(0);
        return true;
    }
    if (which == "NewGlobalRef" || which == "NewLocalRef" || which == "NewWeakGlobalRef") {
        ret(arg(1));
        return true;
    }
    if (which == "DeleteGlobalRef" || which == "DeleteLocalRef" || which == "DeleteWeakGlobalRef" ||
        which == "PushLocalFrame" || which == "EnsureLocalCapacity") {
        ret(0);
        return true;
    }
    if (which == "PopLocalFrame") {
        ret(arg(1));
        return true;
    }
    if (which.compare(0, 9, "NewObject") == 0 || which == "AllocObject") {
        /* An instance of the class it was asked for, as far as anyone here
           can tell. */
        auto owner = handle_names.find(arg(1));
        std::string cls = owner == handle_names.end() ? std::string("?") : owner->second;
        if (QB_ENV("QB_TRACE")) {
            std::string what = "new " + cls;
            if (!java_wanted.count(what)) std::printf("java: %s\n", what.c_str());
            java_wanted.insert(what);
        }
        /* new String(byte[] [, charset]): a string with those bytes (UTF-8 is
           all anything here asks for). */
        auto constructor = handle_names.find(arg(2));
        if (constructor != handle_names.end() && constructor->second.find("<init>([B") != std::string::npos) {
            auto bytes = primitive_arrays.find(java_argument(which, cpu, 0));
            std::string value = bytes == primitive_arrays.end()
                                    ? std::string()
                                    : std::string(bytes->second.begin(), bytes->second.end());
            size_t end = value.find('\0');
            if (end != std::string::npos) value.resize(end);
            uint64_t handle = handle_for("string", value);
            strings_by_handle[handle] = value;
            ret(handle);
            return true;
        }
        uint64_t made = handle_for("object", cls + "#" + std::to_string(++objects_made));
        if (cls.find("java/util/Scanner") != std::string::npos) scanner_streams[made] = java_argument(which, cpu, 0);
        if (cls.find("android/os/Handler") != std::string::npos && cls.find("Callback") == std::string::npos) {
            /* Handler(Looper, Callback): the callback is the second argument. */
            uint64_t callback = java_argument(which, cpu, 1);
            if (proxy_pointers.count(callback)) handler_callbacks[made] = callback;
            else if (proxy_pointers.count(java_argument(which, cpu, 0)))
                handler_callbacks[made] = java_argument(which, cpu, 0); /* Handler(Callback) */
        }
        ret(made);
        return true;
    }
    /* Arrays. An object array is a list of handles; a primitive array is its
       bytes, handed to the guest as a copy it gives back on release. */
    if (which == "NewObjectArray") {
        uint64_t handle = handle_for("array", std::to_string(++objects_made));
        object_arrays[handle].assign((size_t)arg(1), arg(3));
        ret(handle);
        return true;
    }
    if (which == "GetArrayLength") {
        auto objects = object_arrays.find(arg(1));
        auto bytes = primitive_arrays.find(arg(1));
        if (objects != object_arrays.end()) ret(objects->second.size());
        else if (bytes != primitive_arrays.end()) {
            /* An array made without its element size recorded counts bytes. */
            size_t element = primitive_sizes.count(arg(1)) && primitive_sizes[arg(1)] ? primitive_sizes[arg(1)] : 1;
            ret(bytes->second.size() / element);
        }
        else ret(0);
        return true;
    }
    if (which == "GetObjectArrayElement") {
        auto objects = object_arrays.find(arg(1));
        ret(objects != object_arrays.end() && arg(2) < objects->second.size() ? objects->second[(size_t)arg(2)] : 0);
        return true;
    }
    if (which == "SetObjectArrayElement") {
        auto objects = object_arrays.find(arg(1));
        if (objects != object_arrays.end() && arg(2) < objects->second.size())
            objects->second[(size_t)arg(2)] = arg(3);
        ret(0);
        return true;
    }
    static const struct {
        const char* type;
        int size;
    } kPrimitive[] = {{"Boolean", 1}, {"Byte", 1}, {"Char", 2}, {"Short", 2},
                      {"Int", 4},     {"Long", 8}, {"Float", 4}, {"Double", 8}};
    for (const auto& primitive : kPrimitive) {
        std::string type = primitive.type;
        if (which == "New" + type + "Array") {
            uint64_t handle = handle_for("array", std::to_string(++objects_made));
            primitive_arrays[handle].assign((size_t)(arg(1) * primitive.size), 0);
            primitive_sizes[handle] = primitive.size;
            ret(handle);
            return true;
        }
        if (which == "Get" + type + "ArrayElements") {
            auto& bytes = primitive_arrays[arg(1)];
            uint64_t va = guest_alloc(bytes.size() + 16);
            uint8_t* p = guest_ptr(mem, va, bytes.size() + 1);
            if (p && !bytes.empty()) std::memcpy(p, bytes.data(), bytes.size());
            if (arg(2)) {
                uint8_t* copied = guest_ptr(mem, arg(2), 1);
                if (copied) *copied = 1;
            }
            ret(va);
            return true;
        }
        if (which == "Release" + type + "ArrayElements") {
            auto& bytes = primitive_arrays[arg(1)];
            uint8_t* p = guest_ptr(mem, arg(2), bytes.size() + 1);
            if (p && !bytes.empty() && arg(3) != 2 /* JNI_ABORT */) std::memcpy(bytes.data(), p, bytes.size());
            if (arg(3) != 1 /* JNI_COMMIT */) {
                GuestCpu freeing = cpu;
                freeing.x[0] = arg(2);
                call("free", freeing);
            }
            ret(0);
            return true;
        }
        if (which == "Get" + type + "ArrayRegion" || which == "Set" + type + "ArrayRegion") {
            auto& bytes = primitive_arrays[arg(1)];
            uint64_t start = arg(2) * primitive.size, count = arg(3) * primitive.size;
            uint8_t* p = guest_ptr(mem, arg(4), count);
            if (p && start + count <= bytes.size()) {
                if (which[0] == 'G') std::memcpy(p, bytes.data() + start, (size_t)count);
                else std::memcpy(bytes.data() + start, p, (size_t)count);
            }
            ret(0);
            return true;
        }
    }
    if (which == "ToReflectedMethod" || which == "ToReflectedField") {
        /* (env, class, methodID, isStatic): the Method object for that ID. It
           is the same object a proxy's invoke is handed for a method of that
           name, so JNIBridge can match the two. */
        if (reflect_methods.count(arg(2))) {
            ret(arg(2));
            return true;
        }
        auto found = handle_names.find(arg(2));
        std::string id = found == handle_names.end() ? std::string() : found->second;
        if (id.compare(0, 7, "method:") == 0) id = id.substr(7);
        std::string method_name = id.substr(0, id.find('('));
        uint64_t reflected = handle_for("object", "reflect-method:" + method_name);
        reflect_methods[reflected] = method_name;
        reflected_ids[reflected] = arg(2);
        if (QB_ENV("QB_TRACE")) std::printf("java: reflected %s\n", method_name.c_str());
        ret(reflected);
        return true;
    }
    if (which == "FromReflectedMethod" || which == "FromReflectedField") {
        auto found = reflected_ids.find(arg(1));
        if (found != reflected_ids.end()) {
            ret(found->second);
            return true;
        }
        /* A Method object made here for a proxy call: its ID is whichever
           method of that name the guest looked up with GetMethodID, which is
           what the proxy compares it against. */
        auto named = reflect_methods.find(arg(1));
        uint64_t id = 0;
        /* The interface methods proxies are called with have one signature
           each, so the ID is the one GetMethodID gives for it. */
        if (named != reflect_methods.end()) {
            const std::string& n = named->second;
            const char* signature = n == "handleMessage" ? "(Landroid/os/Message;)Z"
                                    : n == "doFrame"     ? "(J)V"
                                    : n == "run"         ? "()V"
                                                         : nullptr;
            if (signature) id = handle_for("method", n + signature);
        }
        if (!id && named != reflect_methods.end()) {
            std::string prefix = "method:" + named->second + "(";
            for (const auto& entry : handle_names)
                if (entry.second.compare(0, prefix.size(), prefix) == 0) {
                    id = entry.first;
                    break;
                }
            if (id) reflected_ids[arg(1)] = id;
        }
        if (QB_ENV("QB_TRACE"))
            std::printf("java: from reflected %s -> %s\n", named == reflect_methods.end() ? "?" : named->second.c_str(),
                        id ? handle_names[id].c_str() : "none");
        ret(id ? id : arg(1));
        return true;
    }
    if (which == "IsInstanceOf" || which == "IsAssignableFrom") {
        ret(1);
        return true;
    }
    if (which == "GetSuperclass") {
        ret(handle_for("class", "java/lang/Object"));
        return true;
    }
    if (which == "IsSameObject") {
        ret(arg(1) == arg(2) ? 1 : 0);
        return true;
    }
    /* An exception this side has "thrown" stays pending until cleared, as
       in the VM, so ExceptionCheck after a failed call tells the truth. */
    if (which == "ExceptionCheck") {
        ret(pending_exception ? 1 : 0);
        return true;
    }
    if (which == "ExceptionOccurred") {
        ret(pending_exception);
        return true;
    }
    if (which == "ExceptionClear") {
        pending_exception = 0;
        ret(0);
        return true;
    }
    if (which == "ExceptionDescribe" || which == "FatalError") {
        ret(0);
        return true;
    }
    if (which == "GetJavaVM") {
        uint8_t* out = guest_ptr(mem, arg(1), 8);
        if (out) std::memcpy(out, &vm_va, 8);
        ret(0);
        return true;
    }
    if (which == "RegisterNatives") {
        /* The guest is handing us its own functions: name, signature and
           address for each. These are how the Java side calls into the engine,
           so they are kept, keyed by class and name, to be called later. */
        std::string owner = handle_names.count(arg(1)) ? handle_names[arg(1)] : std::string("?");
        if (owner.compare(0, 6, "class:") == 0) owner = owner.substr(6);
        uint64_t table = arg(2);
        uint64_t count = arg(3);
        for (uint64_t i = 0; i < count; ++i) {
            uint8_t* entry = guest_ptr(mem, table + i * 24, 24);
            if (!entry) break;
            uint64_t name_va = 0, signature_va = 0, function = 0;
            std::memcpy(&name_va, entry, 8);
            std::memcpy(&signature_va, entry + 8, 8);
            std::memcpy(&function, entry + 16, 8);
            std::string method = text_at(name_va);
            std::string signature = text_at(signature_va);
            natives[owner + "." + method] = function;
            native_signatures[owner + "." + method] = signature;
            if (QB_ENV("QB_TRACE"))
                std::printf("native: %s.%s %s -> %llx\n", owner.c_str(), method.c_str(), signature.c_str(),
                            (unsigned long long)function);
        }
        registered_natives += (int)count;
        std::fflush(stdout);
        ret(0);
        return true;
    }
    if (which == "UnregisterNatives") {
        ret(0);
        return true;
    }

    /* Calling a method. Nothing real is behind these, so an object comes back
       as a handle and a number comes back as zero, which is what a native
       application that only asks for paths and properties can live with. */
    if (which.compare(0, 4, "Call") == 0) {
        if (QB_ENV("QB_TRACE")) {
            /* Static calls name the method in x2 as well; either way it is the
               third argument. */
            auto method = handle_names.find(arg(2));
            std::string what = which + " " + (method == handle_names.end() ? std::string("?") : method->second);
            if (!java_wanted.count(what)) std::printf("java: call %s\n", what.c_str());
            java_wanted.insert(what);
            std::fflush(stdout);
        }
        auto found = handle_names.find(arg(2));
        std::string method = found == handle_names.end() ? std::string() : found->second;
        if (method.compare(0, 7, "method:") == 0) method = method.substr(7);
        std::string method_name = method.substr(0, method.find('('));
        auto first_argument = [&]() -> uint64_t { return java_argument(which, cpu, 0); };
        auto argument = [&](int n) -> uint64_t { return java_argument(which, cpu, n); };
        const char* package = guest_package();
        std::string data_dir = std::string("/data/data/") + package;
        std::string apk = std::string("/data/app/") + package + "/base.apk";
        auto string_result = [&](const std::string& value) {
            uint64_t handle = handle_for("string", value);
            strings_by_handle[handle] = value;
            ret(handle);
        };
        auto file_result = [&](const std::string& path) {
            uint64_t handle = handle_for("object", "file:" + path);
            file_paths[handle] = path;
            ret(handle);
        };
        if (method_name == "getPackageCodePath" || method_name == "getPackageResourcePath") string_result(apk);
        else if (method_name == "getPackageName") string_result(package);
        else if (method_name == "getExternalStorageState") string_result("mounted");
        else if (method_name == "getFilesDir") file_result(data_dir + "/files");
        else if (method_name == "getCacheDir") file_result(data_dir + "/cache");
        else if (method_name == "getExternalFilesDir")
            file_result(std::string("/sdcard/Android/data/") + package + "/files");
        else if (method_name == "getExternalCacheDir")
            file_result(std::string("/sdcard/Android/data/") + package + "/cache");
        else if (method_name == "getObbDir") file_result(std::string("/sdcard/Android/obb/") + package);
        else if (method_name == "getExternalStorageDirectory") file_result("/sdcard");
        /* Unreal's GameActivity.AndroidThunkJava_InitHMDs: the Java side
           answers by calling back into the engine's nativeInitHMDs, which
           InitHMDs() waits for. */
        /* A Quest's GameActivity says it is an Oculus mobile app, which is
           what makes the Oculus HMD module load libOVRPlugin. */
        else if (method_name == "AndroidThunkJava_IsOculusMobileApplication") ret(1);
        else if (method_name == "AndroidThunkJava_InitHMDs") {
            uint64_t native = image->linker.lookup("Java_com_epicgames_ue4_GameActivity_nativeInitHMDs");
            if (native) call_guest_on(cpu, native, env_va, arg(1));
            ret(0);
        }
        /* Unreal's GameActivity.AndroidThunkJava_GetMetaData*: a few keys it
           answers from the device itself, the rest from the manifest's
           <meta-data>, extracted to QB_ROOT/apk/meta-data.txt (key=value). */
        else if (method_name.compare(0, 28, "AndroidThunkJava_GetMetaData") == 0) {
            auto key_found = strings_by_handle.find(first_argument());
            std::string key = key_found == strings_by_handle.end() ? std::string() : key_found->second;
            static const std::unordered_map<std::string, std::string> meta = [] {
                std::unordered_map<std::string, std::string> all;
                const char* root = QB_ENV("QB_ROOT");
                if (FILE* in = std::fopen((std::string(root ? root : ".") + "/apk/meta-data.txt").c_str(), "rb")) {
                    char line[1024];
                    while (std::fgets(line, sizeof(line), in)) {
                        std::string text = line;
                        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
                        size_t eq = text.find('=');
                        if (eq != std::string::npos) all[text.substr(0, eq)] = text.substr(eq + 1);
                    }
                    std::fclose(in);
                }
                return all;
            }();
            std::string value;
            bool known = true;
            if (key == "ue4.displaymetrics.dpi") value = "500.0,500.0"; /* xdpi,ydpi */
            else if (key == "ue4.displaymetrics.densitydpi") value = "480";
            /* AudioManager.getProperty, as the Java side forwards them. */
            else if (key == "audiomanager.framesPerBuffer") value = "192";
            else if (key == "audiomanager.optimalSampleRate") value = "48000";
            /* The engine's own memory in use, in bytes (GameActivity reads Debug.MemoryInfo). */
            else if (key == "ue4.getUsedMemory") value = "1073741824";
            else if (meta.count(key)) value = meta.at(key);
            else known = false;
            if (QB_ENV("QB_TRACE_JNI") || QB_ENV("QB_TRACE_META"))
                std::printf("java: %s(%s) -> %s\n", method_name.c_str(), key.c_str(), known ? value.c_str() : "(none)");
            if (method_name == "AndroidThunkJava_GetMetaDataString") {
                if (known) string_result(value);
                else ret(0);
            } else if (method_name == "AndroidThunkJava_GetMetaDataFloat") {
                float f = known ? std::strtof(value.c_str(), nullptr) : 0.0f;
                uint32_t bits;
                std::memcpy(&bits, &f, 4);
                cpu.q[0].lo = bits; /* a float comes back in s0 */
                cpu.q[0].hi = 0;
            } else if (method_name == "AndroidThunkJava_GetMetaDataBoolean") {
                ret(known && (value == "true" || value == "1") ? 1 : 0);
            } else {
                ret(known ? (uint64_t)std::strtoll(value.c_str(), nullptr, 0) : 0);
            }
        }
        else if (method_name == "getDataDirectory") file_result("/data");
        else if (method_name == "getPath" || method_name == "getAbsolutePath" || method_name == "getCanonicalPath") {
            auto path = file_paths.find(arg(1));
            if (path == file_paths.end()) ret(0);
            else string_result(path->second);
        } else if (method_name == "equals") {
            auto a = strings_by_handle.find(arg(1));
            auto b = strings_by_handle.find(first_argument());
            ret(a != strings_by_handle.end() && b != strings_by_handle.end() && a->second == b->second ? 1 : 0);
        } else if (method_name == "forName" && strings_by_handle.count(first_argument())) {
            /* Class.forName("a.b.C"): the class a/b/C, the same handle FindClass
               gives, so everything keyed on class names (Handler, Scanner, the
               proxy interfaces) recognises it. */
            std::string wanted = strings_by_handle[first_argument()];
            std::replace(wanted.begin(), wanted.end(), '.', '/');
            ret(handle_for("class", wanted));
        } else if (method_name == "findLibrary") {
            auto wanted = strings_by_handle.find(first_argument());
            std::string library = wanted == strings_by_handle.end() ? std::string("main") : wanted->second;
            /* Java's findLibrary takes a bare name, but Unity sometimes hands
               over one that already has its prefix. */
            if (library.compare(0, 3, "lib") != 0) library = "lib" + library;
            if (library.size() < 3 || library.compare(library.size() - 3, 3, ".so") != 0) library += ".so";
            string_result(std::string("/data/app/") + package + "/lib/arm64/" + library);
        } else if (method_name == "setTitle" || method_name == "setMessage") {
            /* An error dialog is the engine's last word before it gives up,
               so what it says is always worth seeing. */
            auto said = strings_by_handle.find(first_argument());
            std::printf("java: dialog %s: %s\n", method_name.c_str(),
                        said == strings_by_handle.end() ? "(not a string)" : said->second.c_str());
            std::fflush(stdout);
            ret(arg(1));
        } else if (method_name == "open" && which.find("ObjectMethod") != std::string::npos) {
            /* AssetManager.open(path): the bytes of that asset, which live in
               the APK under assets/. They are read from QB_ROOT/apk/assets. */
            auto named = strings_by_handle.find(first_argument());
            std::string path = named == strings_by_handle.end() ? std::string() : named->second;
            const char* root = QB_ENV("QB_ROOT");
            std::string host = std::string(root ? root : ".") + "/apk/assets/" + path;
            std::vector<uint8_t> bytes;
            bool found = false;
            if (FILE* file = std::fopen(host.c_str(), "rb")) {
                std::fseek(file, 0, SEEK_END);
                long size = std::ftell(file);
                std::fseek(file, 0, SEEK_SET);
                bytes.resize(size > 0 ? (size_t)size : 0);
                if (!bytes.empty()) std::fread(bytes.data(), 1, bytes.size(), file);
                std::fclose(file);
                found = true;
            }
            if (QB_ENV("QB_TRACE")) std::printf("java: asset %s -> %s\n", path.c_str(), found ? "found" : "missing");
            if (!found) {
                pending_exception = handle_for("object", "java/io/FileNotFoundException:" + path);
                ret(0);
            } else {
                uint64_t handle = handle_for("object", "stream#" + std::to_string(++objects_made));
                java_streams[handle] = JavaStream{std::move(bytes), 0};
                ret(handle);
            }
        } else if (java_streams.count(arg(1)) &&
                   (method_name == "read" || method_name == "available" || method_name == "skip" ||
                    method_name == "close" || method_name == "markSupported" || method_name == "reset")) {
            JavaStream& stream = java_streams[arg(1)];
            size_t left = stream.bytes.size() - stream.at;
            if (method_name == "available") {
                ret(left);
            } else if (method_name == "skip") {
                uint64_t count = std::min<uint64_t>(argument(0), left);
                stream.at += (size_t)count;
                ret(count);
            } else if (method_name == "close" || method_name == "markSupported") {
                ret(0);
            } else if (method_name == "reset") {
                stream.at = 0;
                ret(0);
            } else if (method.find("()I") != std::string::npos) { /* read() */
                ret(left ? stream.bytes[stream.at++] : (uint64_t)(int64_t)-1);
            } else { /* read(byte[]) and read(byte[], offset, length) */
                auto target = primitive_arrays.find(argument(0));
                if (target == primitive_arrays.end()) {
                    ret((uint64_t)(int64_t)-1);
                } else {
                    size_t offset = method.find("[BII") != std::string::npos ? (size_t)(uint32_t)argument(1) : 0;
                    size_t length = method.find("[BII") != std::string::npos ? (size_t)(uint32_t)argument(2)
                                                                              : target->second.size();
                    length = std::min(length, target->second.size() - std::min(offset, target->second.size()));
                    size_t count = std::min(length, left);
                    if (!count && length) {
                        ret((uint64_t)(int64_t)-1); /* end of stream */
                    } else {
                        std::memcpy(target->second.data() + offset, stream.bytes.data() + stream.at, count);
                        stream.at += count;
                        ret(count);
                    }
                }
            }
        } else if (scanner_streams.count(arg(1)) &&
                   (method_name == "useDelimiter" || method_name == "next" || method_name == "hasNext" ||
                    method_name == "nextLine" || method_name == "close")) {
            /* Scanner over a stream: only as used for reading a whole asset,
               which is useDelimiter("\\A").next(). */
            auto stream = java_streams.find(scanner_streams[arg(1)]);
            if (method_name == "useDelimiter") {
                ret(arg(1));
            } else if (method_name == "hasNext") {
                ret(stream != java_streams.end() && stream->second.at < stream->second.bytes.size() ? 1 : 0);
            } else if (method_name == "close") {
                ret(0);
            } else if (stream == java_streams.end() || stream->second.at >= stream->second.bytes.size()) {
                pending_exception = handle_for("object", "java/util/NoSuchElementException");
                ret(0);
            } else {
                JavaStream& from = stream->second;
                std::string rest(from.bytes.begin() + (long long)from.at, from.bytes.end());
                if (method_name == "nextLine") {
                    size_t end = rest.find('\n');
                    if (end != std::string::npos) rest.resize(end);
                    from.at += rest.size() + (end != std::string::npos ? 1 : 0);
                } else {
                    from.at = from.bytes.size();
                }
                string_result(rest);
            }
        } else if (method_name == "loadLibrary" || method_name == "load") {
            /* System.loadLibrary("x") and the helpers that wrap it load
               libx.so from the application's library directory; System.load
               takes a whole path. Either way JNI_OnLoad runs. */
            auto named = strings_by_handle.find(first_argument());
            uint64_t handle = 0;
            if (named != strings_by_handle.end()) {
                std::string library = named->second;
                /* A bare name is libname.so; Unity's own UnityPlayer.loadLibrary
                   is handed a whole path, which is used as it is. */
                if (method_name == "loadLibrary" && library.find('/') == std::string::npos)
                    library = "lib" + library + ".so";
                handle = open_library(library);
                std::printf("java: %s(%s) -> %s\n", method_name.c_str(), named->second.c_str(),
                            handle ? "loaded" : "not found");
            }
            /* The boolean forms say whether it loaded. */
            ret(handle ? 1 : 0);
        } else if (method_name == "getTrustManagers" || method_name == "getAcceptedIssuers" ||
                   (method_name == "getEncoded" && handle_names[arg(1)].compare(0, 12, "object:cert:") == 0)) {
            /* The system's trusted CAs, which TLS code (UnityTls) reads as
               TrustManagerFactory.getTrustManagers()[0].getAcceptedIssuers(),
               each getEncoded() as DER. They are the device's own, copied
               to QB_ROOT/system/etc/security/cacerts (PEM, one per file). */
            static std::vector<std::vector<uint8_t>> certificates = [] {
                std::vector<std::vector<uint8_t>> found;
                const char* root = QB_ENV("QB_ROOT");
                std::string dir = std::string(root ? root : ".") + "/system/etc/security/cacerts/";
                WIN32_FIND_DATAA entry;
                HANDLE search = FindFirstFileA((dir + "*").c_str(), &entry);
                if (search == INVALID_HANDLE_VALUE) return found;
                do {
                    if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    FILE* file = std::fopen((dir + entry.cFileName).c_str(), "rb");
                    if (!file) continue;
                    std::string text;
                    char chunk[4096];
                    for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), file)) > 0;) text.append(chunk, n);
                    std::fclose(file);
                    size_t begin = text.find("-----BEGIN CERTIFICATE-----");
                    size_t end = text.find("-----END CERTIFICATE-----");
                    if (begin == std::string::npos || end == std::string::npos) continue;
                    std::vector<uint8_t> der;
                    uint32_t bits = 0;
                    int have = 0;
                    for (size_t i = begin + 27; i < end; ++i) {
                        char c = text[i];
                        int v = c >= 'A' && c <= 'Z'   ? c - 'A'
                                : c >= 'a' && c <= 'z' ? c - 'a' + 26
                                : c >= '0' && c <= '9' ? c - '0' + 52
                                : c == '+'             ? 62
                                : c == '/'             ? 63
                                                       : -1;
                        if (v < 0) continue;
                        bits = (bits << 6) | (uint32_t)v;
                        have += 6;
                        if (have >= 8) {
                            have -= 8;
                            der.push_back((uint8_t)(bits >> have));
                        }
                    }
                    if (!der.empty()) found.push_back(std::move(der));
                } while (FindNextFileA(search, &entry));
                FindClose(search);
                std::printf("java: %zu trusted CA certificates\n", found.size());
                return found;
            }();
            if (method_name == "getTrustManagers") {
                uint64_t array = handle_for("array", std::to_string(++objects_made));
                object_arrays[array] = {handle_for("object", "X509TrustManager")};
                ret(array);
            } else if (method_name == "getAcceptedIssuers") {
                uint64_t array = handle_for("array", std::to_string(++objects_made));
                auto& list = object_arrays[array];
                for (size_t i = 0; i < certificates.size(); ++i)
                    list.push_back(handle_for("object", "cert:" + std::to_string(i)));
                ret(array);
            } else {
                size_t index = (size_t)std::strtoull(handle_names[arg(1)].c_str() + 12, nullptr, 10);
                uint64_t array = handle_for("array", std::to_string(++objects_made));
                if (index < certificates.size()) primitive_arrays[array] = certificates[index];
                primitive_sizes[array] = 1;
                ret(array);
            }
        } else if (method_name == "getBytes" && strings_by_handle.count(arg(1))) {
            /* String.getBytes(charset): the UTF-8 bytes, as a byte[]. */
            const std::string& value = strings_by_handle[arg(1)];
            uint64_t array = handle_for("array", std::to_string(++objects_made));
            primitive_arrays[array].assign(value.begin(), value.end());
            primitive_sizes[array] = 1;
            ret(array);
        } else if (method.size() > 19 && method.compare(method.size() - 19, 19, ")Ljava/lang/String;") == 0 &&
                   method.back() == ';' && which.find("ObjectMethod") != std::string::npos &&
                   method_name != "toString") {
            /* A string this side knows nothing about is null, which is what
               Java code returns for "not set" (a launch URL, an intent
               extra); a made-up object would be read as a string. */
            if (QB_ENV("QB_TRACE")) std::printf("java: %s gives null\n", method_name.c_str());
            ret(0);
        } else if (method_name == "newInterfaceProxy") {
            /* JNIBridge.newInterfaceProxy(nativePtr, interfaces): a Java object
               whose every method call goes back to native code through
               JNIBridge.invoke with that pointer. */
            uint64_t proxy = handle_for("object", "proxy#" + std::to_string(++objects_made));
            proxy_pointers[proxy] = first_argument();
            ret(proxy);
        } else if ((method_name == "runOnUiThread" || method_name == "post" || method_name == "postDelayed" ||
                    method_name == "postAtFrontOfQueue") &&
                   proxy_pointers.count(first_argument())) {
            /* Runnable.run() on the UI thread, by way of JNIBridge.invoke. */
            int delay = method_name == "postDelayed" ? (int)argument(1) : 0;
            invoke_proxy(first_argument(), "run", {}, delay < 0 ? 0 : delay);
            ret(1);
        } else if (method_name == "obtainMessage") {
            /* Handler.obtainMessage(what, ...): a Message aimed at this handler. */
            uint64_t message = handle_for("object", "message#" + std::to_string(++objects_made));
            message_what[message] = method.find("()") != std::string::npos ? 0 : (int64_t)(int32_t)first_argument();
            message_target[message] = arg(1);
            ret(message);
        } else if (method_name == "sendToTarget" && message_target.count(arg(1))) {
            auto callback = handler_callbacks.find(message_target[arg(1)]);
            if (callback != handler_callbacks.end()) {
                uint64_t args = handle_for("array", std::to_string(++objects_made));
                object_arrays[args] = {arg(1)};
                invoke_proxy(callback->second, "handleMessage", {args}, 0);
            }
            ret(0);
        } else if ((method_name == "sendMessage" || method_name == "sendMessageDelayed" ||
                    method_name == "sendEmptyMessage" || method_name == "sendEmptyMessageDelayed") &&
                   handler_callbacks.count(arg(1))) {
            uint64_t message = first_argument();
            if (method_name.find("Empty") != std::string::npos) {
                uint64_t made = handle_for("object", "message#" + std::to_string(++objects_made));
                message_what[made] = (int64_t)(int32_t)message;
                message = made;
            }
            message_target[message] = arg(1);
            int delay = method_name.find("Delayed") != std::string::npos ? (int)argument(1) : 0;
            uint64_t args = handle_for("array", std::to_string(++objects_made));
            object_arrays[args] = {message};
            invoke_proxy(handler_callbacks[arg(1)], "handleMessage", {args}, delay < 0 ? 0 : delay);
            ret(1);
        } else if ((method_name == "postFrameCallback" || method_name == "postFrameCallbackDelayed") &&
                   proxy_pointers.count(first_argument())) {
            /* Choreographer: doFrame(frameTimeNanos) at the next vsync, which
               here is the next 90 Hz tick. */
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            int64_t nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
            uint64_t boxed = handle_for("object", "long#" + std::to_string(++objects_made));
            boxed_longs[boxed] = nanos + 11111111;
            uint64_t args = handle_for("array", std::to_string(++objects_made));
            object_arrays[args] = {boxed};
            int delay = 11 + (method_name == "postFrameCallbackDelayed" ? (int)argument(1) : 0);
            invoke_proxy(first_argument(), "doFrame", {args}, delay);
            static const bool trace_vsync = QB_ENV("QB_TRACE_VSYNC") != nullptr;
            if (trace_vsync)
                std::fprintf(stderr, "vsync: %s posted by tp %llx\n", method_name.c_str(),
                             t_guest_cpu ? (unsigned long long)t_guest_cpu->tpidr : 0ull);
            ret(0);
        } else if (method_name == "equals" && !strings_by_handle.count(arg(1))) {
            ret(arg(1) == first_argument() ? 1 : 0); /* objects here are equal when they are the same */
        } else if (method_name == "hashCode") {
            ret((uint64_t)(uint32_t)(arg(1) >> 3));
        } else if (method_name == "longValue" && boxed_longs.count(arg(1))) {
            ret((uint64_t)boxed_longs[arg(1)]);
        } else if ((method_name == "getPackageInfo" || method_name == "createPackageContext" ||
                    method_name == "getApplicationInfo") &&
                   strings_by_handle.count(first_argument()) && strings_by_handle[first_argument()] != package) {
            /* Only the game itself is installed here. Asking about another
               package (the Platform SDK asks for com.oculus.platformsdkruntime,
               then Horizon) gets what a device without it gives. */
            const std::string& asked = strings_by_handle[first_argument()];
            if (QB_ENV("QB_TRACE")) std::printf("java: %s(%s) -> not installed\n", method_name.c_str(), asked.c_str());
            pending_exception =
                handle_for("object", "android/content/pm/PackageManager$NameNotFoundException:" + asked);
            ret(0);
        } else if (method_name == "getRefreshRate") {
            ret(90); /* Display.getRefreshRate(): the Quest runs its panel at 90 Hz here */
        } else if (method_name == "getInstance" && method.find("Choreographer") != std::string::npos) {
            ret(handle_for("object", "choreographer"));
        } else if (method_name == "initOculus") {
            /* OculusUnity.initOculus() makes a surface for the XR display and,
               once Android has created it, calls the plugin's
               surfaceCreated(surface) on the UI thread. The display provider's
               graphics thread waits for exactly that. */
            uint64_t created = image->linker.lookup("Java_com_unity_oculus_OculusUnity_surfaceCreated");
            if (created)
                post_to_ui(created, {env_va, handle_for("class", "com/unity/oculus/OculusUnity"),
                                     handle_for("object", "Surface")},
                           0);
            std::printf("java: initOculus -> surfaceCreated %s\n", created ? "posted" : "not found");
            ret(0);
        } else if (method_name == "toString" && reflect_methods.count(arg(1))) {
            /* Method.toString(), as Java writes it; Unity parses the name and
               parameter types out of this. */
            const std::string& which_method = reflect_methods[arg(1)];
            std::string text = which_method == "handleMessage"
                                   ? "public abstract boolean android.os.Handler$Callback.handleMessage(android.os.Message)"
                               : which_method == "doFrame"
                                   ? "public abstract void android.view.Choreographer$FrameCallback.doFrame(long)"
                                   : "public abstract void java.lang.Runnable." + which_method + "()";
            string_result(text);
        } else if (method_name == "getName" && reflect_methods.count(arg(1))) {
            string_result(reflect_methods[arg(1)]);
        } else if (method_name == "getIsOnOculusHardware") {
            /* Unity's Oculus helper answers this from Build.MANUFACTURER, so
               the same rule is applied to the device's own property. */
            std::string maker = property("ro.product.manufacturer");
            for (char& c : maker) c = (char)std::tolower((unsigned char)c);
            ret(maker.find("oculus") != std::string::npos || maker.find("meta") != std::string::npos ? 1 : 0);
        } else if (method_name == "getManifestSetting") {
            ret(0); /* optional features, off unless the manifest says otherwise */
        } else if (method_name == "playCoreApiMissing") {
            ret(1);
        } else if (which.find("ObjectMethod") != std::string::npos) {
            ret(handle_for("result", found == handle_names.end() ? "unknown" : found->second));
        } else {
            ret(0);
        }
        return true;
    }
    if (which == "GetObjectField" || which == "GetStaticObjectField") {
        auto found = handle_names.find(arg(2));
        std::string field = found == handle_names.end() ? std::string() : found->second;
        if (field.compare(0, 6, "field:") == 0) field = field.substr(6);
        field = field.substr(0, field.find('|'));
        const char* package = guest_package();
        std::string value;
        static const struct {
            const char* field;
            const char* property;
        } kBuild[] = {
            {"MANUFACTURER", "ro.product.manufacturer"}, {"MODEL", "ro.product.model"},
            {"BRAND", "ro.product.brand"},               {"DEVICE", "ro.product.device"},
            {"PRODUCT", "ro.product.name"},              {"HARDWARE", "ro.hardware"},
            {"BOARD", "ro.product.board"},               {"FINGERPRINT", "ro.build.fingerprint"},
            {"ID", "ro.build.id"},                       {"DISPLAY", "ro.build.display.id"},
            {"RELEASE", "ro.build.version.release"},     {"INCREMENTAL", "ro.build.version.incremental"},
            {"TYPE", "ro.build.type"},                   {"TAGS", "ro.build.tags"},
            {"HOST", "ro.build.host"},                   {"USER", "ro.build.user"},
            {"BOOTLOADER", "ro.bootloader"},             {"CODENAME", "ro.build.version.codename"},
        };
        for (const auto& entry : kBuild)
            if (field == entry.field) value = property(entry.property);
        if (field == "sourceDir" || field == "publicSourceDir")
            value = std::string("/data/app/") + package + "/base.apk";
        else if (field == "nativeLibraryDir") value = std::string("/data/app/") + package + "/lib/arm64";
        else if (field == "dataDir") value = std::string("/data/data/") + package;
        else if (field == "packageName") value = package;
        /* UnityPlayer.currentActivity: the one activity there is. */
        if (field == "currentActivity") {
            ret(handle_for("object", "activity"));
            return true;
        }
        if (QB_ENV("QB_TRACE")) std::printf("java: field %s\n", field.c_str());
        if (value.empty()) {
            ret(0);
        } else {
            uint64_t handle = handle_for("string", value);
            strings_by_handle[handle] = value;
            ret(handle);
        }
        return true;
    }
    if (which == "GetStaticIntField" || which == "GetIntField") {
        auto found = handle_names.find(arg(2));
        std::string field = found == handle_names.end() ? std::string() : found->second;
        if (field.compare(0, 6, "field:") == 0) field = field.substr(6);
        field = field.substr(0, field.find('|'));
        uint64_t value = 0;
        if (field == "SDK_INT") value = std::strtoull(property("ro.build.version.sdk").c_str(), nullptr, 10);
        /* DisplayMetrics: the host window a guest surface becomes. */
        else if (field == "widthPixels") value = 1280;
        else if (field == "heightPixels") value = 720;
        else if (field == "densityDpi") value = 320;
        else if (field == "what" && message_what.count(arg(1))) value = (uint64_t)message_what[arg(1)];
        if (QB_ENV("QB_TRACE")) std::printf("java: int field %s = %llu\n", field.c_str(), (unsigned long long)value);
        ret(value);
        return true;
    }
    if (which.compare(0, 3, "Get") == 0 && which.find("Field") != std::string::npos) {
        ret(0);
        return true;
    }

    std::fprintf(stderr, "jni: %s is not answered\n", which.c_str());
    ret(0);
    return true;
}

/* The n-th argument of a Call*Method or NewObject call, after the object (or
   class) and the method: in registers from x3 for the plain variadic form, in
   a va_list for the V form, or in a jvalue array for the A form. */
uint64_t GuestLibc::java_argument(const std::string& which, GuestCpu& cpu, int n) {
    GuestMem& mem = image->mem;
    auto read64 = [&](uint64_t va) {
        uint64_t value = 0;
        uint8_t* p = guest_ptr(mem, va, 8);
        if (p) std::memcpy(&value, p, 8);
        return value;
    };
    if (which.size() > 1 && which.back() == 'A') return read64(cpu.x[3] + (uint64_t)n * 8);
    if (which.size() > 1 && which.back() == 'V') {
        /* AArch64 va_list: stack, gr_top, vr_top, gr_offs, vr_offs. */
        uint64_t list = cpu.x[3];
        uint64_t stack = read64(list), gr_top = read64(list + 8);
        int32_t gr_offs = 0;
        uint8_t* p = guest_ptr(mem, list + 24, 4);
        if (p) std::memcpy(&gr_offs, p, 4);
        for (int i = 0; i < n; ++i) {
            if (gr_offs < 0) gr_offs += 8;
            else stack += 8;
        }
        return gr_offs < 0 ? read64(gr_top + (int64_t)gr_offs) : read64(stack);
    }
    if (3 + n <= 7) return cpu.x[3 + n];
    return read64(cpu.sp + (uint64_t)(3 + n - 8) * 8);
}

/* Calls a JNIBridge proxy's method on the UI thread: JNIBridge.invoke(ptr,
   Method, Object[] args), with a Method object that answers getName. */
void GuestLibc::invoke_proxy(uint64_t proxy, const std::string& method, const std::vector<uint64_t>& args,
                             int delay_ms) {
    auto invoke = natives.find("bitter/jnibridge/JNIBridge.invoke");
    auto pointer = proxy_pointers.find(proxy);
    if (invoke == natives.end() || pointer == proxy_pointers.end()) return;
    uint64_t method_object = handle_for("object", "reflect-method:" + method);
    reflect_methods[method_object] = method;
    uint64_t arguments = args.empty() ? 0 : args[0];
    if (QB_ENV("QB_TRACE")) std::printf("java: proxy %s posted\n", method.c_str());
    /* invoke(long ptr, Class iface, Method method, Object[] args), static, so
       after the env and the JNIBridge class come those four. */
    /* The interface the method belongs to: Unity's proxy dispatches on it. */
    std::string iface = method == "handleMessage" ? "android/os/Handler$Callback"
                        : method == "doFrame"     ? "android/view/Choreographer$FrameCallback"
                                                  : "java/lang/Runnable";
    post_to_ui(invoke->second,
               {env_va, handle_for("class", "bitter/jnibridge/JNIBridge"), pointer->second, handle_for("class", iface),
                method_object, arguments},
               delay_ms);
}

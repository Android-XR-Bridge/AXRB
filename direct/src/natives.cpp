// Host natives of boot-class-path classes whose state lives in the guest:
// direct byte buffers (guest memory), file descriptors (guest fds), uncompressed
// APK assets opened as descriptors, and memory figures.

#include "natives.h"

#include "files.h"
#include "kernel.h"
#include "linux.h"
#include "log.h"
#include "media.h"
#include "memory.h"
#include "vfs.h"

#include "arun/vm/heap.h"
#include "arun/vm/runtime.h"
#include "arun/vm/thread.h"
#include "arun/zip_reader.h"

#include <windows.h>

#include <cstring>
#include <map>
#include <memory>
#include <mutex>

namespace axrb {
namespace vm = arun::vm;
using namespace linux_abi;

namespace {

std::shared_ptr<arun::pkg::ZipReader> g_apk;
std::string g_guest_apk;

GuestThread& guest_self() {
    GuestThread* t = current_thread();
    if (!t) fatal("a guest-backed Java native ran on a thread that is not a guest thread");
    return *t;
}

int64_t sys(uint64_t nr, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0,
            uint64_t a5 = 0) {
    return do_syscall(guest_self(), nr, a0, a1, a2, a3, a4, a5);
}

constexpr uint64_t kStatfs = 43, kOpenat = 56, kClose = 57, kFstat = 80, kMmap = 222;
const uint64_t kCwd = static_cast<uint64_t>(static_cast<int64_t>(AT_FDCWD_));

void throw_new(vm::Thread* self, const char* descriptor, const std::string& message) {
    self->runtime()->ThrowNew(self, descriptor, message);
}

// --- java.nio.DirectByteBuffer --------------------------------------------------------

void DirectByteBuffer_nativeAlloc(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    const int32_t capacity = vm::ArgInt(a, 0);
    r->j = 0;
    if (capacity <= 0) return;
    const uint64_t size = (static_cast<uint64_t>(capacity) + 0xfff) & ~uint64_t{0xfff};
    vm::ScopedBlocking blocking(self);
    int64_t addr = sys(kMmap, 0, size, PROT_READ_ | PROT_WRITE_, MAP_PRIVATE_ | MAP_ANONYMOUS_,
                       static_cast<uint64_t>(-1), 0);
    if (addr < 0 && addr > -4096) return;
    r->j = addr;
}

void DirectByteBuffer_peek(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    uint8_t b = 0;
    if (!guest_read(static_cast<uint64_t>(vm::ArgLong(a, 0)), &b, 1)) {
        return throw_new(self, "Ljava/lang/IllegalStateException;", "direct buffer memory is not readable");
    }
    r->i = static_cast<int8_t>(b);
}

void DirectByteBuffer_poke(vm::Thread* self, vm::Frame* a, vm::JValue*) {
    const uint8_t b = static_cast<uint8_t>(vm::ArgInt(a, 2));
    if (!guest_write(static_cast<uint64_t>(vm::ArgLong(a, 0)), &b, 1)) {
        throw_new(self, "Ljava/lang/IllegalStateException;", "direct buffer memory is not writable");
    }
}

bool array_range(vm::Thread* self, vm::Array* arr, int32_t off, int32_t len) {
    if (arr == nullptr) {
        throw_new(self, "Ljava/lang/NullPointerException;", "array == null");
        return false;
    }
    if (off < 0 || len < 0 || off > arr->length - len) {
        throw_new(self, "Ljava/lang/ArrayIndexOutOfBoundsException;", "bad range");
        return false;
    }
    return true;
}

void DirectByteBuffer_peekBytes(vm::Thread* self, vm::Frame* a, vm::JValue*) {
    auto* arr = static_cast<vm::Array*>(vm::ArgRef(a, 2));
    const int32_t off = vm::ArgInt(a, 3), len = vm::ArgInt(a, 4);
    if (!array_range(self, arr, off, len)) return;
    if (!guest_read(static_cast<uint64_t>(vm::ArgLong(a, 0)), arr->data() + off, static_cast<size_t>(len))) {
        throw_new(self, "Ljava/lang/IllegalStateException;", "direct buffer memory is not readable");
    }
}

void DirectByteBuffer_pokeBytes(vm::Thread* self, vm::Frame* a, vm::JValue*) {
    auto* arr = static_cast<vm::Array*>(vm::ArgRef(a, 2));
    const int32_t off = vm::ArgInt(a, 3), len = vm::ArgInt(a, 4);
    if (!array_range(self, arr, off, len)) return;
    if (!guest_write(static_cast<uint64_t>(vm::ArgLong(a, 0)), arr->data() + off, static_cast<size_t>(len))) {
        throw_new(self, "Ljava/lang/IllegalStateException;", "direct buffer memory is not writable");
    }
}

// --- android.content.res.AssetManager.openFd ---------------------------------------------

void AssetManager_nativeOpenAssetFd(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    vm::Runtime* rt = self->runtime();
    r->l = nullptr;
    std::string name = rt->GetStringUtf8(vm::ArgRef(a, 0));
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    if (!g_apk || name.empty() || name.find("..") != std::string::npos) return;
    const arun::pkg::ZipEntry* e = g_apk->Find("assets/" + name);
    if (e == nullptr || e->is_directory) return;
    int64_t values[3] = {-1, 0, 0};
    if (e->method == arun::pkg::CompressionMethod::kStore) {
        int64_t fd;
        {
            vm::ScopedBlocking blocking(self);
            fd = sys(kOpenat, kCwd, reinterpret_cast<uint64_t>(g_guest_apk.c_str()), O_RDONLY_ | O_CLOEXEC_, 0);
        }
        if (fd < 0) return;
        values[0] = fd;
        values[1] = static_cast<int64_t>(e->data_offset);
        values[2] = static_cast<int64_t>(e->uncompressed_size);
    }
    vm::RtClass* long_array = rt->class_linker().FindClass(self, "[J", nullptr);
    if (long_array == nullptr) return;
    vm::Array* out = rt->heap().AllocArray(self, long_array, 3);
    if (out == nullptr) return;
    std::memcpy(out->data(), values, sizeof values);
    r->l = out;
}

// --- android.os.ParcelFileDescriptor ----------------------------------------------------

void ParcelFileDescriptor_nativeOpen(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    const std::string path = self->runtime()->GetStringUtf8(vm::ArgRef(a, 0));
    const uint32_t mode = static_cast<uint32_t>(vm::ArgInt(a, 1));
    uint32_t flags = O_CLOEXEC_;
    switch (mode & 0x30000000u) {
        case 0x20000000u: flags |= O_WRONLY_; break;
        case 0x30000000u: flags |= O_RDWR_; break;
        default: flags |= O_RDONLY_; break;
    }
    if (mode & 0x08000000u) flags |= O_CREAT_;
    if (mode & 0x04000000u) flags |= O_TRUNC_;
    if (mode & 0x02000000u) flags |= O_APPEND_;
    vm::ScopedBlocking blocking(self);
    r->i = static_cast<int32_t>(sys(kOpenat, kCwd, reinterpret_cast<uint64_t>(path.c_str()), flags, 0660));
}

void ParcelFileDescriptor_nativeStatSize(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    kstat st{};
    vm::ScopedBlocking blocking(self);
    r->j = sys(kFstat, static_cast<uint64_t>(vm::ArgInt(a, 0)), reinterpret_cast<uint64_t>(&st)) == 0 ? st.st_size : -1;
}

void ParcelFileDescriptor_nativeClose(vm::Thread* self, vm::Frame* a, vm::JValue*) {
    vm::ScopedBlocking blocking(self);
    sys(kClose, static_cast<uint64_t>(vm::ArgInt(a, 0)));
}

// --- android.app.ActivityManager ---------------------------------------------------------

void ActivityManager_nativeTotalMemory(vm::Thread*, vm::Frame*, vm::JValue* r) {
    MEMORYSTATUSEX m{sizeof m};
    GlobalMemoryStatusEx(&m);
    r->j = static_cast<int64_t>(m.ullTotalPhys);
}

void ActivityManager_nativeAvailableMemory(vm::Thread*, vm::Frame*, vm::JValue* r) {
    MEMORYSTATUSEX m{sizeof m};
    GlobalMemoryStatusEx(&m);
    r->j = static_cast<int64_t>(m.ullAvailPhys);
}

// --- android.os.StatFs ------------------------------------------------------------------

void StatFs_nativeStatFs(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    vm::Runtime* rt = self->runtime();
    r->l = nullptr;
    const std::string path = rt->GetStringUtf8(vm::ArgRef(a, 0));
    kstatfs st{};
    {
        vm::ScopedBlocking blocking(self);
        if (sys(kStatfs, reinterpret_cast<uint64_t>(path.c_str()), reinterpret_cast<uint64_t>(&st)) != 0) return;
    }
    const int64_t values[4] = {static_cast<int64_t>(st.f_bsize), static_cast<int64_t>(st.f_blocks),
                               static_cast<int64_t>(st.f_bfree), static_cast<int64_t>(st.f_bavail)};
    vm::RtClass* long_array = rt->class_linker().FindClass(self, "[J", nullptr);
    if (long_array == nullptr) return;
    vm::Array* out = rt->heap().AllocArray(self, long_array, 4);
    if (out == nullptr) return;
    std::memcpy(out->data(), values, sizeof values);
    r->l = out;
}

// --- java.util.zip.ZipFile --------------------------------------------------------------
//
// Archives in the guest file system, parsed on the host (read-only access).

std::mutex g_zip_lock;
std::map<int64_t, std::shared_ptr<arun::pkg::ZipReader>> g_zips;
int64_t g_next_zip = 1;

std::shared_ptr<arun::pkg::ZipReader> zip_handle(int64_t handle) {
    std::lock_guard<std::mutex> lock(g_zip_lock);
    auto it = g_zips.find(handle);
    return it == g_zips.end() ? nullptr : it->second;
}

void ZipFile_nativeOpen(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    r->j = 0;
    const std::string path = self->runtime()->GetStringUtf8(vm::ArgRef(a, 0));
    Resolved where;
    if (vfs().resolve(Vfs::normalize("/", path), true, where) != 0 || where.kind != Resolved::Host) return;
    std::shared_ptr<arun::pkg::ZipReader> zip;
    {
        vm::ScopedBlocking blocking(self);
        auto opened = arun::pkg::ZipReader::Open(where.host);
        if (!opened) return;
        zip = std::make_shared<arun::pkg::ZipReader>(std::move(*opened));
    }
    std::lock_guard<std::mutex> lock(g_zip_lock);
    r->j = g_next_zip;
    g_zips[g_next_zip++] = std::move(zip);
}

void ZipFile_nativeNames(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    vm::Runtime* rt = self->runtime();
    r->l = nullptr;
    auto zip = zip_handle(vm::ArgLong(a, 0));
    if (!zip) return;
    const auto& entries = zip->entries();
    vm::RootScope roots(self);
    auto arr = roots.Add(rt->heap().AllocArray(self, rt->wk.string_array, static_cast<int32_t>(entries.size())));
    if (arr.Get() == nullptr) return;
    for (size_t i = 0; i < entries.size(); ++i) {
        vm::Object* s = rt->NewStringUtf8(self, entries[i].name);
        if (s == nullptr) return;
        static_cast<vm::Array*>(arr.Get())->elements<vm::Object*>()[i] = s;
    }
    r->l = arr.Get();
}

void ZipFile_nativeEntry(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    vm::Runtime* rt = self->runtime();
    r->l = nullptr;
    auto zip = zip_handle(vm::ArgLong(a, 0));
    if (!zip) return;
    const arun::pkg::ZipEntry* e = zip->Find(rt->GetStringUtf8(vm::ArgRef(a, 2)));
    if (e == nullptr) return;
    const int64_t values[6] = {static_cast<int64_t>(e->uncompressed_size), static_cast<int64_t>(e->compressed_size),
                               static_cast<int64_t>(e->method), static_cast<int64_t>(e->crc32), -1,
                               static_cast<int64_t>(e->data_offset)};
    vm::RtClass* long_array = rt->class_linker().FindClass(self, "[J", nullptr);
    if (long_array == nullptr) return;
    vm::Array* out = rt->heap().AllocArray(self, long_array, 6);
    if (out == nullptr) return;
    std::memcpy(out->data(), values, sizeof values);
    r->l = out;
}

void ZipFile_nativeRead(vm::Thread* self, vm::Frame* a, vm::JValue* r) {
    vm::Runtime* rt = self->runtime();
    r->l = nullptr;
    auto zip = zip_handle(vm::ArgLong(a, 0));
    if (!zip) return;
    const arun::pkg::ZipEntry* e = zip->Find(rt->GetStringUtf8(vm::ArgRef(a, 2)));
    if (e == nullptr || e->is_directory) return;
    std::vector<uint8_t> bytes;
    {
        vm::ScopedBlocking blocking(self);
        auto data = zip->Extract(*e);
        if (!data) return;
        bytes = std::move(*data);
    }
    vm::RtClass* byte_array = rt->class_linker().FindClass(self, "[B", nullptr);
    if (byte_array == nullptr) return;
    vm::Array* out = rt->heap().AllocArray(self, byte_array, static_cast<int32_t>(bytes.size()));
    if (out == nullptr) return;
    if (!bytes.empty()) std::memcpy(out->data(), bytes.data(), bytes.size());
    r->l = out;
}

void ZipFile_nativeClose(vm::Thread*, vm::Frame* a, vm::JValue*) {
    std::lock_guard<std::mutex> lock(g_zip_lock);
    g_zips.erase(vm::ArgLong(a, 0));
}

}  // namespace

void register_direct_natives(vm::Runtime* rt, std::shared_ptr<arun::pkg::ZipReader> apk, std::string guest_apk) {
    g_apk = std::move(apk);
    g_guest_apk = std::move(guest_apk);
    auto reg = [rt](const char* key, vm::NativeFn fn) { rt->RegisterNative(key, fn); };
    reg("Ljava/nio/DirectByteBuffer;.nativeAlloc(I)J", DirectByteBuffer_nativeAlloc);
    reg("Ljava/nio/DirectByteBuffer;.peek(J)B", DirectByteBuffer_peek);
    reg("Ljava/nio/DirectByteBuffer;.poke(JB)V", DirectByteBuffer_poke);
    reg("Ljava/nio/DirectByteBuffer;.peekBytes(J[BII)V", DirectByteBuffer_peekBytes);
    reg("Ljava/nio/DirectByteBuffer;.pokeBytes(J[BII)V", DirectByteBuffer_pokeBytes);
    reg("Landroid/content/res/AssetManager;.nativeOpenAssetFd(Ljava/lang/String;)[J", AssetManager_nativeOpenAssetFd);
    reg("Landroid/os/ParcelFileDescriptor;.nativeOpen(Ljava/lang/String;I)I", ParcelFileDescriptor_nativeOpen);
    reg("Landroid/os/ParcelFileDescriptor;.nativeStatSize(I)J", ParcelFileDescriptor_nativeStatSize);
    reg("Landroid/os/ParcelFileDescriptor;.nativeClose(I)V", ParcelFileDescriptor_nativeClose);
    reg("Landroid/app/ActivityManager;.nativeTotalMemory()J", ActivityManager_nativeTotalMemory);
    reg("Landroid/app/ActivityManager;.nativeAvailableMemory()J", ActivityManager_nativeAvailableMemory);
    reg("Landroid/os/StatFs;.nativeStatFs(Ljava/lang/String;)[J", StatFs_nativeStatFs);
    reg("Ljava/util/zip/ZipFile;.nativeOpen(Ljava/lang/String;)J", ZipFile_nativeOpen);
    reg("Ljava/util/zip/ZipFile;.nativeNames(J)[Ljava/lang/String;", ZipFile_nativeNames);
    reg("Ljava/util/zip/ZipFile;.nativeEntry(JLjava/lang/String;)[J", ZipFile_nativeEntry);
    reg("Ljava/util/zip/ZipFile;.nativeRead(JLjava/lang/String;)[B", ZipFile_nativeRead);
    reg("Ljava/util/zip/ZipFile;.nativeClose(J)V", ZipFile_nativeClose);
    register_media_natives(rt);
}

}  // namespace axrb

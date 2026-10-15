// java.io natives (the Vfs bridge) and dalvik.system dex loading.
#include <cstring>
#include <format>

#include "arun/vfs.h"
#include "arun/vm/natives.h"
#include "arun/vm/runtime.h"
#include "arun/zip_reader.h"

namespace arun::vm {
namespace natives {
namespace {

Runtime* Rt(Thread* self) { return self->runtime(); }

// Every Vfs native fails with ENOENT when the runtime has no file system.
vfs::FileSystem* Fs(Thread* self) { return Rt(self)->file_system(); }

std::string PathArg(Thread* self, Frame* a, std::uint32_t reg) { return Rt(self)->GetStringUtf8(ArgRef(a, reg)); }

// Validates (array, off, len) against the array; false with an exception pending.
bool CheckRange(Thread* self, Array* arr, std::int32_t off, std::int32_t len) {
  if (arr == nullptr) {
    Rt(self)->ThrowNew(self, "Ljava/lang/NullPointerException;", "buffer == null");
    return false;
  }
  if (off < 0 || len < 0 || off > arr->length - len) {
    Rt(self)->ThrowNew(self, "Ljava/lang/ArrayIndexOutOfBoundsException;",
                       std::format("length={}; regionStart={}; regionLength={}", arr->length, off, len));
    return false;
  }
  return true;
}

void Vfs_open(Thread* self, Frame* a, JValue* r) {
  vfs::FileSystem* fs = Fs(self);
  if (fs == nullptr) {
    r->i = -vfs::kENOENT;
    return;
  }
  vfs::OpenFlags flags;
  flags.write = ArgInt(a, 1) != 0;
  flags.append = ArgInt(a, 2) != 0;
  flags.create = ArgInt(a, 3) != 0;
  flags.exclusive = ArgInt(a, 4) != 0;
  const std::string path = PathArg(self, a, 0);
  int handle = -1;
  int err;
  {
    ScopedBlocking blocking(self);
    err = fs->Open(path, flags, &handle);
  }
  r->i = err != 0 ? -err : handle;
}

void Vfs_read(Thread* self, Frame* a, JValue* r) {
  vfs::FileSystem* fs = Fs(self);
  auto* arr = static_cast<Array*>(ArgRef(a, 1));
  const std::int32_t off = ArgInt(a, 2), len = ArgInt(a, 3);
  if (!CheckRange(self, arr, off, len)) return;
  if (fs == nullptr) {
    r->i = -vfs::kEBADF;
    return;
  }
  // Read into host memory first: the array must not be touched while this
  // thread is not runnable (a collection may be running).
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(len));
  std::size_t n = 0;
  int err;
  {
    ScopedBlocking blocking(self);
    err = fs->Read(ArgInt(a, 0), buf, &n);
  }
  if (err != 0) {
    r->i = -err;
    return;
  }
  std::memcpy(arr->data() + off, buf.data(), n);
  r->i = static_cast<std::int32_t>(n);
}

void Vfs_write(Thread* self, Frame* a, JValue* r) {
  vfs::FileSystem* fs = Fs(self);
  auto* arr = static_cast<Array*>(ArgRef(a, 1));
  const std::int32_t off = ArgInt(a, 2), len = ArgInt(a, 3);
  if (!CheckRange(self, arr, off, len)) return;
  if (fs == nullptr) {
    r->i = -vfs::kEBADF;
    return;
  }
  std::vector<std::uint8_t> buf(arr->data() + off, arr->data() + off + len);
  int err;
  {
    ScopedBlocking blocking(self);
    err = fs->Write(ArgInt(a, 0), buf);
  }
  r->i = -err;
}

void Vfs_available(Thread* self, Frame* a, JValue* r) {
  vfs::FileSystem* fs = Fs(self);
  std::int64_t n = 0;
  const int err = fs == nullptr ? vfs::kEBADF : fs->Available(ArgInt(a, 0), &n);
  r->j = err != 0 ? -err : n;
}

void Vfs_skip(Thread* self, Frame* a, JValue* r) {
  vfs::FileSystem* fs = Fs(self);
  std::int64_t n = 0;
  const int err = fs == nullptr ? vfs::kEBADF : fs->Skip(ArgInt(a, 0), ArgLong(a, 1), &n);
  r->j = err != 0 ? -err : n;
}

void Vfs_close(Thread* self, Frame* a, JValue*) {
  if (vfs::FileSystem* fs = Fs(self)) fs->Close(ArgInt(a, 0));
}

void Vfs_access(Thread* self, Frame* a, JValue* r) {
  vfs::FileSystem* fs = Fs(self);
  r->i = fs != nullptr && fs->Access(PathArg(self, a, 0), ArgInt(a, 1)) ? 1 : 0;
}

bool StatOf(Thread* self, Frame* a, vfs::StatInfo* info) {
  vfs::FileSystem* fs = Fs(self);
  return fs != nullptr && fs->Stat(PathArg(self, a, 0), info) == 0;
}

void Vfs_isDirectory(Thread* self, Frame* a, JValue* r) {
  vfs::StatInfo info;
  r->i = StatOf(self, a, &info) && info.is_directory ? 1 : 0;
}

void Vfs_isFile(Thread* self, Frame* a, JValue* r) {
  vfs::StatInfo info;
  r->i = StatOf(self, a, &info) && !info.is_directory ? 1 : 0;
}

void Vfs_length(Thread* self, Frame* a, JValue* r) {
  vfs::StatInfo info;
  r->j = StatOf(self, a, &info) ? static_cast<std::int64_t>(info.size) : 0;
}

void Vfs_lastModified(Thread* self, Frame* a, JValue* r) {
  vfs::StatInfo info;
  r->j = StatOf(self, a, &info) ? info.mtime_ms : 0;
}

void Vfs_mkdir(Thread* self, Frame* a, JValue* r) {
  vfs::FileSystem* fs = Fs(self);
  r->i = fs == nullptr ? -vfs::kENOENT : -fs->Mkdir(PathArg(self, a, 0));
}

void Vfs_remove(Thread* self, Frame* a, JValue* r) {
  vfs::FileSystem* fs = Fs(self);
  r->i = fs == nullptr ? -vfs::kENOENT : -fs->Remove(PathArg(self, a, 0));
}

void Vfs_rename(Thread* self, Frame* a, JValue* r) {
  vfs::FileSystem* fs = Fs(self);
  r->i = fs == nullptr ? -vfs::kENOENT : -fs->Rename(PathArg(self, a, 0), PathArg(self, a, 1));
}

void Vfs_list(Thread* self, Frame* a, JValue* r) {
  Runtime* rt = Rt(self);
  vfs::FileSystem* fs = Fs(self);
  std::vector<std::string> names;
  if (fs == nullptr || fs->List(PathArg(self, a, 0), &names) != 0) {
    r->l = nullptr;
    return;
  }
  RootScope roots(self);
  auto arr = roots.Add(rt->heap().AllocArray(self, rt->wk.string_array, static_cast<std::int32_t>(names.size())));
  if (arr.Get() == nullptr) return;
  for (std::size_t i = 0; i < names.size(); ++i) {
    Object* s = rt->NewStringUtf8(self, names[i]);
    if (s == nullptr) return;
    static_cast<Array*>(arr.Get())->elements<Object*>()[i] = s;
  }
  r->l = arr.Get();
}

void Vfs_strerror(Thread* self, Frame* a, JValue* r) {
  r->l = Rt(self)->NewStringUtf8(self, vfs::ErrnoDescription(ArgInt(a, 0)));
}

// --- dalvik.system.BaseDexClassLoader -------------------------------------------------

// Appends the DEX files of one dex-path element (a .dex, or a zip containing
// classes.dex, classes2.dex, ...). False with a message on failure.
bool CollectDex(vfs::FileSystem* fs, const std::string& path, std::vector<std::unique_ptr<DexFile>>* out,
                std::string* error) {
  std::vector<std::uint8_t> bytes;
  if (int e = fs->ReadFile(path, &bytes); e != 0) {
    *error = std::format("{}: {}", path, vfs::ErrnoDescription(e));
    return false;
  }
  if (bytes.size() >= 4 && std::memcmp(bytes.data(), "dex\n", 4) == 0) {
    auto dex = DexFile::Open(std::move(bytes), path);
    if (!dex.ok()) {
      *error = std::string(dex.status().message());
      return false;
    }
    out->push_back(std::move(*dex));
    return true;
  }
  auto zip = pkg::ZipReader::FromBytes(bytes, path);
  if (!zip.ok()) {
    *error = std::format("{}: not a dex file or zip archive ({})", path, zip.status().message());
    return false;
  }
  for (int i = 1;; ++i) {
    const std::string entry = i == 1 ? "classes.dex" : std::format("classes{}.dex", i);
    const pkg::ZipEntry* e = zip->Find(entry);
    if (e == nullptr) break;
    auto data = zip->Extract(*e);
    if (!data.ok()) {
      *error = std::format("{}!{}: {}", path, entry, data.status().message());
      return false;
    }
    auto dex = DexFile::Open(std::move(*data), path + "!" + entry);
    if (!dex.ok()) {
      *error = std::string(dex.status().message());
      return false;
    }
    out->push_back(std::move(*dex));
  }
  return true;
}

void BaseDexClassLoader_nativeOpenDexPath(Thread* self, Frame* a, JValue*) {
  Runtime* rt = Rt(self);
  Object* loader = ArgRef(a, 0);
  const std::string dex_path = PathArg(self, a, 1);
  vfs::FileSystem* fs = Fs(self);
  std::vector<std::unique_ptr<DexFile>> dexes;
  std::string error;
  std::size_t start = 0;
  while (fs != nullptr && start <= dex_path.size()) {
    std::size_t end = dex_path.find(':', start);
    if (end == std::string::npos) end = dex_path.size();
    const std::string element = dex_path.substr(start, end - start);
    start = end + 1;
    if (element.empty()) continue;
    if (!CollectDex(fs, element, &dexes, &error)) break;
  }
  if (fs == nullptr) error = dex_path + ": no file system";
  if (!error.empty()) {
    rt->ThrowNew(self, "Ljava/io/IOException;", error);
    return;
  }
  // Runtime-level parent: the Java parent's loader if the runtime backs it.
  RtField* parent_field = rt->wk.class_loader->FindDeclaredField("parent", "Ljava/lang/ClassLoader;");
  Object* jparent = parent_field != nullptr ? FieldSlots(loader)[parent_field->slot].l : nullptr;
  ClassLoaderRt* parent = LoaderFromJava(rt, jparent);
  ClassLoaderRt* l = rt->class_linker().CreateLoader(dex_path, parent, std::move(dexes));
  l->java_object = loader;
  FieldSlots(loader)[rt->wk.class_loader_vm_loader->slot].j =
      static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(l));
}

}  // namespace

void RegisterIo(Runtime* rt) {
  auto reg = [rt](const char* key, NativeFn fn) { rt->RegisterNative(key, fn); };
  reg("Ljava/io/Vfs;.open(Ljava/lang/String;ZZZZ)I", Vfs_open);
  reg("Ljava/io/Vfs;.read(I[BII)I", Vfs_read);
  reg("Ljava/io/Vfs;.write(I[BII)I", Vfs_write);
  reg("Ljava/io/Vfs;.available(I)J", Vfs_available);
  reg("Ljava/io/Vfs;.skip(IJ)J", Vfs_skip);
  reg("Ljava/io/Vfs;.close(I)V", Vfs_close);
  reg("Ljava/io/Vfs;.access(Ljava/lang/String;I)Z", Vfs_access);
  reg("Ljava/io/Vfs;.isDirectory(Ljava/lang/String;)Z", Vfs_isDirectory);
  reg("Ljava/io/Vfs;.isFile(Ljava/lang/String;)Z", Vfs_isFile);
  reg("Ljava/io/Vfs;.length(Ljava/lang/String;)J", Vfs_length);
  reg("Ljava/io/Vfs;.lastModified(Ljava/lang/String;)J", Vfs_lastModified);
  reg("Ljava/io/Vfs;.mkdir(Ljava/lang/String;)I", Vfs_mkdir);
  reg("Ljava/io/Vfs;.remove(Ljava/lang/String;)I", Vfs_remove);
  reg("Ljava/io/Vfs;.rename(Ljava/lang/String;Ljava/lang/String;)I", Vfs_rename);
  reg("Ljava/io/Vfs;.list(Ljava/lang/String;)[Ljava/lang/String;", Vfs_list);
  reg("Ljava/io/Vfs;.strerror(I)Ljava/lang/String;", Vfs_strerror);
  reg("Ldalvik/system/BaseDexClassLoader;.nativeOpenDexPath(Ljava/lang/String;)V",
      BaseDexClassLoader_nativeOpenDexPath);
}

}  // namespace natives
}  // namespace arun::vm

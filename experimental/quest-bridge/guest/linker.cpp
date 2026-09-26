#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "qb_env.h"
#include <windows.h>
/* Joining several Android shared objects together in guest memory.

   The single-object loader could assume every undefined symbol was ours and
   hand it a thunk by position. With more than one object that is no longer
   true: a symbol may be defined by a sibling, so relocations have to be
   resolved by name against everything loaded, and only what is left over is
   taken to be the C library. */

#include "linker.h"

uint64_t guest_libc_data(const std::string& name);

#include "libc_names.h"
#include <unordered_set>

/* True for names Android's libc, libm or libdl define. */
bool is_libc_name(const std::string& name) {
    static const std::unordered_set<std::string> names(std::begin(kLibcNames), std::end(kLibcNames));
    return names.count(name) != 0;
}

#include <cstdio>
#include <algorithm>
#include <cstring>

namespace {

uint64_t u64(const uint8_t* p) {
    uint64_t v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

uint32_t u32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* One entry of .dynsym. */
struct Symbol {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t section;
    uint64_t value;
    uint64_t size;
};

Symbol symbol_at(const GuestBytes& image, uint64_t symtab, uint64_t syment, uint64_t index) {
    Symbol s{};
    uint64_t at = symtab + index * syment;
    if (at + 24 > image.size()) return s;
    const uint8_t* p = image.data() + at;
    s.name = u32(p);
    s.info = p[4];
    s.other = p[5];
    s.section = u16(p + 6);
    s.value = u64(p + 8);
    s.size = u64(p + 16);
    return s;
}

std::string directory_of(const std::string& path) {
    size_t cut = path.find_last_of("\\/");
    return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
}

}  // namespace

const char* GuestObject::string_at(uint64_t offset) const {
    if (strtab + offset >= image.size()) return "";
    return reinterpret_cast<const char*>(image.data() + strtab + offset);
}

GuestLinker::~GuestLinker() {
    for (GuestObject* object : objects) delete object;
}

bool GuestLinker::is_system(const std::string& soname) const {
    /* These are the libraries we answer ourselves rather than run. */
    static const char* ours[] = {"libc.so",      "libm.so",    "libdl.so",     "liblog.so",
                                 "libstdc++.so", "libc++.so",  "libc++_shared.so", "libandroid.so",
                                 "libEGL.so",    "libGLESv2.so", "libGLESv3.so",
                                 "libvulkan.so", "libGLESv1_CM.so", "libopenxr_loader.so", "libmediandk.so", "libnativewindow.so",
                                 /* the Oculus Platform SDK: platform.cpp stands in */
                                 "libovrplatformloader.so"};
    /* QB_APP_LIBCXX=1: the app's own libc++_shared.so is loaded and run
       instead (Unreal needs parts of it the stand-in does not have). */
    static const bool app_libcxx = QB_ENV("QB_APP_LIBCXX") != nullptr;
    if (app_libcxx && soname == "libc++_shared.so") return false;
    for (const char* name : ours)
        if (soname == name) return true;
    return false;
}

uint64_t GuestLinker::lookup(const std::string& name) const {
    auto found = exports.find(name);
    return found == exports.end() ? 0 : found->second;
}

uint64_t GuestLinker::thunk_for(const std::string& name) {
    /* Called at run time too, from any guest thread, when dlsym or a Vulkan
       proc lookup hands out a new function. The list is reserved up front so
       it never moves while another thread is reading a name from it. */
    std::lock_guard<std::mutex> held(thunk_lock);
    if (imports.capacity() < kMaxImports) imports.reserve(kMaxImports);
    if (imports.size() >= kMaxImports) {
        std::fprintf(stderr, "linker: more than %d imports\n", (int)kMaxImports);
        return 0;
    }
    auto found = import_slot.find(name);
    if (found != import_slot.end()) return thunk_base + (uint64_t)found->second * 16;
    int slot = (int)imports.size();
    imports.push_back(name);
    import_slot[name] = slot;
    if (mapped) mapped->thunk_count = (int)imports.size();
    return thunk_base + (uint64_t)slot * 16;
}

bool GuestLinker::load(const char* path, GuestMem& mem) {
    std::string wanted = path;
    std::string soname = wanted.substr(wanted.find_last_of("\\/") + 1);
    for (GuestObject* object : objects)
        if (object->name == soname) return true; /* already in */

    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) {
        error = "cannot open " + wanted;
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<uint8_t> bytes((size_t)length);
    std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (bytes.size() < 64 || bytes[0] != 0x7f || std::memcmp(bytes.data() + 1, "ELF", 3) != 0) {
        error = wanted + " is not an ELF file";
        return false;
    }

    GuestObject* object = new GuestObject();
    object->name = soname;
    objects.push_back(object);

    const uint8_t* e = bytes.data();
    uint64_t phoff = u64(e + 32);
    uint16_t phentsize = u16(e + 54);
    uint16_t phnum = u16(e + 56);
    object->program_headers.assign(e + phoff, e + phoff + (size_t)phnum * phentsize);
    object->program_header_count = phnum;
    uint64_t span = 0;
    uint64_t dynamic = 0;
    for (int i = 0; i < phnum; ++i) {
        const uint8_t* ph = e + phoff + (uint64_t)i * phentsize;
        uint32_t type = u32(ph);
        uint64_t va = u64(ph + 16);
        uint64_t memsz = u64(ph + 40);
        if (type == 1 && va + memsz > span) span = va + memsz;
        if (type == 2) dynamic = va;
        if (type == 7) { /* PT_TLS */
            object->tls_template = va;
            object->tls_filesz = u64(ph + 32);
            object->tls_memsz = memsz;
            object->tls_align = u64(ph + 48);
            if (!object->tls_align) object->tls_align = 8;
        }
    }
    /* Each object gets its own slice of address space, with a gap after it so
       a stray pointer lands nowhere rather than in the next library. The
       slice has to be free in the host process too, since guest addresses
       are host addresses, so look for one rather than assume. */
    uint64_t slice = (span + 0x10000 + 0xfffff) & ~0xfffffull;
    object->base = guest_reserve_near(next_base, slice, false, 0x10000000ull);
    VirtualFree(reinterpret_cast<void*>(object->base), 0, MEM_RELEASE);
    next_base = object->base + slice;
    object->image.at = object->base;
    object->image.assign((size_t)span + 0x1000, 0);
    for (int i = 0; i < phnum; ++i) {
        const uint8_t* ph = e + phoff + (uint64_t)i * phentsize;
        if (u32(ph) != 1) continue;
        uint64_t offset = u64(ph + 8);
        uint64_t va = u64(ph + 16);
        uint64_t filesz = u64(ph + 32);
        if (filesz) std::memcpy(object->image.data() + va, e + offset, (size_t)filesz);
    }
    mem.map(object->image.data(), object->base, object->image.size());

    /* The dynamic section, which says where everything else is. */
    for (uint64_t at = dynamic; at + 16 <= object->image.size(); at += 16) {
        int64_t tag = (int64_t)u64(object->image.data() + at);
        uint64_t value = u64(object->image.data() + at + 8);
        if (tag == 0) break;
        switch (tag) {
            case 1: object->needed.push_back(""); object->needed.back() = std::to_string(value); break;
            case 5: object->strtab = value; break;
            case 6: object->symtab = value; break;
            case 11: object->syment = value; break;
            case 7: object->rela = value; break;
            case 8: object->relasz = value; break;
            case 9: object->relaent = value; break;
            case 23: object->jmprel = value; break;
            case 2: object->pltrelsz = value; break;
            case 36: object->relr = value; break;
            case 35: object->relrsz = value; break;
            case 0x6fffe000: object->relr = value; break;    /* DT_ANDROID_RELR */
            case 0x6fffe001: object->relrsz = value; break;  /* DT_ANDROID_RELRSZ */
            case 0x60000011: object->android_rela = value; break;
            case 0x60000012: object->android_relasz = value; break;
            case 25: object->init_array = value; break;
            case 27: object->init_arraysz = value; break;
            case 12: object->init_one = value; break;
            default: break;
        }
    }
    /* DT_NEEDED holds string offsets, which only mean something once strtab
       is known, so they were kept as numbers until now. */
    for (std::string& entry : object->needed) entry = object->string_at(std::strtoull(entry.c_str(), nullptr, 10));

    /* How many dynamic symbols there are. The section table says so directly
       when it is present, which it is for everything the NDK produces. */
    uint64_t shoff = u64(e + 40);
    uint16_t shentsize = u16(e + 58);
    uint16_t shnum = u16(e + 60);
    for (int i = 0; i < shnum && shoff; ++i) {
        const uint8_t* sh = e + shoff + (uint64_t)i * shentsize;
        if (u32(sh + 4) != 11) continue; /* SHT_DYNSYM */
        uint64_t size = u64(sh + 32);
        uint64_t entsize = u64(sh + 56);
        if (entsize) object->symcount = size / entsize;
        break;
    }

    /* Everything this object defines becomes available to the others. */
    for (uint64_t i = 0; i < object->symcount; ++i) {
        Symbol symbol = symbol_at(object->image, object->symtab, object->syment, i);
        if (symbol.section == 0 || !symbol.name) continue;
        int binding = symbol.info >> 4;
        if (binding != 1 && binding != 2) continue; /* global or weak only */
        std::string name = object->string_at(symbol.name);
        if (name.empty()) continue;
        if (name == "JNI_OnLoad") object->jni_onload = object->base + symbol.value;
        object->symbols[name] = object->base + symbol.value;
        if (!exports.count(name)) exports[name] = object->base + symbol.value;
    }

    /* Anything it needs and we have a file for gets loaded too. */
    for (const std::string& full : object->needed) {
        /* A needed entry is normally a bare soname, but it can carry a path
           if the library was linked by path, so only the name is used. */
        std::string needs = full.substr(full.find_last_of("/\\") + 1);
        if (is_system(needs)) continue;
        bool found = false;
        for (const std::string& where : search) {
            std::string candidate = where + "/" + needs;
            FILE* probe = nullptr;
            if (fopen_s(&probe, candidate.c_str(), "rb") == 0 && probe) {
                std::fclose(probe);
                if (!load(candidate.c_str(), mem)) return false;
                found = true;
                break;
            }
        }
        if (!found)
            std::printf("linker: %s wants %s, which is taken to be the system's\n", soname.c_str(), needs.c_str());
    }
    return true;
}

/* Android's packed relocations: "APS2", then signed LEB128 numbers. The
   relocations come in groups that may share an offset step, an info word or
   an addend step, and what is shared is written once for the group. The
   result is ordinary 24-byte RELA entries, so one routine applies both. */
std::vector<uint8_t> unpack_aps2(const uint8_t* data, uint64_t size) {
    std::vector<uint8_t> out;
    if (size < 4 || std::memcmp(data, "APS2", 4) != 0) return out;
    uint64_t at = 4;
    bool broken = false;
    auto next = [&]() -> int64_t {
        int64_t value = 0;
        int shift = 0;
        uint8_t byte = 0;
        do {
            if (at >= size) {
                broken = true;
                return 0;
            }
            byte = data[at++];
            value |= (int64_t)(byte & 0x7f) << shift;
            shift += 7;
        } while (byte & 0x80);
        if (shift < 64 && (byte & 0x40)) value |= -((int64_t)1 << shift);
        return value;
    };
    const int64_t kByInfo = 1, kByOffsetDelta = 2, kByAddend = 4, kHasAddend = 8;
    int64_t count = next();
    uint64_t offset = (uint64_t)next();
    uint64_t info = 0;
    int64_t addend = 0;
    int64_t done = 0;
    while (done < count && !broken) {
        int64_t group = next();
        int64_t flags = next();
        int64_t offset_step = (flags & kByOffsetDelta) ? next() : 0;
        if (flags & kByInfo) info = (uint64_t)next();
        if ((flags & kHasAddend) && (flags & kByAddend)) addend += next();
        else if (!(flags & kHasAddend)) addend = 0;
        for (int64_t i = 0; i < group && !broken; ++i) {
            offset += (flags & kByOffsetDelta) ? (uint64_t)offset_step : (uint64_t)next();
            if (!(flags & kByInfo)) info = (uint64_t)next();
            if ((flags & kHasAddend) && !(flags & kByAddend)) addend += next();
            uint8_t entry[24];
            std::memcpy(entry, &offset, 8);
            std::memcpy(entry + 8, &info, 8);
            std::memcpy(entry + 16, &addend, 8);
            out.insert(out.end(), entry, entry + 24);
        }
        done += group;
    }
    if (broken) out.clear();
    return out;
}

bool GuestLinker::relocate(GuestMem& mem) {
    mapped = &mem;
    /* Give every object that asked for thread local storage a place in the
       block, and build the block, so a relocation can be worked out as an
       offset from the thread pointer. */
    if (tls_used < 16) tls_used = 16;
    std::vector<GuestObject*> placed_now;
    for (GuestObject* object : objects) {
        if (!object->tls_memsz || object->tls_placed) continue;
        uint64_t align = object->tls_align ? object->tls_align : 8;
        tls_used = (tls_used + align - 1) & ~(align - 1);
        object->tls_offset = tls_used - 16;
        tls_used += object->tls_memsz;
        object->tls_placed = true;
        placed_now.push_back(object);
    }
    /* The block is mapped once, with room to spare, so a library opened later
       can take its share without the storage moving under a live mapping. */
    if (tls.empty()) {
        tls.at = tls_base;
        tls.assign(std::max<size_t>((size_t)tls_used + 64, 1u << 20), 0);
        mem.map(tls.data(), tls_base, tls.size());
    } else if (tls.size() < tls_used + 64) {
        std::printf("linker: thread local storage outgrew its block\n");
    }
    for (GuestObject* object : placed_now) {
        uint8_t* to = tls.data() + 16 + object->tls_offset;
        std::memset(to, 0, (size_t)object->tls_memsz);
        if (object->tls_filesz)
            std::memcpy(to, object->image.data() + object->tls_template, (size_t)object->tls_filesz);
    }
    /* What a new thread starts from: the pristine template of every object,
       not the main thread's current values. */
    if (tls_initial.size() < tls.size()) tls_initial.resize(tls.size(), 0);
    for (GuestObject* object : placed_now) {
        uint8_t* to = tls_initial.data() + 16 + object->tls_offset;
        std::memset(to, 0, (size_t)object->tls_memsz);
        if (object->tls_filesz)
            std::memcpy(to, object->image.data() + object->tls_template, (size_t)object->tls_filesz);
    }

    for (GuestObject* object : objects) {
        if (object->relocated) continue;
        object->relocated = true;
        uint64_t base = object->base;
        auto apply_from = [&](const uint8_t* data, uint64_t size, uint64_t stride) {
            for (uint64_t at = 0; stride && at + stride <= size; at += stride) {
                const uint8_t* entry = data + at;
                uint64_t offset = u64(entry);
                uint64_t info = u64(entry + 8);
                int64_t addend = (int64_t)u64(entry + 16);
                uint32_t type = (uint32_t)info;
                uint32_t index = (uint32_t)(info >> 32);
                uint64_t value = 0;
                if (type == 1027) { /* RELATIVE, no symbol involved */
                    value = base + (uint64_t)addend;
                } else if (type == 257 || type == 1025 || type == 1026) {
                    /* ABS64, GLOB_DAT and JUMP_SLOT all name a symbol. */
                    Symbol symbol = symbol_at(object->image, object->symtab, object->syment, index);
                    std::string name = object->string_at(symbol.name);
                    if (symbol.section != 0) {
                        value = base + symbol.value + (uint64_t)addend;
                    } else {
                        /* libc's own names bind to the runtime's libc, never
                           to a game library that happens to export one too
                           (an interposer like Crashpad's pthread_create). */
                        uint64_t defined = is_libc_name(name) ? 0 : lookup(name);
                        if (!defined) defined = guest_libc_data(name);
                        /* A thread_local's init hook (_ZTH...) that nothing
                           defines is a weak null the code tests before calling. */
                        bool weak_hook = !defined && name.compare(0, 4, "_ZTH") == 0;
                        value = defined ? defined + (uint64_t)addend : weak_hook ? 0 : thunk_for(name);
                    }
                } else if (type == 1031) {
                    /* TLSDESC. The pair of words is a function to call and an
                       argument for it; ours hands back the offset from the
                       thread pointer that the second word carries. */
                    Symbol symbol = symbol_at(object->image, object->symtab, object->syment, index);
                    uint64_t within = (index && symbol.section != 0) ? symbol.value : 0;
                    uint64_t resolver = thunk_for("__tlsdesc_resolve");
                    uint64_t from_tp = 16 + object->tls_offset + within + (uint64_t)addend;
                    if (offset + 16 <= object->image.size()) {
                        std::memcpy(object->image.data() + offset, &resolver, 8);
                        std::memcpy(object->image.data() + offset + 8, &from_tp, 8);
                    }
                    continue;
                } else if (type == 1030) {
                    /* TPREL64 gives that offset directly. */
                    Symbol symbol = symbol_at(object->image, object->symtab, object->syment, index);
                    uint64_t within = (index && symbol.section != 0) ? symbol.value : 0;
                    value = 16 + object->tls_offset + within + (uint64_t)addend;
                } else {
                    std::printf("linker: %s has relocation type %u, which is not handled\n", object->name.c_str(),
                                type);
                    continue;
                }
                if (offset + 8 <= object->image.size()) std::memcpy(object->image.data() + offset, &value, 8);
            }
        };
        auto apply = [&](uint64_t table, uint64_t size, uint64_t stride) {
            if (table && table + size <= object->image.size()) apply_from(object->image.data() + table, size, stride);
        };
        apply(object->rela, object->relasz, object->relaent);
        apply(object->jmprel, object->pltrelsz, 24);
        if (object->android_rela) {
            std::vector<uint8_t> unpacked = unpack_aps2(object->image.data() + object->android_rela,
                                                        object->android_relasz);
            if (unpacked.empty())
                std::printf("linker: %s has packed relocations that did not decode\n", object->name.c_str());
            apply_from(unpacked.data(), unpacked.size(), 24);
        }

        /* The packed form of the relative relocations. */
        for (uint64_t at = 0, where = 0; at + 8 <= object->relrsz; at += 8) {
            uint64_t entry = u64(object->image.data() + object->relr + at);
            if ((entry & 1) == 0) {
                where = entry;
                uint64_t value = u64(object->image.data() + where) + base;
                std::memcpy(object->image.data() + where, &value, 8);
                where += 8;
                continue;
            }
            for (int bit = 1; bit < 64; ++bit) {
                if (!((entry >> bit) & 1)) continue;
                uint64_t target = where + (uint64_t)(bit - 1) * 8;
                uint64_t value = u64(object->image.data() + target) + base;
                std::memcpy(object->image.data() + target, &value, 8);
            }
            where += 63 * 8;
        }
    }
    /* Now that the relocations are in, the initialiser list can be read: its
       entries are relocated like any other pointer, so before this point they
       are still zero. */
    for (GuestObject* object : objects) {
        object->initialisers.clear();
        if (object->init_one) object->initialisers.push_back(object->base + object->init_one);
        for (uint64_t at = 0; at + 8 <= object->init_arraysz; at += 8) {
            uint64_t entry = u64(object->image.data() + object->init_array + at);
            if (entry && entry != ~0ull) object->initialisers.push_back(entry);
        }
    }
    mem.thunk_va = thunk_base;
    mem.thunk_count = (int)imports.size();
    return true;
}

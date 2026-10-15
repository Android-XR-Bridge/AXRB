#include "exec.h"

#include "cpu.h"
#include "files.h"
#include "log.h"
#include "memory.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstring>

namespace axrb {
using namespace linux_abi;

namespace {

#pragma pack(push, 1)
struct Elf64Header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Elf64Phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
#pragma pack(pop)
static_assert(sizeof(Elf64Header) == 64 && sizeof(Elf64Phdr) == 56);

constexpr uint32_t PT_LOAD = 1, PT_INTERP = 3, PT_PHDR = 6;
constexpr uint16_t ET_EXEC = 2, ET_DYN = 3, EM_AARCH64 = 183;
constexpr uint64_t kStackSize = 8ull << 20;
// Where Linux puts a PIE on a 39-bit arm64 address space (2/3 of TASK_SIZE).
constexpr uint64_t kPieBase = 0x5555554000ull;

struct LoadedElf {
    uint64_t bias = 0;
    uint64_t entry = 0;
    uint64_t phdr = 0;
    uint16_t phnum = 0;
    uint64_t end = 0;  // highest mapped address
    std::string interpreter;
    std::string path;
};

uint32_t segment_prot(uint32_t flags) {
    uint32_t prot = 0;
    if (flags & 4) prot |= PROT_READ_;
    if (flags & 2) prot |= PROT_WRITE_;
    if (flags & 1) prot |= PROT_EXEC_;
    return prot;
}

int64_t load_elf(GuestThread& t, const std::string& path, uint64_t hint, LoadedElf& out) {
    std::shared_ptr<OpenFile> file;
    if (int64_t e = open_path(t, path, O_RDONLY_ | O_CLOEXEC_, 0, file)) return e;
    if (!file->mappable()) return -EACCES_;
    Elf64Header eh{};
    if (file->pread_host(&eh, sizeof eh, 0) != sizeof eh) return -ENOEXEC_;
    if (std::memcmp(eh.ident, "\x7f" "ELF", 4) != 0 || eh.ident[4] != 2 || eh.ident[5] != 1) return -ENOEXEC_;
    if (eh.machine != EM_AARCH64 || (eh.type != ET_EXEC && eh.type != ET_DYN)) return -ENOEXEC_;
    if (eh.phentsize != sizeof(Elf64Phdr) || eh.phnum == 0 || eh.phnum > 128) return -ENOEXEC_;
    std::vector<Elf64Phdr> ph(eh.phnum);
    int64_t bytes = static_cast<int64_t>(ph.size() * sizeof(Elf64Phdr));
    if (file->pread_host(ph.data(), static_cast<uint64_t>(bytes), eh.phoff) != bytes) return -ENOEXEC_;

    uint64_t lo = ~0ull, hi = 0;
    for (auto& p : ph) {
        if (p.type == PT_INTERP) {
            std::string interp(static_cast<size_t>(p.filesz), '\0');
            file->pread_host(interp.data(), p.filesz, p.offset);
            out.interpreter = interp.c_str();
        }
        if (p.type != PT_LOAD) continue;
        lo = std::min(lo, page_down(p.vaddr));
        hi = std::max(hi, page_up(p.vaddr + p.memsz));
    }
    if (lo >= hi) return -ENOEXEC_;

    // Reserve the whole image, then place segments inside the reservation.
    uint64_t bias = 0;
    if (eh.type == ET_DYN) {
        int64_t at = address_space().mmap(hint, hi - lo, PROT_NONE_, MAP_PRIVATE_ | MAP_ANONYMOUS_, nullptr, 0);
        if (at < 0) return at;
        bias = static_cast<uint64_t>(at) - lo;
    } else {
        int64_t at = address_space().mmap(lo, hi - lo, PROT_NONE_, MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_, nullptr, 0);
        if (at < 0) return at;
    }

    for (auto& p : ph) {
        if (p.type != PT_LOAD || p.memsz == 0) continue;
        uint32_t prot = segment_prot(p.flags);
        uint64_t start = bias + p.vaddr;
        uint64_t map_start = page_down(start);
        uint64_t file_end = start + p.filesz;
        uint64_t mem_end = start + p.memsz;
        if (p.filesz) {
            uint64_t length = page_up(file_end) - map_start;
            uint64_t offset = page_down(p.offset);
            if ((p.offset - offset) != (start - map_start)) return -ENOEXEC_;
            // Writable while the tail of the last file page is cleared.
            int64_t r = address_space().mmap(map_start, length, prot | PROT_WRITE_, MAP_PRIVATE_ | MAP_FIXED_, file, offset);
            if (r < 0) return r;
            if (mem_end > file_end && page_up(file_end) > file_end) {
                uint64_t clear_end = std::min(page_up(file_end), mem_end);
                address_space().populate(file_end, clear_end - file_end);
                std::memset(reinterpret_cast<void*>(file_end), 0, static_cast<size_t>(clear_end - file_end));
            }
            if (!(prot & PROT_WRITE_)) address_space().mprotect(map_start, length, prot);
        }
        uint64_t anon_start = p.filesz ? page_up(file_end) : map_start;
        if (page_up(mem_end) > anon_start) {
            int64_t r = address_space().mmap(anon_start, page_up(mem_end) - anon_start, prot,
                                             MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_, nullptr, 0);
            if (r < 0) return r;
            address_space().name_range(anon_start, page_up(mem_end) - anon_start, "[anon:.bss]");
        }
    }

    out.bias = bias;
    out.entry = bias + eh.entry;
    out.phnum = eh.phnum;
    out.end = bias + hi;
    out.path = file->guest_path();
    for (auto& p : ph)
        if (p.type == PT_PHDR) out.phdr = bias + p.vaddr;
    if (!out.phdr) {
        for (auto& p : ph)
            if (p.type == PT_LOAD && eh.phoff >= p.offset && eh.phoff < p.offset + p.filesz)
                out.phdr = bias + p.vaddr + (eh.phoff - p.offset);
    }
    TRACE("mmap", "loaded %s at %llx (entry %llx)", path.c_str(), (unsigned long long)bias, (unsigned long long)out.entry);
    return 0;
}

}  // namespace

int64_t exec_program(GuestThread& thread, const std::string& path, const std::vector<std::string>& argv,
                     const std::vector<std::string>& envp, CpuContext& start) {
    auto& space = address_space();
    // The stack sits at the top of the arena, like Linux's below TASK_SIZE.
    uint64_t stack_top = space.arena_end() - (16ull << 20);
    int64_t stack = space.mmap(stack_top - kStackSize, kStackSize, PROT_READ_ | PROT_WRITE_,
                               MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_, nullptr, 0, "[stack]");
    if (stack < 0) return stack;

    LoadedElf program;
    if (int64_t e = load_elf(thread, path, kPieBase, program)) {
        LOGE("exec", "cannot load %s: %lld", path.c_str(), (long long)e);
        return e;
    }
    LoadedElf interpreter;
    if (!program.interpreter.empty()) {
        if (int64_t e = load_elf(thread, program.interpreter, 0, interpreter)) {
            LOGE("exec", "cannot load interpreter %s: %lld", program.interpreter.c_str(), (long long)e);
            return e;
        }
    }
    space.set_brk_base(program.end + kPageSize);

    // Strings and the random bytes at the very top, then the vectors.
    std::vector<uint8_t> blob;
    auto put_string = [&](const std::string& s) {
        uint64_t offset = blob.size();
        blob.insert(blob.end(), s.begin(), s.end());
        blob.push_back(0);
        return offset;
    };
    uint64_t execfn = put_string(program.path);
    std::vector<uint64_t> argv_offsets, envp_offsets;
    for (auto& a : argv) argv_offsets.push_back(put_string(a));
    for (auto& e : envp) envp_offsets.push_back(put_string(e));
    uint64_t platform = put_string("aarch64");
    while (blob.size() % 16) blob.push_back(0);
    uint64_t random = blob.size();
    blob.resize(blob.size() + 16);
    BCryptGenRandom(nullptr, blob.data() + random, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    while (blob.size() % 16) blob.push_back(0);

    uint64_t blob_base = (stack_top - 64 - blob.size()) & ~15ull;
    auto at = [&](uint64_t offset) { return blob_base + offset; };

    const uint64_t hwcap = HWCAP_FP_ | HWCAP_ASIMD_ | HWCAP_EVTSTRM_ | HWCAP_AES_ | HWCAP_PMULL_ | HWCAP_SHA1_ |
                           HWCAP_SHA2_ | HWCAP_CRC32_;
    std::vector<uint64_t> auxv = {
        AT_PHDR_, program.phdr,
        AT_PHENT_, sizeof(Elf64Phdr),
        AT_PHNUM_, program.phnum,
        AT_PAGESZ_, kPageSize,
        AT_BASE_, interpreter.bias,
        AT_FLAGS_, 0,
        AT_ENTRY_, program.entry,
        AT_UID_, process().uid,
        AT_EUID_, process().uid,
        AT_GID_, process().gid,
        AT_EGID_, process().gid,
        AT_SECURE_, 0,
        AT_RANDOM_, at(random),
        AT_HWCAP_, hwcap,
        AT_HWCAP2_, 0,
        AT_CLKTCK_, 100,
        AT_MINSIGSTKSZ_, 5120,
        AT_EXECFN_, at(execfn),
        AT_PLATFORM_, at(platform),
        AT_NULL_, 0,
    };
    process().auxv = auxv;

    std::vector<uint64_t> vectors;
    vectors.push_back(argv.size());
    for (uint64_t o : argv_offsets) vectors.push_back(at(o));
    vectors.push_back(0);
    for (uint64_t o : envp_offsets) vectors.push_back(at(o));
    vectors.push_back(0);
    vectors.insert(vectors.end(), auxv.begin(), auxv.end());
    uint64_t sp = (blob_base - vectors.size() * 8) & ~15ull;

    space.populate(sp, stack_top - sp);
    if (!guest_write(blob_base, blob.data(), blob.size()) || !guest_write(sp, vectors.data(), vectors.size() * 8))
        return -EFAULT_;

    process().exe_path = program.path;
    process().start_stack = sp;
    process().cmdline.clear();
    for (auto& a : argv) {
        process().cmdline += a;
        process().cmdline.push_back('\0');
    }
    start = CpuContext{};
    start.sp = sp;
    start.pc = program.interpreter.empty() ? program.entry : interpreter.entry;
    LOGI("exec", "%s: entry %s, sp %llx", program.path.c_str(), describe_address(start.pc).c_str(),
         (unsigned long long)sp);
    return 0;
}

}  // namespace axrb

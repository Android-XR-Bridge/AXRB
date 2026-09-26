#pragma once

#include "cpu.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/* One loaded shared object. */
struct GuestObject {
    std::string name;
    GuestBytes image;
    uint64_t base = 0;
    std::vector<uint64_t> initialisers;
    std::vector<std::string> needed;
    /* The program headers, kept for dl_iterate_phdr, which is how an unwinder
       finds each object's exception tables. Guest memory is identity mapped,
       so the guest can be handed a pointer straight into this. */
    std::vector<uint8_t> program_headers;
    uint16_t program_header_count = 0;
    /* This object's own JNI_OnLoad, if it has one. Java's System.loadLibrary
       would have called it, so whoever stands in for Java must. */
    uint64_t jni_onload = 0;
    bool jni_onload_done = false;
    /* What this object itself defines, for dlsym on its handle. */
    std::unordered_map<std::string, uint64_t> symbols;

    /* Where the dynamic section pointed, all already shifted by base. */
    uint64_t symtab = 0, strtab = 0, syment = 24, symcount = 0;
    uint64_t rela = 0, relasz = 0, relaent = 24;
    uint64_t jmprel = 0, pltrelsz = 0;
    uint64_t relr = 0, relrsz = 0;
    /* Android's packed relocations (APS2), and whether this object has had
       its relocations applied: RELR adds to what is there, so applying twice
       would move every pointer twice. */
    uint64_t android_rela = 0, android_relasz = 0;
    bool relocated = false;
    bool tls_placed = false;
    /* Read only after relocation: the entries are themselves relocated. */
    uint64_t init_array = 0, init_arraysz = 0, init_one = 0;
    /* The thread local template: where it sits in the image, how much of it
       has file behind it, and where this object's block lands. */
    uint64_t tls_template = 0, tls_filesz = 0, tls_memsz = 0, tls_align = 8, tls_offset = 0;

    const char* string_at(uint64_t offset) const;
};

/* Loads objects, joins them up, and answers dlsym.

   A symbol an object asks for is looked for in every object that is loaded,
   and anything still missing is taken to be part of the C library, which is
   answered on the host side through the thunk page. */
struct GuestLinker {
    /* Loads this object and everything it says it needs. */
    bool load(const char* path, GuestMem& mem);

    /* Ties up the relocations of everything loaded so far. Safe to call again
       after a later dlopen. */
    bool relocate(GuestMem& mem);

    /* A defined symbol's address, or zero. */
    uint64_t lookup(const std::string& name) const;

    /* The thunk a host-provided name is reached through, allocating one the
       first time the name is seen. */
    uint64_t thunk_for(const std::string& name);

    /* Where to look for a DT_NEEDED library. Names we have no file for are
       treated as the system's, so their symbols fall through to the host. */
    std::vector<std::string> search;
    /* Names we deliberately do not load, because we answer them ourselves. */
    bool is_system(const std::string& soname) const;

    std::vector<GuestObject*> objects;
    std::unordered_map<std::string, uint64_t> exports;
    std::vector<std::string> imports; /* index is the thunk slot */
    std::unordered_map<std::string, int> import_slot;
    static const size_t kMaxImports = 16384;
    std::mutex thunk_lock;
    /* One thread's storage: a header, then a block per object that wanted one. */
    GuestBytes tls;
    /* The untouched template, which a new thread starts from. The block above
       belongs to whichever thread is running and has been written to. */
    std::vector<uint8_t> tls_initial;
    uint64_t tls_base = 0x60000000ull;
    uint64_t tls_used = 16; /* the two reserved words the header is */
    /* Kept so a thunk added later, by a lookup at run time, is still counted
       as part of the thunk page. */
    GuestMem* mapped = nullptr;
    /* Objects go high, clear of everything else and of the fake JNI handle
       numbers, which start at 0x4a000000. */
    uint64_t next_base = 0x2000000000ull;
    uint64_t thunk_base = 0xA0000000ull;
    std::string error;

    ~GuestLinker();
};

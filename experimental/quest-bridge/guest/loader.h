#pragma once

#include "cpu.h"
#include "linker.h"

#include <string>
#include <vector>

/* A guest program, loaded and ready to run: every object the linker brought
   in, the stack, and the registers pointed at the entry symbol. */
/* The main thread's stack: sixteen megabytes ending at 0x7f000000, just
   below Windows' shared user data page at 0x7ffe0000, which every process
   has at that fixed address. */
const uint64_t kStackBase = 0x7e000000ull;
const uint64_t kStackSize = 16u << 20;

struct GuestImage {
    GuestLinker linker;
    GuestBytes stack;
    GuestMem mem{};
    GuestCpu cpu{};
    /* Entries the toolchain wants run before anything else, dependencies first. */
    std::vector<uint64_t> initialisers;
    /* How a re-entered call reaches the same import handling. */
    GuestThunk callback = nullptr;
    void* callback_user = nullptr;

    const std::vector<std::string>& imports() const { return linker.imports; }
};

/* Loads the object and everything it needs, ties up the relocations, and sets
   the program counter at the named entry point. */
bool guest_load(GuestImage& image, const char* path, const char* entry);

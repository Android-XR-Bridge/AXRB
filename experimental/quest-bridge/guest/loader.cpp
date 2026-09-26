/* Bringing a guest program up: the linker does the work, this decides where
   the stack goes and where execution starts. */

#include "loader.h"

#include <cstdio>
#include <cstring>

namespace {

/* A real main thread gets eight megabytes, and an engine's initialisation
   uses frames of ten kilobytes at a time, so two was nowhere near enough. */

std::string directory_of(const std::string& path) {
    size_t cut = path.find_last_of("\\/");
    return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
}

}  // namespace

bool guest_load(GuestImage& image, const char* path, const char* entry) {
    image.linker.search.push_back(directory_of(path));
    if (!image.linker.load(path, image.mem)) {
        std::fprintf(stderr, "loader: %s\n", image.linker.error.c_str());
        return false;
    }
    if (!image.linker.relocate(image.mem)) return false;

    image.stack.at = kStackBase;

    image.stack.assign((size_t)kStackSize, 0);
    image.mem.map(image.stack.data(), kStackBase, kStackSize);
    image.cpu.sp = kStackBase + kStackSize - 16;
    /* The thread pointer, which the guest reads with mrs and the linker
       measured every thread local offset from. */
    image.cpu.tpidr = image.linker.tls_base;

    /* A dependency is loaded after the object that asked for it, so running
       the list backwards gets the dependencies set up first. */
    for (size_t i = image.linker.objects.size(); i > 0; --i)
        for (uint64_t initialiser : image.linker.objects[i - 1]->initialisers)
            image.initialisers.push_back(initialiser);

    uint64_t start = image.linker.lookup(entry);
    if (!start) {
        std::fprintf(stderr, "loader: %s has no %s\n", path, entry);
        return false;
    }
    image.cpu.pc = start;
    image.cpu.x[30] = 1; /* the sentinel guest_run stops on */
    std::printf("loader: %zu objects, %zu imports, %zu initialisers, %s at %llx\n", image.linker.objects.size(),
                image.linker.imports.size(), image.initialisers.size(), entry, (unsigned long long)start);
    std::fflush(stdout);
    return true;
}

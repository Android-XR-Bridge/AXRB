// The smallest end-to-end check: the kernel-style exec of linker64, bionic's
// own startup, stdio and the exit path.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
    printf("hello from arm64 bionic, pid %d, argc %d, argv[0] %s\n", getpid(), argc, argv[0]);
    char* p = (char*)malloc(1 << 20);
    memset(p, 0x5a, 1 << 20);
    printf("malloc ok: %p %d\n", (void*)p, p[12345]);
    free(p);
    return 42;
}

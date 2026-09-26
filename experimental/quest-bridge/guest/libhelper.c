/* A second object, so the linker has something to join up.

   It exports a function, a piece of data, and a function pointer that only
   holds the right address once the relocation for it has been applied. */

#include <stdlib.h>
#include <string.h>

volatile int helper_counter = 0;

__attribute__((constructor)) static void helper_start(void) { helper_counter += 100; }

int helper_add(int a, int b) {
    helper_counter += 1;
    return a + b;
}

const char* helper_name(void) { return "helper"; }

/* A pointer held in data, which needs a relocation to point anywhere. */
int (*helper_indirect)(int, int) = helper_add;

/* Something that uses the heap, so both objects share one allocator. */
char* helper_copy(const char* text) {
    char* out = (char*)malloc(strlen(text) + 1);
    strcpy(out, text);
    return out;
}

/* Only reachable through dlsym, never named by the other object. */
int helper_secret(void) { return 0xfeed; }

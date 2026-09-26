/* Two objects that have to be joined up, and a symbol found by name at run
   time. Everything here goes through the linker rather than around it. */

#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

extern void qb_check(const char* name, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);

#define CHECK(name, got, want) qb_check(name, (uint64_t)(got), 0, (uint64_t)(want), 0)

/* Defined in the other library, so each of these needs resolving across it. */
extern int helper_add(int a, int b);
extern const char* helper_name(void);
extern char* helper_copy(const char* text);
extern volatile int helper_counter;
extern int (*helper_indirect)(int, int);

void qb_guest_main(void) {
    /* A call into the sibling, which is a jump slot pointed at its code. */
    CHECK("a call across objects", helper_add(20, 22), 42);

    /* Its constructor ran, and ours sees the result. */
    CHECK("the other library was set up first", helper_counter >= 100, 1);

    /* Data in the sibling, reached through a global offset entry. */
    int before = helper_counter;
    helper_add(1, 1);
    CHECK("shared data is one object", helper_counter, before + 1);

    /* A returned pointer into the sibling's own read only data. */
    CHECK("a string from the other library", strcmp(helper_name(), "helper"), 0);

    /* A function pointer that lived in data and needed relocating. */
    CHECK("a relocated function pointer", helper_indirect(7, 8), 15);

    /* One heap, shared by both objects. */
    char* copied = helper_copy("across");
    CHECK("the other library allocated from the same heap", strcmp(copied, "across"), 0);

    /* Found by name at run time rather than linked against. */
    void* handle = dlopen("libhelper.so", 2 /* RTLD_NOW */);
    CHECK("dlopen found the library", handle != 0, 1);
    int (*secret)(void) = (int (*)(void))dlsym(handle, "helper_secret");
    CHECK("dlsym found a symbol never named here", secret != 0, 1);
    if (secret) CHECK("and calling it works", secret(), 0xfeed);
    int (*again)(int, int) = (int (*)(int, int))dlsym(handle, "helper_add");
    CHECK("dlsym agrees with the linker", again == helper_add, 1);
    CHECK("dlsym on a name nobody defines", dlsym(handle, "no_such_symbol"), 0);
}

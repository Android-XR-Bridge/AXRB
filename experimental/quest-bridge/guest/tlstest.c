/* Thread local storage, which C++ and bionic both lean on heavily even in a
   program that never starts a second thread. */

#include <stdint.h>
#include <string.h>

extern void qb_check(const char* name, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);

#define CHECK(name, got, want) qb_check(name, (uint64_t)(got), 0, (uint64_t)(want), 0)

/* Initialised from the template the loader has to copy. */
__thread int counter = 7;
__thread long long wide = 0x1122334455667788ll;
__thread char text[16] = "thread";

/* Zero filled, which comes from the part of the segment with no file behind it. */
__thread int blank[8];

static int* slow_address(void) { return &counter; }

void qb_guest_main(void) {
    CHECK("an int from the template", counter, 7);
    CHECK("a wide value from the template", wide, 0x1122334455667788ll);
    CHECK("an array from the template", strcmp(text, "thread"), 0);
    CHECK("the zero filled part is zero", blank[5], 0);

    counter += 35;
    CHECK("writing then reading", counter, 42);

    blank[3] = 99;
    CHECK("writing the zero filled part", blank[3], 99);

    /* Taking the address has to agree with reading it directly. */
    int* address = slow_address();
    *address = 1234;
    CHECK("the address is the same storage", counter, 1234);

    strcpy(text, "changed");
    CHECK("writing an array", strcmp(text, "changed"), 0);
}

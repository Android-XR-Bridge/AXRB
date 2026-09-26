/* A guest built the ordinary way: linked against bionic, with a heap, string
   and memory routines, math, and the startup the toolchain puts in by itself.
   Nothing here is written to avoid the C library; that is the whole point. */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void qb_check(const char* name, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);

#define CHECK(name, got, want) qb_check(name, (uint64_t)(got), 0, (uint64_t)(want), 0)

static uint32_t bits_of(float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    return bits;
}

/* A constructor, which the toolchain records in init_array and the loader has
   to run before anything else. */
static volatile int g_constructed;
__attribute__((constructor)) static void set_up(void) { g_constructed += 0x1234; }

static int compare_ints(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }

void qb_guest_main(void) {
    CHECK("the constructor ran", g_constructed, 0x1234);

    /* The heap */
    char* text = (char*)malloc(64);
    CHECK("malloc returned something", text != NULL, 1);
    strcpy(text, "hello");
    CHECK("strlen", strlen(text), 5);
    strcat(text, ", world");
    CHECK("strcat then strlen", strlen(text), 12);
    CHECK("strcmp", strcmp(text, "hello, world"), 0);
    CHECK("strchr", *strchr(text, 'w'), 'w');

    char* copy = (char*)calloc(32, 1);
    CHECK("calloc zeroes", copy[7], 0);
    memcpy(copy, text, 13);
    CHECK("memcpy", memcmp(copy, text, 13), 0);
    memset(copy, 'x', 4);
    CHECK("memset", copy[3], 'x');
    CHECK("memmove overlapping", (memmove(copy + 1, copy, 8), copy[1]), 'x');
    free(copy);

    int* numbers = (int*)malloc(8 * sizeof(int));
    for (int i = 0; i < 8; ++i) numbers[i] = (i * 37) % 11;
    qsort(numbers, 8, sizeof(int), compare_ints);
    CHECK("qsort put the smallest first", numbers[0] <= numbers[7], 1);
    free(numbers);

    char* grown = (char*)realloc(text, 256);
    CHECK("realloc kept the contents", strcmp(grown, "hello, world"), 0);
    free(grown);

    /* Formatting, which pulls in a large part of the library by itself */
    char line[64];
    snprintf(line, sizeof(line), "%d %s %.2f", 42, "ok", 1.5);
    CHECK("snprintf", strcmp(line, "42 ok 1.50"), 0);

    /* Maths */
    CHECK("sqrtf", bits_of(sqrtf(16.0f)), bits_of(4.0f));
    CHECK("floorf", bits_of(floorf(2.7f)), bits_of(2.0f));
    CHECK("fabsf", bits_of(fabsf(-3.25f)), bits_of(3.25f));
    CHECK("sinf is near zero at zero", fabsf(sinf(0.0f)) < 0.0001f, 1);
    CHECK("cosf is near one at zero", fabsf(cosf(0.0f) - 1.0f) < 0.0001f, 1);
    CHECK("powf", bits_of(powf(2.0f, 10.0f)), bits_of(1024.0f));
    CHECK("atan2f quarter turn", fabsf(atan2f(1.0f, 0.0f) - 1.5707964f) < 0.0001f, 1);

    /* String to number, which real code uses for parsing anything */
    CHECK("strtol", strtol("  -1234xyz", NULL, 10), -1234);
    CHECK("atoi", atoi("77"), 77);
}

/* Threads: several running at once over shared memory, a lock that has to
   actually exclude, and thread local storage that must not be shared. */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void qb_check(const char* name, uint64_t got_lo, uint64_t got_hi, uint64_t want_lo, uint64_t want_hi);

#define CHECK(name, got, want) qb_check(name, (uint64_t)(got), 0, (uint64_t)(want), 0)

/* Each thread gets its own copy of this, so the counts must not mix. */
__thread int mine = 0;

static int shared_unguarded = 0;
static int shared_guarded = 0;
static pthread_mutex_t guard = PTHREAD_MUTEX_INITIALIZER;

static void* count_up(void* argument) {
    long which = (long)argument;
    for (int i = 0; i < 2000; ++i) {
        mine += 1;
        shared_unguarded += 1;
        pthread_mutex_lock(&guard);
        shared_guarded += 1;
        pthread_mutex_unlock(&guard);
    }
    /* Returning the thread's own count proves the storage was not shared. */
    return (void*)(long)(mine + which * 0);
}

static void* allocate_lots(void* argument) {
    (void)argument;
    long total = 0;
    for (int i = 0; i < 200; ++i) {
        char* block = (char*)malloc(64);
        if (!block) return (void*)-1;
        memset(block, 'a', 64);
        total += block[10];
        free(block);
    }
    return (void*)total;
}

static pthread_once_t once_flag = PTHREAD_ONCE_INIT;
static int once_count = 0;
static void once_body(void) { once_count += 1; }

void qb_guest_main(void) {
    /* The main thread has its own copy too. */
    mine = 5;

    pthread_t workers[4];
    for (long i = 0; i < 4; ++i) CHECK("starting a thread", pthread_create(&workers[i], 0, count_up, (void*)i), 0);

    void* results[4];
    for (int i = 0; i < 4; ++i) CHECK("joining a thread", pthread_join(workers[i], &results[i]), 0);

    for (int i = 0; i < 4; ++i) CHECK("each thread kept its own count", (long)results[i], 2000);
    CHECK("the main thread's copy is untouched", mine, 5);
    CHECK("the guarded total is exact", shared_guarded, 4 * 2000);

    /* The unguarded one is allowed to be wrong; it just must not exceed the
       total, which would mean memory was not being shared at all. */
    CHECK("the unguarded total is no larger than the truth", shared_unguarded <= 4 * 2000, 1);
    CHECK("and the threads really did share it", shared_unguarded > 2000, 1);

    /* One heap, used from several threads at once. */
    pthread_t hungry[3];
    for (int i = 0; i < 3; ++i) pthread_create(&hungry[i], 0, allocate_lots, 0);
    for (int i = 0; i < 3; ++i) {
        void* result = 0;
        pthread_join(hungry[i], &result);
        CHECK("allocating from several threads", (long)result, 200 * 'a');
    }

    pthread_once(&once_flag, once_body);
    pthread_once(&once_flag, once_body);
    CHECK("once runs once", once_count, 1);

    pthread_key_t key;
    CHECK("making a key", pthread_key_create(&key, 0), 0);
    CHECK("a key starts empty", pthread_getspecific(key), 0);
    pthread_setspecific(key, (void*)0x1234);
    CHECK("a key remembers", pthread_getspecific(key), 0x1234);
}

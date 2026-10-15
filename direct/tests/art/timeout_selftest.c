#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static _Atomic int word;
static void* wake_later(void* unused) {
    (void)unused;
    struct timespec pause = {0, 50000000};
    nanosleep(&pause, 0);
    atomic_store(&word, 1);
    syscall(SYS_futex, &word, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0);
    return 0;
}
static int check_wait(int op, struct timespec deadline) {
    atomic_store(&word, 0);
    pthread_t worker;
    if (pthread_create(&worker, 0, wake_later, 0)) return 1;
    errno = 0;
    long result = syscall(SYS_futex, &word, op, 0, &deadline, 0, FUTEX_BITSET_MATCH_ANY);
    int error = errno, woke = atomic_load(&word);
    pthread_join(worker, 0);
    if (!woke || (result != 0 && error != EAGAIN)) {
        fprintf(stderr, "long wait failed: op=%x sec=%lld result=%ld errno=%d woke=%d\n",
                op, (long long)deadline.tv_sec, result, error, woke);
        return 1;
    }
    return 0;
}
int main(void) {
    struct timespec mono, real;
    clock_gettime(CLOCK_MONOTONIC, &mono);
    clock_gettime(CLOCK_REALTIME, &real);
    mono.tv_sec += 946080000;  // 30-year parking deadline.
    real.tv_sec += 946080000;
    if (check_wait(FUTEX_WAIT_BITSET_PRIVATE, mono)) return 1;
    if (check_wait(FUTEX_WAIT_BITSET_PRIVATE | FUTEX_CLOCK_REALTIME, real)) return 2;
    if (check_wait(FUTEX_WAIT_PRIVATE, (struct timespec){946080000, 123})) return 3;
    if (check_wait(FUTEX_WAIT_BITSET_PRIVATE, (struct timespec){INT64_MAX, 999999999})) return 4;
    atomic_store(&word, 0);
    struct timespec short_wait = {0, 20000000}, start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (syscall(SYS_futex, &word, FUTEX_WAIT_PRIVATE, 0, &short_wait, 0, 0) != -1 || errno != ETIMEDOUT) return 5;
    clock_gettime(CLOCK_MONOTONIC, &end);
    int64_t ns = (end.tv_sec-start.tv_sec)*1000000000LL + end.tv_nsec-start.tv_nsec;
    if (ns < 15000000 || ns > 2000000000LL) return 6;
    struct timespec invalid = {-1, 0};
    if (syscall(SYS_futex, &word, FUTEX_WAIT_PRIVATE, 0, &invalid, 0, 0) != -1 || errno != EINVAL) return 7;
    struct timespec expired = {0, 0};
    if (syscall(SYS_futex, &word, FUTEX_WAIT_BITSET_PRIVATE, 0, &expired, 0, FUTEX_BITSET_MATCH_ANY) != -1 || errno != ETIMEDOUT) return 8;
    puts("TIMEOUT_SELFTEST_PASS");
    return 0;
}

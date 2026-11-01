/* macOS counterparts of the Linux/POSIX calls the runtime uses. Included through runtime.h
 * after the C library headers, like compat_win.h. Everything here is a Darwin substitution for
 * something libSystem lacks; nothing changes behaviour on other platforms. */
#ifndef BB_COMPAT_DARWIN_H
#define BB_COMPAT_DARWIN_H
#ifdef __APPLE__
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

/* struct stat timestamps are st_atimespec/st_mtimespec/st_ctimespec. */
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
/* glibc spells the static recursive-mutex initializer with _NP. */
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#endif
/* No coarse clock; the second-resolution PS4 clock only needs seconds. */
#define CLOCK_REALTIME_COARSE CLOCK_REALTIME

static inline void bb_darwin_poll_pause(void) {
    struct timespec pause = {0, 50000}; /* 50 us */
    nanosleep(&pause, NULL);
}
static inline int bb_darwin_expired(const struct timespec *deadline) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return now.tv_sec > deadline->tv_sec || (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}
/* libSystem has no pthread_mutex_timedlock / pthread_rwlock_timed*lock: poll the try-lock until
 * the CLOCK_REALTIME deadline (the same clock the callers build their deadlines from). */
static inline int bb_pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline) {
    for (;;) {
        int e = pthread_mutex_trylock(mutex);
        if (e != EBUSY) return e;
        if (bb_darwin_expired(deadline)) return ETIMEDOUT;
        bb_darwin_poll_pause();
    }
}
static inline int bb_pthread_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *deadline) {
    for (;;) {
        int e = pthread_rwlock_tryrdlock(lock);
        if (e != EBUSY) return e;
        if (bb_darwin_expired(deadline)) return ETIMEDOUT;
        bb_darwin_poll_pause();
    }
}
static inline int bb_pthread_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *deadline) {
    for (;;) {
        int e = pthread_rwlock_trywrlock(lock);
        if (e != EBUSY) return e;
        if (bb_darwin_expired(deadline)) return ETIMEDOUT;
        bb_darwin_poll_pause();
    }
}
/* libSystem has no pthread_barrier_*: a mutex + condvar barrier (used by tests/test_runtime.c). */
typedef struct { pthread_mutex_t mutex; pthread_cond_t cond; unsigned count, waiting, generation; } bb_pthread_barrier_t;
#define pthread_barrier_t bb_pthread_barrier_t
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)
static inline int bb_pthread_barrier_init(bb_pthread_barrier_t *b, const void *attr, unsigned count) {
    (void)attr;
    if (!count) return EINVAL;
    b->count = count; b->waiting = 0; b->generation = 0;
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);
    return 0;
}
static inline int bb_pthread_barrier_destroy(bb_pthread_barrier_t *b) {
    pthread_cond_destroy(&b->cond);
    pthread_mutex_destroy(&b->mutex);
    return 0;
}
static inline int bb_pthread_barrier_wait(bb_pthread_barrier_t *b) {
    pthread_mutex_lock(&b->mutex);
    const unsigned generation = b->generation;
    if (++b->waiting == b->count) {
        b->waiting = 0; ++b->generation;
        pthread_cond_broadcast(&b->cond);
        pthread_mutex_unlock(&b->mutex);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (generation == b->generation) pthread_cond_wait(&b->cond, &b->mutex);
    pthread_mutex_unlock(&b->mutex);
    return 0;
}
#define pthread_barrier_init bb_pthread_barrier_init
#define pthread_barrier_destroy bb_pthread_barrier_destroy
#define pthread_barrier_wait bb_pthread_barrier_wait
/* macOS has no MAP_FIXED_NOREPLACE. mmap without MAP_FIXED takes the address as a hint and honours
 * it when the range is free (tested under Rosetta: 0x400000 and the low ranges). Returns the mapping
 * only if it landed exactly at `wanted`; otherwise it is unmapped again and MAP_FAILED is returned,
 * so nothing that exists is ever replaced. */
static inline void *bb_darwin_map_at(void *wanted, size_t size, int prot) {
    void *got = mmap(wanted, size, prot, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (got == MAP_FAILED || got == wanted) return got;
    munmap(got, size);
    return MAP_FAILED;
}
#define pthread_mutex_timedlock bb_pthread_mutex_timedlock
#define pthread_rwlock_timedrdlock bb_pthread_rwlock_timedrdlock
#define pthread_rwlock_timedwrlock bb_pthread_rwlock_timedwrlock
/* pthread_cond_timedwait on macOS always measures CLOCK_REALTIME: there is no condattr clock.
 * runtime_sema.c builds its deadlines with CLOCK_REALTIME on Darwin (WAIT_CLOCK). */
#define pthread_condattr_setclock(attr, clock) ((void)(attr), (void)(clock), 0)
#endif
#endif

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

/* MACOS-STUB(M2): macOS has no MAP_FIXED_NOREPLACE. The flag is 0, so mmap treats the address as a
 * hint (never replacing a mapping) and every call site checks that the result equals the request
 * (bb_darwin_hint_missed). M2 verifies this under Rosetta and replaces it if the hint is not honoured. */
#define MAP_FIXED_NOREPLACE 0

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
/* True (after unmapping) when mmap placed a hinted mapping somewhere else than requested. */
static inline int bb_darwin_hint_missed(void *got, void *wanted, size_t size) {
    if (got == MAP_FAILED || got == wanted) return 0;
    munmap(got, size);
    return 1;
}
#define pthread_mutex_timedlock bb_pthread_mutex_timedlock
#define pthread_rwlock_timedrdlock bb_pthread_rwlock_timedrdlock
#define pthread_rwlock_timedwrlock bb_pthread_rwlock_timedwrlock
/* pthread_cond_timedwait on macOS always measures CLOCK_REALTIME: there is no condattr clock.
 * runtime_sema.c builds its deadlines with CLOCK_REALTIME on Darwin (WAIT_CLOCK). */
#define pthread_condattr_setclock(attr, clock) ((void)(attr), (void)(clock), 0)
#endif
#endif

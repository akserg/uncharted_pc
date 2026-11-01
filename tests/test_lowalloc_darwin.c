/* The low allocator (src/runtime_lowalloc.c): every block the guest could be handed lies below 1 TiB
 * (the guest packs pointers into 40 bits), from many threads at once, for plain, zeroed, 64-byte
 * aligned and large blocks, with frees interleaved; blocks never overlap and memory is reused.
 * The guest-visible objects the runtime really creates (mutex, cond, rwlock, thread handles,
 * TLS blocks) are checked as well. macOS only (build.sh --test). */
#include "runtime.h"
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __APPLE__
int main(void) { puts("SKIP: macOS only"); return 0; }
#else
void runtime_restart(void) { abort(); }

#define LIMIT (UINT64_C(1) << 40)
#define THREADS 16
#define ROUNDS 4000
static pthread_barrier_t barrier;
static uint64_t highest[THREADS];
static uint64_t seed_of(int t) { return UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)(t + 1); }
static uint32_t next(uint64_t *s) { *s = *s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(*s >> 33); }

static void *worker(void *arg) {
    const int id = (int)(intptr_t)arg;
    uint64_t seed = seed_of(id);
    struct { unsigned char *p; size_t n; unsigned char tag; } held[64] = {0};
    pthread_barrier_wait(&barrier);
    for (int round = 0; round < ROUNDS; ++round) {
        const unsigned slot = next(&seed) % 64;
        if (held[slot].p) { /* verify nobody scribbled on it, then free it */
            for (size_t i = 0; i < held[slot].n; i += 61) assert(held[slot].p[i] == held[slot].tag);
            bb_guest_free(held[slot].p);
            held[slot].p = NULL;
            continue;
        }
        const unsigned kind = next(&seed) % 10;
        size_t n = kind < 6 ? 1 + next(&seed) % 300 : kind < 8 ? 64 + next(&seed) % 5000 : kind < 9 ? 30000 + next(&seed) % 40000 : 70000 + next(&seed) % 300000;
        unsigned char *p;
        if (kind == 3) {
            p = bb_guest_aligned_alloc(64, n);
            assert(p && ((uintptr_t)p & 63) == 0);
        } else if (kind == 4) {
            p = bb_guest_calloc(1, n);
            assert(p);
            for (size_t i = 0; i < n; i += 13) assert(p[i] == 0);
        } else {
            p = bb_guest_malloc(n);
            assert(p && ((uintptr_t)p & 15) == 0);
        }
        assert((uint64_t)(uintptr_t)p + n <= LIMIT);
        if ((uint64_t)(uintptr_t)p > highest[id]) highest[id] = (uint64_t)(uintptr_t)p;
        held[slot].p = p; held[slot].n = n; held[slot].tag = (unsigned char)(1 + id);
        memset(p, held[slot].tag, n);
    }
    for (unsigned s = 0; s < 64; ++s)
        if (held[s].p) {
            for (size_t i = 0; i < held[s].n; i += 61) assert(held[s].p[i] == held[s].tag);
            bb_guest_free(held[s].p);
        }
    return NULL;
}

static void *handle_thread(void *arg) { (void)arg; return NULL; }

int main(void) {
    runtime_start(1);
    const uint64_t before = runtime_low_live();
    pthread_t threads[THREADS];
    pthread_barrier_init(&barrier, NULL, THREADS);
    for (intptr_t t = 0; t < THREADS; ++t) assert(pthread_create(&threads[t], NULL, worker, (void *)t) == 0);
    for (int t = 0; t < THREADS; ++t) pthread_join(threads[t], NULL);
    uint64_t top = 0;
    for (int t = 0; t < THREADS; ++t) if (highest[t] > top) top = highest[t];
    assert(top < LIMIT && top >= UINT64_C(0x800000000)); /* inside the low range runtime_low_map serves */
    assert(runtime_low_live() == before);                /* every block was freed */
    printf("lowalloc: %d threads x %d rounds, highest block %#" PRIx64 " (< 2^40), %" PRIu64 " live after\n",
           THREADS, ROUNDS, top, runtime_low_live() - before);

    /* A freed small block is handed out again. */
    void *a = bb_guest_malloc(100); bb_guest_free(a);
    void *b = bb_guest_malloc(100);
    assert(a == b);
    bb_guest_free(b);

    /* The malloc heap itself is NOT low under Rosetta; the guest-visible objects must not come from it. */
    void *heap = malloc(64);
    printf("lowalloc: plain malloc gives %p (the reason for this allocator)\n", heap);
    free(heap);

    /* Real guest-visible objects: scePthreadMutexInit/CondInit/RwlockInit, scePthreadCreate, Self. */
    typedef int32_t (ABI *Init3)(void **, void *, const char *);
    typedef int32_t (ABI *Create)(void **, void *, void *(ABI *)(void *), void *, const char *);
    typedef int32_t (ABI *Join)(void *, void **);
    typedef void *(ABI *Self)(void);
    Init3 mutex_init = (Init3)runtime_resolve("cmo1RIYva9o#p#J", 0), rw_init = (Init3)runtime_resolve("6ULAa0fq4jA#p#J", 0);
    Init3 cond_init = (Init3)runtime_resolve("2Tb92quprl0#p#J", 0);
    Create create = (Create)runtime_resolve("6UgtwV+0zb4#p#J", 0);
    Join join = (Join)runtime_resolve("onNY9Byn-W8#p#J", 0);
    Self self = (Self)runtime_resolve("aI+OeCz8xrQ#p#J", 0);
    assert(mutex_init && rw_init && cond_init && create && join && self);
    void *mutex = NULL, *rw = NULL, *cond = NULL, *thread = NULL;
    assert(mutex_init(&mutex, NULL, "m") == 0 && rw_init(&rw, NULL, "r") == 0 && cond_init(&cond, NULL, "c") == 0);
    assert(create(&thread, NULL, (void *(ABI *)(void *))handle_thread, NULL, "low") == 0);
    void *main_handle = self(); /* attaches this thread: TLS block + GuestThread */
    assert(join(thread, NULL) == 0);
    void *objects[] = {mutex, rw, cond, thread, main_handle};
    const char *names[] = {"mutex", "rwlock", "cond", "thread", "main thread"};
    for (unsigned i = 0; i < sizeof(objects) / sizeof(*objects); ++i) {
        printf("lowalloc: guest-visible %s handle %p\n", names[i], objects[i]);
        assert((uint64_t)(uintptr_t)objects[i] < LIMIT && objects[i]);
    }
    puts("PASS: low allocator (16 threads, plain/zeroed/aligned/large blocks, all below 2^40; mutex/cond/rwlock/thread handles low)");
    return 0;
}
#endif

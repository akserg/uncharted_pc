/* Guest TLS on macOS (docs/macos/M3-guest-boots.md task 6). GS is the pthread TSD base: the TCB set
 * by the runtime (set_gs = pthread_setspecific) is what `mov rax, gs:[key*8]` reads, per thread.
 * Runs, under Rosetta: the displacement from runtime_thread_tcb_offset(); inline-asm reads of
 * gs:[offset] from the main thread and from guest threads created through scePthreadCreate; and the
 * exact 9-byte instruction probe.c patch_tcb_loads writes (65 48 8b 04 25 disp32), executed from
 * RWX memory on several threads at once. Each thread must see its own TCB: tcb_self == tcb and
 * tcb_thread == that thread's handle, all distinct, all below 1 TiB. macOS only. */
#include "runtime.h"
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef __APPLE__
int main(void) { puts("SKIP: macOS only"); return 0; }
#else
void runtime_restart(void) { abort(); }
uint32_t runtime_thread_tcb_offset(void);

typedef int32_t (ABI *Create)(void **, void *, void *(ABI *)(void *), void *, const char *);
typedef int32_t (ABI *Join)(void *, void **);
typedef void *(ABI *Self)(void);
typedef uint64_t (*Load)(void);
static Create create_thread; static Join join_thread; static Self self_handle;
static uint32_t offset;
static Load patched_load; /* mov rax, gs:[offset]; ret */

static uint64_t gs_load(uint32_t displacement) {
    uint64_t value;
    __asm__ volatile("movq %%gs:(%1), %0" : "=r"(value) : "r"((uint64_t)displacement) : "memory");
    return value;
}
#define THREADS 12
typedef struct { uint64_t tcb_asm, tcb_patched, tcb_again, self_field, thread_field; void *handle; } Seen;
static Seen seen[THREADS + 1];
static pthread_barrier_t barrier;
static volatile unsigned failures;

static void observe(Seen *s) {
    s->handle = self_handle();
    s->tcb_asm = gs_load(offset);
    s->tcb_patched = patched_load();
    pthread_barrier_wait(&barrier); /* every thread has set its TCB before anyone re-reads */
    for (int i = 0; i < 1000; ++i) sched_yield();
    s->tcb_again = gs_load(offset);
    const uint64_t *tcb = (const uint64_t *)(uintptr_t)s->tcb_asm;
    s->self_field = tcb[0]; s->thread_field = tcb[2];
}
static void *ABI guest_main(void *arg) { observe(arg); return NULL; }

int main(void) {
    runtime_start(1);
    offset = runtime_thread_tcb_offset();
    assert(offset == runtime_thread_tcb_offset() && offset % 8 == 0 && offset > 0);
    printf("tls: pthread key slot -> displacement gs:[%#x] (key %u)\n", offset, offset / 8);
    create_thread = (Create)runtime_resolve("6UgtwV+0zb4#p#J", 0);
    join_thread = (Join)runtime_resolve("onNY9Byn-W8#p#J", 0);
    self_handle = (Self)runtime_resolve("aI+OeCz8xrQ#p#J", 0);
    assert(create_thread && join_thread && self_handle);

    /* The instruction patch_tcb_loads produces, in executable memory. */
    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(code != MAP_FAILED);
    static const unsigned char load[] = {0x65, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0};
    memcpy(code, load, sizeof(load));
    memcpy(code + 5, &offset, 4);
    code[sizeof(load)] = 0xc3;
    patched_load = (Load)(void *)code;

    pthread_barrier_init(&barrier, NULL, THREADS + 1);
    void *threads[THREADS];
    for (int i = 0; i < THREADS; ++i) assert(create_thread(&threads[i], NULL, guest_main, &seen[i], "tls") == 0);
    observe(&seen[THREADS]); /* this thread becomes a host-owned guest thread through scePthreadSelf */
    for (int i = 0; i < THREADS; ++i) assert(join_thread(threads[i], NULL) == 0);

    for (int i = 0; i <= THREADS; ++i) {
        const Seen *s = &seen[i];
        if (!s->tcb_asm || s->tcb_asm != s->tcb_patched || s->tcb_asm != s->tcb_again || s->self_field != s->tcb_asm ||
            s->thread_field != (uint64_t)(uintptr_t)s->handle || s->tcb_asm >> 40) {
            printf("FAIL thread %d: asm %#" PRIx64 " patched %#" PRIx64 " again %#" PRIx64 " self %#" PRIx64 " thread %#" PRIx64 " handle %p\n",
                   i, s->tcb_asm, s->tcb_patched, s->tcb_again, s->self_field, s->thread_field, s->handle);
            ++failures;
        }
        for (int j = 0; j < i; ++j) if (seen[j].tcb_asm == s->tcb_asm) { printf("FAIL threads %d and %d share a TCB\n", i, j); ++failures; }
    }
    if (failures) return 1;
    printf("tls: %d threads each read their own TCB through gs:[%#x] (asm and the patched 9-byte load); sample TCB %#" PRIx64 "\n",
           THREADS + 1, offset, seen[0].tcb_asm);
    puts("PASS: guest TLS (pthread TSD key, gs:[key*8] per thread, patched load executes under Rosetta)");
    return 0;
}
#endif

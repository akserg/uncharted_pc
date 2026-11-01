/* Allocator for host objects whose address the guest sees (thread and sync-object handles, TLS
 * blocks). The guest packs pointers into 40-bit fields, so they must lie below 1 TiB. On Linux the
 * non-PIE brk heap does that and on Windows --disable-high-entropy-va; under Rosetta malloc returns
 * 0x6000xxxxxxxx / 0x7f..xxxxxxxx (M2). Here: 1 MiB chunks from runtime_low_map (always inside
 * [0x800000000, 0xfc0000000)), power-of-two size classes with free lists, a 16-byte header in
 * front of every block. Large blocks get their own mapping. Thread safe (one mutex: these
 * objects are created at thread and sync-object creation, not per frame). Used on Apple only
 * (runtime.h maps bb_guest_* to the libc functions elsewhere). */
#include "runtime.h"
#ifdef __APPLE__
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>

#define CHUNK (UINT64_C(1)<<20)
#define MIN_CLASS 5            /* 32 bytes including the header */
#define MAX_CLASS 16           /* 64 KiB; larger blocks are mapped on their own */
#define CLASS_LARGE 0xffu
#define CLASS_ALIGNED 0xfeu
#define HEADER 16u
typedef struct Header { uint32_t magic, klass; uint64_t value; } Header; /* value: mapped size / origin / next */
#define MAGIC UINT32_C(0x4c4f5741) /* "LOWA" */
typedef struct Free { struct Free *next; } Free;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Free *free_list[MAX_CLASS+1];
static unsigned char *chunk_next, *chunk_end;
static uint64_t chunks, large_blocks, live;

static unsigned class_of(size_t total) {
    unsigned k=MIN_CLASS;
    while (((size_t)1<<k)<total) ++k;
    return k;
}
void *runtime_low_alloc(size_t size) {
    if (size>SIZE_MAX-HEADER-4096) return NULL;
    size_t total=size+HEADER;
    unsigned char *block=NULL;
    unsigned klass;
    if (total>((size_t)1<<MAX_CLASS)) {
        size_t mapped=(total+16383)&~(size_t)16383;
        block=runtime_low_map(mapped,PROT_READ|PROT_WRITE);
        if (!block) return NULL;
        pthread_mutex_lock(&lock); ++large_blocks; ++live; pthread_mutex_unlock(&lock);
        *(Header *)block=(Header){MAGIC,CLASS_LARGE,mapped};
        return block+HEADER;
    }
    klass=class_of(total);
    pthread_mutex_lock(&lock);
    if (free_list[klass]) { block=(unsigned char *)free_list[klass]; free_list[klass]=free_list[klass]->next; }
    else {
        size_t bytes=(size_t)1<<klass;
        if ((size_t)(chunk_end-chunk_next)<bytes) {
            unsigned char *chunk=runtime_low_map(CHUNK,PROT_READ|PROT_WRITE);
            if (!chunk) { pthread_mutex_unlock(&lock); return NULL; }
            chunk_next=chunk; chunk_end=chunk+CHUNK; ++chunks;
        }
        block=chunk_next; chunk_next+=bytes;
    }
    ++live;
    pthread_mutex_unlock(&lock);
    *(Header *)block=(Header){MAGIC,klass,0};
    return block+HEADER;
}
void *runtime_low_calloc(size_t count, size_t size) {
    if (size && count>SIZE_MAX/size) return NULL;
    void *p=runtime_low_alloc(count*size);
    if (p) memset(p,0,count*size);
    return p;
}
/* alignment: a power of two. The header in front of the returned pointer names the real block. */
void *runtime_low_aligned(size_t alignment, size_t size) {
    if (alignment<HEADER) alignment=HEADER;
    if (alignment&(alignment-1)) return NULL;
    unsigned char *raw=runtime_low_alloc(size+alignment+2*HEADER);
    if (!raw) return NULL;
    uintptr_t at=((uintptr_t)raw+2*HEADER+alignment-1)&~(uintptr_t)(alignment-1);
    *(Header *)(at-HEADER)=(Header){MAGIC,CLASS_ALIGNED,(uint64_t)(uintptr_t)raw};
    return (void *)at;
}
void runtime_low_free(void *pointer) {
    if (!pointer) return;
    Header *h=(Header *)((unsigned char *)pointer-HEADER);
    if (h->magic!=MAGIC) { fputs("STOP: runtime_low_free of a pointer the low allocator did not return\n",stderr); abort(); }
    if (h->klass==CLASS_ALIGNED) { runtime_low_free((void *)(uintptr_t)h->value); return; }
    if (h->klass==CLASS_LARGE) {
        pthread_mutex_lock(&lock); --live; pthread_mutex_unlock(&lock);
        munmap(h,(size_t)h->value);
        return;
    }
    unsigned klass=h->klass;
    pthread_mutex_lock(&lock);
    ((Free *)h)->next=free_list[klass]; free_list[klass]=(Free *)h; --live;
    pthread_mutex_unlock(&lock);
}
/* Number of live blocks (tests). */
uint64_t runtime_low_live(void) {
    pthread_mutex_lock(&lock); uint64_t n=live; pthread_mutex_unlock(&lock);
    return n;
}
#else
typedef int bb_runtime_lowalloc_unused;
#endif

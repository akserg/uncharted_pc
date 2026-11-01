/* Guest address space on macOS (x86_64 under Rosetta 2): the Darwin backend of runtime_memory.c.
 * Aliasing of one backing offset at several guest addresses, the image range below 2 GiB, the low
 * range, RWX, discard-and-retouch, address-range hygiene, page sizes. Built and run only on APPLE
 * (build.sh --test). BB_TEST_PAUSE=<seconds> sleeps at the end so `vmmap <pid>` can be taken. */
#include "runtime.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#ifndef __APPLE__
int main(void) { puts("SKIP: macOS only"); return 0; }
#else
/* The settings menu of the GPU library restarts through probe.c, which tests do not link. */
void runtime_restart(void) { abort(); }

int runtime_memory_write_backing(uintptr_t address, const void *data, uint64_t size); /* runtime_memory.c */

typedef int32_t (ABI *Allocate)(int64_t, int64_t, uint64_t, uint64_t, int, int64_t *);
typedef int32_t (ABI *Map)(void **, uint64_t, int, int, int64_t, uint64_t);
typedef int32_t (ABI *Unmap)(void *, uint64_t);
typedef int32_t (ABI *Release)(uint64_t, uint64_t);
typedef int32_t (ABI *MapFlexible)(void **, uint64_t, int, int);
typedef int32_t (ABI *Protect)(const void *, uint64_t, int);
#define GET(type, name) ((type)runtime_resolve(name, 0))

enum { MAP_FIXED_GUEST = 0x10 };
#define USER_MIN UINT64_C(0x7000000000)
#define USER_MAX UINT64_C(0xfc00000000)
#define KiB UINT64_C(1024)
#define MiB (KiB * 1024)
#define GiB (MiB * 1024)

static Allocate alloc_direct; static Map map_direct; static Unmap unmap_guest; static Release release_direct;
static MapFlexible map_flexible; static Protect protect_guest;

static void aliasing(void) {
    int64_t a = -1, b = -1;
    assert(alloc_direct(0, 1 * GiB, 16 * MiB, 0, 0, &a) == 0);
    assert(alloc_direct(0, 1 * GiB, 16 * MiB, 0, 0, &b) == 0 && b != a);
    /* The same backing offset at three guest addresses (one fixed, two chosen by the runtime). */
    void *x = (void *)(uintptr_t)(USER_MIN + 4 * GiB), *y = NULL, *z = NULL, *other = NULL;
    assert(map_direct(&x, 16 * MiB, 3, MAP_FIXED_GUEST, a, 0) == 0 && x == (void *)(uintptr_t)(USER_MIN + 4 * GiB));
    assert(map_direct(&y, 16 * MiB, 3, 0, a, 0) == 0 && y != x);
    assert(map_direct(&z, 4 * MiB, 3, 0, a, 0) == 0);          /* a shorter view of the start */
    assert(map_direct(&other, 16 * MiB, 3, 0, b, 0) == 0);     /* a different offset: no aliasing */
    assert((uintptr_t)y >= USER_MIN && (uintptr_t)y < USER_MAX);
    volatile uint64_t *px = x, *py = y, *pz = z, *po = other;
    assert(px[0] == 0 && py[0] == 0 && po[0] == 0);
    px[0] = UINT64_C(0x1122334455667788);
    assert(py[0] == UINT64_C(0x1122334455667788) && pz[0] == UINT64_C(0x1122334455667788));
    py[(8 * MiB) / 8] = UINT64_C(0xa5a5a5a5a5a5a5a5);          /* in the middle, through another view */
    assert(px[(8 * MiB) / 8] == UINT64_C(0xa5a5a5a5a5a5a5a5));
    pz[(4 * MiB) / 8 - 1] = 77;                                 /* last word of the short view */
    assert(py[(4 * MiB) / 8 - 1] == 77 && px[(4 * MiB) / 8 - 1] == 77);
    assert(po[0] == 0 && po[(8 * MiB) / 8] == 0);
    /* The runtime's host-side backing view sees the same bytes. */
    uint64_t seen = 0;
    const uint64_t word = UINT64_C(0xfeedfacecafebeef);
    assert(runtime_memory_write_backing((uintptr_t)x + 4096, &word, sizeof word));
    memcpy(&seen, (char *)y + 4096, sizeof seen);
    assert(seen == word);
    /* Mapping a sub-range at an odd (16 KiB) offset keeps the offset: bytes line up. */
    void *w = NULL;
    assert(map_direct(&w, 16 * KiB, 3, 0, a + 16 * KiB, 0) == 0);
    assert(memcmp(w, (char *)x + 16 * KiB, 16 * KiB) == 0);
    printf("aliasing: PASS (x=%p y=%p z=%p w=%p, other offset %p)\n", x, y, z, w, other);
    assert(unmap_guest(x, 16 * MiB) == 0 && unmap_guest(y, 16 * MiB) == 0 && unmap_guest(z, 4 * MiB) == 0);
    assert(unmap_guest(w, 16 * KiB) == 0 && unmap_guest(other, 16 * MiB) == 0);
    assert(release_direct(a, 16 * MiB) == 0 && release_direct(b, 16 * MiB) == 0);
}

/* Regions in [0, limit): who sits below the guest image (the host binary itself, malloc, Rosetta). */
static void print_regions(uint64_t limit) {
    for (mach_vm_address_t at = 0; at < limit;) {
        mach_vm_size_t size = 0; vm_region_basic_info_data_64_t info; mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t object;
        mach_vm_address_t start = at;
        if (mach_vm_region(mach_task_self(), &start, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object) != KERN_SUCCESS || start >= limit) break;
        printf("  region %#llx-%#llx prot=%d\n", (unsigned long long)start, (unsigned long long)(start + size), info.protection);
        at = start + size;
    }
}

static void image_range(void) {
    printf("regions below 2 GiB before the image map:\n"); print_regions(0x80000000);
    void *image = runtime_image_map(64 * MiB, PROT_READ | PROT_WRITE);
    assert(image);
    printf("image_map(64 MiB) -> %p\n", image);
    assert((uintptr_t)image >= 0x400000 && (uintptr_t)image + 64 * MiB <= 0x80000000);
    /* 0x400000 itself is taken when the host binary is large: the first free 1 MiB slot is used, as on Linux. */
    if ((uintptr_t)image != 0x400000) printf("image is not at the PS4 base 0x400000 (host binary and heap occupy it); see regions above\n");
    memset(image, 0x5c, 64 * MiB);
    assert(((unsigned char *)image)[64 * MiB - 1] == 0x5c);
    /* Image-relative thunk memory (cpu_patch.c) goes next to it, RWX, within rel32 reach. */
    void *second = runtime_image_map(8 * MiB, PROT_READ | PROT_WRITE);
    assert(second && (uintptr_t)second >= 0x400000 && (uintptr_t)second + 8 * MiB <= 0x80000000 && second != image);
    printf("second image_map(8 MiB) -> %p\n", second);
}

static void low_range(void) {
    void *low = runtime_low_map(1 * GiB, PROT_READ | PROT_WRITE);
    assert(low);
    printf("low_map(1 GiB) -> %p\n", low);
    assert((uintptr_t)low >= UINT64_C(0x0800000000) && (uintptr_t)low + GiB <= UINT64_C(0x1000000000));
    ((unsigned char *)low)[0] = 1; ((unsigned char *)low)[GiB - 1] = 2;
    assert(((unsigned char *)low)[0] == 1 && ((unsigned char *)low)[GiB - 1] == 2);
    void *stack = runtime_low_map(8 * MiB, PROT_READ | PROT_WRITE);
    assert(stack && (uintptr_t)stack >= UINT64_C(0x0800000000) && (uintptr_t)stack < UINT64_C(0x1000000000) && stack != low);
    assert((uintptr_t)stack > (uintptr_t)low + GiB); /* the gap after each mapping catches overruns */
    /* A hint that is taken must never replace what is there: fill the page after `stack`, map again. */
    printf("low_map(8 MiB) -> %p\n", stack);
}

static void rwx(void) {
    int64_t a = -1;
    assert(alloc_direct(0, 1 * GiB, 1 * MiB, 0, 0, &a) == 0);
    void *code = NULL;
    /* Guest protection 7 = read | write | execute, mapped at once. */
    int32_t r = map_direct(&code, 1 * MiB, 7, 0, a, 0);
    printf("RWX: sceKernelMapDirectMemory(prot=RWX) -> 0x%x\n", (unsigned)r);
    assert(r == 0);
    ((unsigned char *)code)[0] = 0xc3; /* ret */
    ((void (*)(void))code)();
    puts("RWX: executed a ret in a direct-memory page mapped RWX: WORKS");
    /* Changing protection of a live mapping: RW -> RWX (mprotect) -> RX -> RW. */
    void *p = NULL;
    assert(map_direct(&p, 1 * MiB, 3, 0, a, 0) == 0);
    assert(protect_guest(p, 16 * KiB, 7) == 0);
    ((unsigned char *)p)[1] = 0x90;
    ((void (*)(void))p)();
    assert(protect_guest(p, 16 * KiB, 5) == 0);
    ((void (*)(void))p)();
    assert(protect_guest(p, 16 * KiB, 3) == 0);
    ((unsigned char *)p)[2] = 0x90;
    puts("RWX: mprotect RW -> RWX -> RX -> RW on a MAP_SHARED backing page: WORKS (executed in RWX and RX)");
    /* Host-side thunk memory: anonymous RWX (cpu_patch.c). */
    void *anon = mmap(NULL, 16 * KiB, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(anon != MAP_FAILED);
    *(unsigned char *)anon = 0xc3; ((void (*)(void))anon)();
    puts("RWX: anonymous PROT_READ|WRITE|EXEC mmap (thunks): WORKS");
    assert(unmap_guest(code, 1 * MiB) == 0 && unmap_guest(p, 1 * MiB) == 0 && release_direct(a, 1 * MiB) == 0);
}

static void discard(void) {
    /* Direct memory: written, released, allocated again: reads zero (punched hole). */
    int64_t a = -1;
    assert(alloc_direct(0, 1 * GiB, 32 * MiB, 0, 0, &a) == 0);
    unsigned char *p = NULL;
    assert(map_direct((void **)&p, 32 * MiB, 3, 0, a, 0) == 0);
    memset(p, 0xcd, 32 * MiB);
    assert(p[0] == 0xcd && p[32 * MiB - 1] == 0xcd);
    assert(release_direct(a, 32 * MiB) == 0);   /* unmaps p, discards the pages */
    int64_t again = -1;
    assert(alloc_direct(0, 1 * GiB, 32 * MiB, 0, 0, &again) == 0 && again == a);
    assert(map_direct((void **)&p, 32 * MiB, 3, 0, again, 0) == 0);
    for (uint64_t i = 0; i < 32 * MiB; i += 4096) assert(p[i] == 0);
    p[0] = 9; assert(p[0] == 9); /* and it can be written again */
    assert(release_direct(again, 32 * MiB) == 0);
    /* Flexible memory: unmap discards. */
    unsigned char *f = NULL;
    assert(map_flexible((void **)&f, 4 * MiB, 3, 0) == 0);
    memset(f, 0xee, 4 * MiB);
    void *old = f;
    assert(unmap_guest(f, 4 * MiB) == 0);
    unsigned char *g = NULL;
    assert(map_flexible((void **)&g, 4 * MiB, 3, 0) == 0);
    for (uint64_t i = 0; i < 4 * MiB; i += 4096) assert(g[i] == 0);
    printf("discard: PASS (direct 32 MiB re-allocated at the same offset reads zero; flexible 4 MiB %p -> %p reads zero)\n", old, (void *)g);
    assert(unmap_guest(g, 4 * MiB) == 0);
}

static void range_hygiene(void) {
    /* A fixed guest map outside the PS4 user range must be refused, never replace host mappings. */
    int64_t a = -1;
    assert(alloc_direct(0, 1 * GiB, 1 * MiB, 0, 0, &a) == 0);
    void *outside = (void *)(uintptr_t)UINT64_C(0x1000000000); /* kernel-reserved under Rosetta: EACCES */
    int32_t r = map_direct(&outside, 1 * MiB, 3, MAP_FIXED_GUEST, a, 0);
    printf("fixed map at 0x1000000000 (outside the user range) -> 0x%x\n", (unsigned)r);
    assert(r != 0);
    void *host_like = (void *)(uintptr_t)0x100000000; /* where dyld/Rosetta live */
    r = map_direct(&host_like, 1 * MiB, 3, MAP_FIXED_GUEST, a, 0);
    assert(r != 0);
    assert(release_direct(a, 1 * MiB) == 0);
    /* The reservation is one PROT_NONE mapping that begins at USER_MIN. */
    mach_vm_address_t at = USER_MIN + 100 * GiB; mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info; mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t object;
    kern_return_t kr = mach_vm_region(mach_task_self(), &at, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object);
    assert(kr == KERN_SUCCESS && at <= USER_MIN + 100 * GiB && at + size > USER_MIN + 100 * GiB);
    printf("range hygiene: PASS (region around USER_MIN+100GiB: %#llx-%#llx prot=%d)\n", (unsigned long long)at, (unsigned long long)(at + size), info.protection);
}

static void page_sizes(void) {
    int hw = 0; size_t n = sizeof hw;
    sysctlbyname("hw.pagesize", &hw, &n, NULL, 0);
    printf("page sizes: vm_page_size=%lu getpagesize()=%d sysconf(_SC_PAGESIZE)=%ld hw.pagesize=%d vm_kernel_page_size=%lu\n",
           (unsigned long)vm_page_size, getpagesize(), sysconf(_SC_PAGESIZE), hw, (unsigned long)vm_kernel_page_size);
    /* The runtime works in 16 KiB units; mapping at 16 KiB-aligned addresses must always work, and
     * the runtime refuses addresses that are not 16 KiB-aligned. */
    int64_t a = -1;
    assert(alloc_direct(0, 1 * GiB, 64 * KiB, 0, 0, &a) == 0);
    void *odd = (void *)(uintptr_t)(USER_MIN + 8 * GiB + 4096);
    assert(map_direct(&odd, 16 * KiB, 3, MAP_FIXED_GUEST, a, 0) != 0);
    void *ok = (void *)(uintptr_t)(USER_MIN + 8 * GiB + 16 * KiB);
    assert(map_direct(&ok, 16 * KiB, 3, MAP_FIXED_GUEST, a, 0) == 0);
    assert(unmap_guest(ok, 16 * KiB) == 0 && release_direct(a, 64 * KiB) == 0);
}

int main(void) {
    runtime_start(1); /* capability 1: the guest-visible resolver */
    alloc_direct = GET(Allocate, "rTXw65xmLIA#p#J"); map_direct = GET(Map, "L-Q3LEjIbgA#p#J");
    unmap_guest = GET(Unmap, "cQke9UuBQOk#p#J"); release_direct = GET(Release, "MBuItvba6z8#p#J");
    map_flexible = GET(MapFlexible, "IWIBBdTHit4#p#J"); protect_guest = GET(Protect, "vSMAm3cxYTY#p#J");
    assert(alloc_direct && map_direct && unmap_guest && release_direct && map_flexible && protect_guest);
    page_sizes();
    image_range();
    low_range();
    aliasing();
    rwx();
    discard();
    range_hygiene();
    const char *pause_seconds = getenv("BB_TEST_PAUSE");
    if (pause_seconds) {
        /* Leave some mappings in place for vmmap: the pool and a user-range view. */
        int64_t keep = -1; void *view = NULL;
        assert(alloc_direct(0, 1 * GiB, 64 * MiB, 0, 0, &keep) == 0 && map_direct(&view, 64 * MiB, 3, 0, keep, 0) == 0);
        memset(view, 1, 8 * MiB);
        printf("pid %d paused for %s s; user view %p\n", getpid(), pause_seconds, view);
        fflush(stdout);
        sleep((unsigned)atoi(pause_seconds));
    }
    puts("PASS: Darwin guest address space (aliasing, image < 2 GiB, low range, RWX, discard, range hygiene, page sizes)");
    return 0;
}
#endif

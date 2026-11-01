/* Does the x86-64 CPU (Rosetta 2 on Apple Silicon) really EXECUTE the instruction sets the guest uses?
 * Not just report them through cpuid: every group runs real instructions with inline asm and checks
 * the results. A SIGILL in a group is caught and reported as a failure of that group.
 * Rosetta only advertises/translates AVX when ROSETTA_ADVERTISE_AVX=1 is in the environment at launch.
 * Exit code: 0 all groups pass, 1 a group failed, 2 AVX is not advertised (cpuid), nothing executed. */
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__x86_64__)
int main(void) { puts("SKIP: x86_64 only"); return 0; }
#else
static sigjmp_buf jump;
static volatile sig_atomic_t trapped;
static void on_sigill(int sig) { trapped = sig; siglongjmp(jump, 1); }

static void cpuid(uint32_t leaf, uint32_t sub, uint32_t r[4]) {
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}

typedef struct { float f[8]; uint32_t d[8]; } Vec;

static int t_avx_float(void) { /* vaddps / vmulps / vsubps ymm */
    Vec a, b, sum, product, difference;
    for (int i = 0; i < 8; ++i) { a.f[i] = (float)i + 0.5f; b.f[i] = (float)(10 * i) - 3.0f; }
    __asm__ volatile("vmovups (%0), %%ymm0\n\tvmovups (%1), %%ymm1\n\t"
                     "vaddps %%ymm1, %%ymm0, %%ymm2\n\tvmovups %%ymm2, (%2)\n\t"
                     "vmulps %%ymm1, %%ymm0, %%ymm3\n\tvmovups %%ymm3, (%3)\n\t"
                     "vsubps %%ymm1, %%ymm0, %%ymm4\n\tvmovups %%ymm4, (%4)\n\tvzeroupper"
                     :: "r"(a.f), "r"(b.f), "r"(sum.f), "r"(product.f), "r"(difference.f)
                     : "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "memory");
    for (int i = 0; i < 8; ++i)
        if (sum.f[i] != a.f[i] + b.f[i] || product.f[i] != a.f[i] * b.f[i] || difference.f[i] != a.f[i] - b.f[i]) return 0;
    return 1;
}
static int t_fma(void) { /* vfmadd231ps ymm: acc += a*b */
    Vec a, b, acc;
    for (int i = 0; i < 8; ++i) { a.f[i] = (float)(i + 1); b.f[i] = 0.25f * (float)(i + 2); acc.f[i] = 100.0f + (float)i; }
    float expected[8];
    for (int i = 0; i < 8; ++i) expected[i] = acc.f[i] + a.f[i] * b.f[i]; /* exact in float for these values */
    __asm__ volatile("vmovups (%0), %%ymm0\n\tvmovups (%1), %%ymm1\n\tvmovups (%2), %%ymm2\n\t"
                     "vfmadd231ps %%ymm1, %%ymm0, %%ymm2\n\tvmovups %%ymm2, (%2)\n\tvzeroupper"
                     :: "r"(a.f), "r"(b.f), "r"(acc.f) : "ymm0", "ymm1", "ymm2", "memory");
    for (int i = 0; i < 8; ++i) if (acc.f[i] != expected[i]) return 0;
    return 1;
}
static int t_avx2_int(void) { /* vpbroadcastd, vpaddd, vpermd (AVX2, 256-bit integer) */
    Vec out, added, perm, index, source;
    uint32_t seven = 7;
    for (int i = 0; i < 8; ++i) { index.d[i] = (uint32_t)(7 - i); source.d[i] = 1000u + (uint32_t)i; }
    __asm__ volatile("vpbroadcastd (%0), %%ymm0\n\tvmovdqu %%ymm0, (%1)\n\t"
                     "vmovdqu (%2), %%ymm1\n\tvpaddd %%ymm1, %%ymm0, %%ymm2\n\tvmovdqu %%ymm2, (%3)\n\t"
                     "vmovdqu (%4), %%ymm3\n\tvpermd %%ymm1, %%ymm3, %%ymm4\n\tvmovdqu %%ymm4, (%5)\n\tvzeroupper"
                     :: "r"(&seven), "r"(out.d), "r"(source.d), "r"(added.d), "r"(index.d), "r"(perm.d)
                     : "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "memory");
    for (int i = 0; i < 8; ++i)
        if (out.d[i] != 7 || added.d[i] != source.d[i] + 7 || perm.d[i] != source.d[index.d[i]]) return 0;
    return 1;
}
static int t_ymm_state(void) { /* the upper halves of ymm survive a function call and a context switch point */
    Vec in, out;
    for (int i = 0; i < 8; ++i) in.d[i] = 0x11111111u * (uint32_t)(i + 1);
    __asm__ volatile("vmovdqu (%0), %%ymm7" :: "r"(in.d) : "ymm7");
    for (volatile int i = 0; i < 2000000; ++i) { } /* gives the scheduler a chance to preempt */
    __asm__ volatile("vmovdqu %%ymm7, (%0)\n\tvzeroupper" :: "r"(out.d) : "memory");
    return memcmp(in.d, out.d, sizeof in.d) == 0;
}
static int t_bmi1(void) { /* andn, blsr, blsi, blsmsk, tzcnt */
    uint64_t a = 0xF0F0, b = 0x00FF, r_andn, r_blsr, r_blsi, r_blsmsk, r_tzcnt;
    __asm__ volatile("andn %2, %1, %0" : "=r"(r_andn) : "r"(a), "r"(b));          /* ~a & b */
    __asm__ volatile("blsr %1, %0" : "=r"(r_blsr) : "r"(a) : "cc");               /* clear lowest set bit */
    __asm__ volatile("blsi %1, %0" : "=r"(r_blsi) : "r"(a) : "cc");               /* isolate lowest set bit */
    __asm__ volatile("blsmsk %1, %0" : "=r"(r_blsmsk) : "r"(a) : "cc");           /* mask up to lowest set bit */
    __asm__ volatile("tzcnt %1, %0" : "=r"(r_tzcnt) : "r"(a) : "cc");
    return r_andn == (~a & b) && r_blsr == (a & (a - 1)) && r_blsi == (a & -a) && r_blsmsk == (a ^ (a - 1)) && r_tzcnt == 4;
}
static int t_lzcnt(void) {
    uint64_t one = 1, zero = 0, r1, r0; uint32_t r32, v32 = 0x00010000u;
    __asm__ volatile("lzcnt %1, %0" : "=r"(r1) : "r"(one) : "cc");
    __asm__ volatile("lzcnt %1, %0" : "=r"(r0) : "r"(zero) : "cc");
    __asm__ volatile("lzcnt %1, %0" : "=r"(r32) : "r"(v32) : "cc");
    return r1 == 63 && r0 == 64 && r32 == 15; /* on a CPU without LZCNT these decode as BSR and differ */
}
static int t_movbe(void) {
    uint32_t in32 = 0x11223344u, out32; uint64_t in64 = 0x1122334455667788ull, out64; uint16_t in16 = 0xAABB, out16;
    __asm__ volatile("movbe %1, %0" : "=r"(out32) : "m"(in32));
    __asm__ volatile("movbe %1, %0" : "=r"(out64) : "m"(in64));
    __asm__ volatile("movbe %1, %0" : "=r"(out16) : "m"(in16));
    uint32_t stored32; __asm__ volatile("movbe %1, %0" : "=m"(stored32) : "r"(in32));
    return out32 == 0x44332211u && out64 == 0x8877665544332211ull && out16 == 0xBBAA && stored32 == 0x44332211u;
}
static int t_popcnt(void) {
    uint64_t v = 0xF0F0F0F0F0F0F0F0ull, r64; uint32_t v32 = 0xFFu, r32;
    __asm__ volatile("popcnt %1, %0" : "=r"(r64) : "r"(v) : "cc");
    __asm__ volatile("popcnt %1, %0" : "=r"(r32) : "r"(v32) : "cc");
    return r64 == 32 && r32 == 8;
}
static int t_threads_ymm(void); /* ymm per thread: below */

typedef struct { const char *name; int (*run)(void); const char *needs; } Group;
static const Group groups[] = {
    {"AVX   vaddps/vmulps/vsubps ymm", t_avx_float, "avx"},
    {"FMA3  vfmadd231ps ymm", t_fma, "fma"},
    {"AVX2  vpbroadcastd/vpaddd/vpermd ymm", t_avx2_int, "avx2"},
    {"AVX   ymm state preserved across preemption window", t_ymm_state, "avx"},
    {"BMI1  andn/blsr/blsi/blsmsk/tzcnt", t_bmi1, "bmi1"},
    {"LZCNT lzcnt (63/64/15)", t_lzcnt, "lzcnt"},
    {"MOVBE movbe 16/32/64 load+store", t_movbe, "movbe"},
    {"POPCNT popcnt", t_popcnt, "popcnt"},
};

#include <pthread.h>
static void *thread_main(void *arg) { *(int *)arg = t_ymm_state() && t_avx2_int() && t_fma(); return NULL; }
static int t_threads_ymm(void) {
    pthread_t t[4]; int ok[4] = {0};
    for (int i = 0; i < 4; ++i) pthread_create(&t[i], NULL, thread_main, &ok[i]);
    for (int i = 0; i < 4; ++i) pthread_join(t[i], NULL);
    return ok[0] && ok[1] && ok[2] && ok[3];
}

int main(void) {
    uint32_t r[4]; cpuid(1, 0, r);
    const uint32_t ecx1 = r[2];
    uint32_t l7[4]; cpuid(7, 0, l7);
    uint32_t e1[4]; cpuid(0x80000001u, 0, e1);
    struct { const char *n; int v; } bits[] = {
        {"avx", (ecx1 >> 28) & 1}, {"fma", (ecx1 >> 12) & 1}, {"avx2", (l7[1] >> 5) & 1}, {"bmi1", (l7[1] >> 3) & 1},
        {"bmi2", (l7[1] >> 8) & 1}, {"movbe", (ecx1 >> 22) & 1}, {"popcnt", (ecx1 >> 23) & 1}, {"lzcnt", (e1[2] >> 5) & 1},
        {"osxsave", (ecx1 >> 27) & 1},
    };
    const char *env = getenv("ROSETTA_ADVERTISE_AVX");
    printf("ROSETTA_ADVERTISE_AVX=%s\ncpuid:", env ? env : "(unset)");
    for (size_t i = 0; i < sizeof bits / sizeof *bits; ++i) printf(" %s=%d", bits[i].n, bits[i].v);
    puts("");
    if (bits[8].v) {
        uint32_t lo, hi; __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        printf("xcr0=0x%x (ymm state %s)\n", lo, (lo & 4) ? "enabled" : "NOT enabled");
    }
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigill; sa.sa_flags = SA_NODEFER; sigemptyset(&sa.sa_mask);
    sigaction(SIGILL, &sa, NULL); sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    int failed = 0, not_advertised = 0;
    for (size_t g = 0; g < sizeof groups / sizeof *groups; ++g) {
        int advertised = 0;
        for (size_t i = 0; i < sizeof bits / sizeof *bits; ++i) if (!strcmp(bits[i].n, groups[g].needs)) advertised = bits[i].v;
        int ok = 0; trapped = 0;
        if (!sigsetjmp(jump, 1)) ok = groups[g].run();
        printf("%-4s %s%s\n", ok ? "PASS" : "FAIL", groups[g].name,
               ok ? "" : trapped ? (trapped == SIGILL ? "  [SIGILL]" : "  [SIGSEGV/SIGBUS]") : "  [wrong result]");
        if (!ok) { ++failed; if (!advertised) ++not_advertised; }
    }
    trapped = 0;
    int threads_ok = 0;
    if (!failed && !sigsetjmp(jump, 1)) threads_ok = t_threads_ymm(); /* only after the single-thread groups passed: siglongjmp cannot leave a worker */
    printf("%-4s AVX/AVX2/FMA in 4 concurrent threads%s\n", threads_ok ? "PASS" : "FAIL", threads_ok ? "" : trapped ? "  [signal]" : "");
    if (!threads_ok) ++failed;
    if (failed) {
        printf("RESULT: %d group(s) failed (%d of them not advertised by cpuid)\n", failed, not_advertised);
        return not_advertised && !bits[0].v ? 2 : 1;
    }
    puts("PASS: AVX, AVX2, FMA, BMI1, LZCNT, MOVBE, POPCNT execute and give correct results");
    return 0;
}
#endif

/* Signal handling on macOS under Rosetta 2 (docs/macos/M3-guest-boots.md task 5 and the watchdog of
 * task 6). The real probe.c is compiled into this test (main renamed), so fault() and watchdog() are
 * the shipped ones:
 *  1. SIGTRAP: cpu_patch rewrites an LZCNT into a breakpoint; running it enters fault(), which reads
 *     __ss.__rip, finds the site and resumes in the thunk. Result must equal lzcnt.
 *  2. SIGSEGV on a PROT_NONE page with runtime_fault_recover set (speculative guest reads): fault()
 *     siglongjmps back; from the main thread and from other threads.
 *  3. A handler of the same shape as fault() (SA_SIGINFO|SA_ONSTACK, host sigaltstack) for SIGILL (ud2),
 *     SIGTRAP (int3) and SIGSEGV: rip, si_addr and the write bit (__es.__err, what
 *     Common::IsWriteError reads for GPU page tracking) are right, and the thread continues.
 *  4. The watchdog: a forked child with idle threads arms alarm(); its stderr must hold the
 *     "watchdog timeout" line and a stack dump for the other threads (SIGUSR2 via pthread_kill).
 * macOS only. */
/* main falls off its end in probe.c, which only main may do. */
#pragma clang diagnostic ignored "-Wreturn-type"
#define main bb_probe_main
#include "../src/probe.c"
#undef main
#include <assert.h>
#include <sys/wait.h>

#ifndef __APPLE__
int main(void) { puts("SKIP: macOS only"); return 0; }
#else
static void install_probe_handlers(void) {
    struct sigaction sa = {0}; sa.sa_sigaction = fault; sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGILL, &sa, NULL); sigaction(SIGBUS, &sa, NULL); sigaction(SIGTRAP, &sa, NULL);
}

/* 1. cpu_patch breakpoint through the real fault(). */
typedef uint32_t (__attribute__((ms_abi)) *Lzcnt)(uint32_t);
static void breakpoint_through_fault(void) {
    const size_t size = 1 << 20;
    unsigned char *image = bb_darwin_map_at((void *)(uintptr_t)0x30000000, size, PROT_READ | PROT_WRITE | PROT_EXEC);
    assert(image == (unsigned char *)(uintptr_t)0x30000000);
    memset(image, 0xcc, size);
    /* jmp0, lzcnt eax,ecx, jmp0, ret: too short for a jump, so cpu_patch uses a breakpoint
     * (tests/test_cpu_patch.c "lzcnt breakpoint"). */
    static const unsigned char body[] = {0xeb, 0x00, 0xf3, 0x0f, 0xbd, 0xc1, 0xeb, 0x00, 0xc3};
    memcpy(image + 0x1000, body, sizeof(body));
    setenv("BB_CPU_PATCH", "all", 1);
    const uint64_t ranges[1][2] = {{0x1000, 0x1000}};
    cpu_patch_image(image, size, ranges, 1);
    assert(image[0x1000 + 2] == 0xcc);
    Lzcnt f = (Lzcnt)(void *)(image + 0x1000);
    assert(f(0x10) == 27 && f(0) == 32 && f(0x80000000u) == 0 && f(1) == 31);
    for (int i = 0; i < 2000; ++i) assert(f((uint32_t)i) == (i ? (uint32_t)__builtin_clz((unsigned)i) : 32u));
    puts("signals: SIGTRAP -> fault() -> cpu_patch thunk (2004 breakpoints, results equal lzcnt)");
}

/* 2. SIGSEGV recovery (runtime_fault_recover). */
static void *segv_thread(void *page) {
    for (int i = 0; i < 200; ++i) {
        sigjmp_buf recover;
        if (!sigsetjmp(recover, 1)) {
            runtime_fault_recover = &recover;
            if (i & 1) *(volatile char *)page = 1; else (void)*(volatile char *)page;
            runtime_fault_recover = NULL;
            return (void *)1; /* the access did not fault: wrong */
        }
        assert(runtime_fault_recover == NULL);
    }
    return NULL;
}
static void segv_recovery(void) {
    char *page = mmap(NULL, 16384, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(page != MAP_FAILED);
    assert(segv_thread(page) == NULL);
    pthread_t t[4];
    void *r[4];
    for (int i = 0; i < 4; ++i) pthread_create(&t[i], NULL, segv_thread, page);
    for (int i = 0; i < 4; ++i) { pthread_join(t[i], &r[i]); assert(!r[i]); }
    puts("signals: SIGSEGV -> fault() -> siglongjmp recovery (5 threads x 200 faults, reads and writes)");
}

/* 3. Handler shape of fault(), with a host alternate stack. */
static volatile uint64_t seen_rip, seen_addr, seen_err, seen_sp_marker;
static volatile int seen_signal;
static uint64_t resume_at;
static void probe_shape(int sig, siginfo_t *info, void *context) {
    ucontext_t *uc = context;
    seen_signal = sig;
    seen_rip = UC_RIP(uc);
    seen_addr = (uint64_t)(uintptr_t)info->si_addr;
    seen_err = uc->uc_mcontext->__es.__err;
    char marker; seen_sp_marker = (uint64_t)(uintptr_t)&marker;
    if (resume_at) UC_RIP(uc) = resume_at;
}
static void *shape_thread(void *unused) {
    (void)unused;
    stack_t alt = {.ss_sp = malloc(1 << 16), .ss_size = 1 << 16}; /* host memory, not guest memory */
    assert(sigaltstack(&alt, NULL) == 0);
    struct sigaction sa = {0}; sa.sa_sigaction = probe_shape; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGILL, &sa, NULL); sigaction(SIGTRAP, &sa, NULL); sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);

    unsigned char *code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(code != MAP_FAILED);
    /* int3; ret */
    code[0] = 0xcc; code[1] = 0xc3;
    resume_at = 0; seen_signal = 0;
    ((void (*)(void))(void *)code)();
    assert(seen_signal == SIGTRAP);
    printf("signals: SIGTRAP rip=%#llx, int3 at %p (rip is after the int3: cpu_patch uses rip-1)\n", (unsigned long long)seen_rip, (void *)code);
    assert(seen_rip == (uint64_t)(uintptr_t)code + 1);
    assert(seen_sp_marker >= (uint64_t)(uintptr_t)alt.ss_sp && seen_sp_marker < (uint64_t)(uintptr_t)alt.ss_sp + alt.ss_size); /* ran on the alt stack */

    /* ud2; ret: SIGILL at the ud2, resume at the ret. */
    code[0] = 0x0f; code[1] = 0x0b; code[2] = 0xc3;
    resume_at = (uint64_t)(uintptr_t)code + 2; seen_signal = 0;
    ((void (*)(void))(void *)code)();
    assert(seen_signal == SIGILL);
    printf("signals: SIGILL rip=%#llx, ud2 at %p\n", (unsigned long long)seen_rip, (void *)code);
    assert(seen_rip == (uint64_t)(uintptr_t)code);

    /* mov rax,[rdi]; ret then mov [rdi],al; ret on a PROT_NONE page: si_addr and the write bit. */
    char *page = mmap(NULL, 16384, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(page != MAP_FAILED);
    static const unsigned char read_[] = {0x48, 0x8b, 0x07, 0xc3}, write_[] = {0x88, 0x07, 0xc3};
    memcpy(code, read_, sizeof(read_)); memcpy(code + 16, write_, sizeof(write_));
    resume_at = (uint64_t)(uintptr_t)code + 3; seen_signal = 0;
    ((void (*)(char *))(void *)code)(page + 8);
    assert(seen_signal == SIGSEGV || seen_signal == SIGBUS);
    printf("signals: read fault: signal %d rip=%#llx si_addr=%#llx (page+8=%p) err=%#llx write-bit=%d\n", seen_signal,
           (unsigned long long)seen_rip, (unsigned long long)seen_addr, (void *)(page + 8), (unsigned long long)seen_err, (int)((seen_err >> 1) & 1));
    assert(seen_addr == (uint64_t)(uintptr_t)(page + 8) && seen_rip == (uint64_t)(uintptr_t)code);
    assert(!(seen_err & 2));
    resume_at = (uint64_t)(uintptr_t)code + 16 + 2; seen_signal = 0;
    ((void (*)(char *))(void *)(code + 16))(page + 24);
    printf("signals: write fault: signal %d rip=%#llx si_addr=%#llx (page+24=%p) err=%#llx write-bit=%d\n", seen_signal,
           (unsigned long long)seen_rip, (unsigned long long)seen_addr, (void *)(page + 24), (unsigned long long)seen_err, (int)((seen_err >> 1) & 1));
    assert(seen_addr == (uint64_t)(uintptr_t)(page + 24) && seen_rip == (uint64_t)(uintptr_t)code + 16);
    assert(seen_err & 2);

    /* Which signal does a protection fault (write to a read-only page: GPU page tracking) raise, and which an
     * unmapped address? fault() must treat the first like SIGSEGV. */
    char *ro = mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(ro != MAP_FAILED);
    resume_at = (uint64_t)(uintptr_t)code + 16 + 2; seen_signal = 0;
    ((void (*)(char *))(void *)(code + 16))(ro + 8);
    const int protection_signal = seen_signal;
    assert(seen_addr == (uint64_t)(uintptr_t)(ro + 8) && (seen_err & 2));
    char *gone = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(gone != MAP_FAILED && munmap(gone, 16384) == 0);
    resume_at = (uint64_t)(uintptr_t)code + 16 + 2; seen_signal = 0;
    ((void (*)(char *))(void *)(code + 16))(gone + 8);
    printf("signals: write to a read-only page raises signal %d (SIGSEGV=%d SIGBUS=%d), write to an unmapped page raises signal %d\n",
           protection_signal, SIGSEGV, SIGBUS, seen_signal);
    assert(protection_signal == SIGBUS || protection_signal == SIGSEGV);
    return NULL;
}
static void handler_shape(void) {
    pthread_t t;
    pthread_create(&t, NULL, shape_thread, NULL);
    pthread_join(t, NULL);
    puts("signals: SIGILL / SIGTRAP / SIGSEGV on SA_ONSTACK with a host sigaltstack: rip, si_addr and the write bit correct");
}

/* 4. The watchdog in a child process. */
static void *idle(void *unused) { (void)unused; for (;;) sleep(1); return NULL; }
static void watchdog_dump(void) {
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        dup2(fds[1], 2); close(fds[0]); close(fds[1]);
        install_probe_handlers();
        struct sigaction dump = {0}; dump.sa_sigaction = thread_dump; dump.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&dump.sa_mask); sigaction(SIGUSR2, &dump, NULL);
        struct sigaction alarm_action = {0}; alarm_action.sa_sigaction = watchdog; alarm_action.sa_flags = SA_SIGINFO;
        sigemptyset(&alarm_action.sa_mask); sigaction(SIGALRM, &alarm_action, NULL);
        Dl_info self_info;
        if (dladdr((void *)watchdog_dump, &self_info)) exe_base = (uintptr_t)self_info.dli_fbase;
        pthread_t t[3];
        for (int i = 0; i < 3; ++i) pthread_create(&t[i], NULL, idle, NULL);
        alarm(1);
        for (;;) sleep(1);
    }
    close(fds[1]);
    char text[65536];
    size_t n = 0;
    ssize_t got;
    while (n < sizeof(text) - 1 && (got = read(fds[0], text + n, sizeof(text) - 1 - n)) > 0) n += (size_t)got;
    text[n] = 0;
    int status = 0;
    waitpid(pid, &status, 0);
    unsigned tids = 0;
    char seen_tid[16][17] = {{0}};
    for (const char *p = text; (p = strstr(p, "  tid=")); p += 6) {
        char tid[17]; memcpy(tid, p + 6, 16); tid[16] = 0;
        unsigned k = 0;
        while (k < tids && memcmp(seen_tid[k], tid, 17)) ++k;
        if (k == tids && tids < 16) memcpy(seen_tid[tids++], tid, 17);
    }
    printf("signals: watchdog child: exit status %d (signal %d), %u distinct thread ids in the dump\n",
           WIFEXITED(status) ? WEXITSTATUS(status) : -1, WIFSIGNALED(status) ? WTERMSIG(status) : 0, tids);
    if (!strstr(text, "STOP: watchdog timeout") || tids < 4 || !WIFEXITED(status) || WEXITSTATUS(status) != 128 + SIGALRM) {
        fputs(text, stdout);
        assert(!"watchdog did not dump the threads");
    }
}

int main(void) {
    install_probe_handlers();
    breakpoint_through_fault();
    segv_recovery();
    handler_shape();
    watchdog_dump();
    puts("PASS: Darwin signals (SIGTRAP cpu_patch, SIGSEGV recovery, SIGILL/SIGTRAP/SIGSEGV context fields, watchdog thread dump)");
    return 0;
}
#endif

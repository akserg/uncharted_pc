/* M3 task 1: does the file-backed guest memory pool (runtime_memory.c Darwin backend, an unlinked
 * sparse file under MAP_SHARED) make macOS write dirty pages to the SSD continuously?
 * Maps 2 GiB of direct memory through the runtime and rewrites all of it in a loop (a game
 * rewrites its GPU buffers every frame), printing every 5 s the passes done, this process's
 * disk bytes written (proc_pid_rusage ri_diskio_byteswritten) and the system's pageouts. Run
 * `iostat -d -w 5 disk0` and `vm_stat 5` next to it. Not part of build.sh --test (it takes minutes).
 *   usage: measure-pool-writeback [seconds=120] [gib=2]
 * Build: see the Result of docs/macos/M3-guest-boots.md. macOS only. */
#include "runtime.h"
#include <assert.h>
#include <inttypes.h>
#include <libproc.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void runtime_restart(void) { abort(); }
typedef int32_t (ABI *Allocate)(int64_t, int64_t, uint64_t, uint64_t, int, int64_t *);
typedef int32_t (ABI *Map)(void **, uint64_t, int, int, int64_t, uint64_t);
#define GET(type, name) ((type)runtime_resolve(name, 0))

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + (double)t.tv_nsec * 1e-9; }
static uint64_t disk_written(void) {
    struct rusage_info_v4 info;
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&info)) return 0;
    return info.ri_diskio_byteswritten;
}
static uint64_t pageouts(void) {
    vm_statistics64_data_t s; mach_msg_type_number_t n = HOST_VM_INFO64_COUNT;
    return host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&s, &n) == KERN_SUCCESS ? s.pageouts : 0;
}
int main(int argc, char **argv) {
    const double seconds = argc > 1 ? atof(argv[1]) : 120;
    const uint64_t bytes = (argc > 2 ? (uint64_t)atoi(argv[2]) : 2) << 30;
    runtime_start(1);
    Allocate alloc = GET(Allocate, "rTXw65xmLIA#p#J");
    Map map = GET(Map, "L-Q3LEjIbgA#p#J");
    assert(alloc && map);
    int64_t offset = -1; void *view = NULL;
    assert(alloc(0, 1LL << 40, bytes, 0, 0, &offset) == 0 && map(&view, bytes, 3, 0, offset, 0) == 0);
    volatile uint64_t *p = view; const uint64_t words = bytes / 8;
    printf("pid %d: %" PRIu64 " MiB at %p; rewriting for %.0f s\n", getpid(), bytes >> 20, view, seconds);
    const uint64_t d0 = disk_written(), p0 = pageouts();
    const double start = now();
    double next_report = start + 5;
    uint64_t pass = 0, last_disk = d0, last_pageouts = p0;
    while (now() - start < seconds) {
        ++pass;
        for (uint64_t i = 0; i < words; i += 2048) { for (uint64_t k = 0; k < 2048; ++k) p[i + k] = pass * 0x9e3779b97f4a7c15ull + i + k; }
        if (now() >= next_report) {
            const uint64_t d = disk_written(), po = pageouts();
            printf("t=%5.0f s passes=%" PRIu64 " (%.1f GiB written by the test) process disk writes +%.1f MiB (total %.1f MiB), system pageouts +%" PRIu64 "\n",
                   now() - start, pass, (double)pass * (double)bytes / (double)(1 << 30), (double)(d - last_disk) / (1 << 20),
                   (double)(d - d0) / (1 << 20), po - last_pageouts);
            last_disk = d; last_pageouts = po; next_report += 5;
        }
    }
    printf("done: %" PRIu64 " passes, process disk writes %.1f MiB in %.0f s (%.3f MiB/s), system pageouts +%" PRIu64 "\n", pass,
           (double)(disk_written() - d0) / (1 << 20), now() - start, (double)(disk_written() - d0) / (1 << 20) / (now() - start), pageouts() - p0);
    return 0;
}

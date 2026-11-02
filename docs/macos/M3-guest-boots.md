# M3 — Guest boots to the first GNM call

## Goal
`bb-probe` loads `eboot.bin`, patches it, enters it on the guest stack under Rosetta, and the game runs
its init until it first talks to the GPU (`sceGnm*` / `sceVideoOut*`). The renderer may still be
absent/stubbed; the point is proving guest execution + runtime HLE under Rosetta.

## Preconditions
- M2 done. Game dump available at `BB_GAME_DIR`.

## Tasks
1. **Pool write-back check** (M2 review item 3). Test program: map 2 GiB of the pool via the runtime,
   rewrite all of it in a loop for 120 s. Watch `iostat -d -w 5` (disk0 MB/s) and `vm_stat` pageouts.
   If sustained writes > a few MB/s attributable to the test, replace the Darwin pool with
   `mach_make_memory_entry_64(MAP_MEM_NAMED_CREATE)` + `mach_vm_map` (copy=FALSE) for aliases, discard =
   `mach_vm_behavior_set(VM_BEHAVIOR_REUSABLE)`/`madvise(MADV_FREE_REUSABLE)` or zeroing; keep
   `test_memory_darwin` passing. Record numbers either way.
2. **GPU-heap window** (M2 review item 1): on `__APPLE__` set `GpuUseLow/GpuUseHigh` in
   `buffer_cache.cpp` to `USER_MIN`-based values (expose `USER_MIN` via a header, don't duplicate the
   literal). Grep all of `gpu/` and `src/` for other hard-coded user-range constants and list them in Result.
3. **Low allocator** (M2 open issue 2): every host object whose address is given to the guest
   (thread structs/handles, `tls_block_alloc`, mutex/rwlock/sema/condvar handles, file, ajm, trophy
   handles) must be below 1 TiB. Implement a small thread-safe arena allocator on top of `runtime_low_map`
   (`runtime_low_alloc/free/aligned`), used under `#ifdef __APPLE__` at those sites. Add a test that
   allocates from many threads and asserts every pointer < 2^40. Remove the `MACOS-STUB(M3)` malloc tag.
4. Log every refused guest fixed-address mapping outside `[USER_MIN, USER_MAX)`: `MACOS: refused fixed map
   addr=... size=...`.
5. **Signals**: replace the M1 stub in `probe.c` `fault()` with the Darwin layout:
   `((ucontext_t*)ctx)->uc_mcontext->__ss.__rip`, fault address from `info->si_addr`
   (or `__es.__faultvaddr`). Same for `bbgpu_handle_fault` in `gpu/shim` if it reads the context.
   `sigaltstack` and `SA_SIGINFO|SA_ONSTACK` as on Linux.
6. **Guest TLS** (most important item in M3). Linux points GS at the guest TCB via `arch_prctl(ARCH_SET_GS)`; macOS has no userspace equivalent. Reuse the **Windows design**: `probe.c` `patch_tcb_loads` rewrites every `mov rax, gs:[0]` (`65 48 8b 04 25 00 00 00 00`) to a fixed displacement, and `runtime_thread.c` `set_gs` stores the TCB in that slot. On macOS x86_64, GS is the pthread TSD base: `pthread_key_create` once → displacement = `key*8` (`runtime_thread_tcb_offset`), `set_gs` = `pthread_setspecific(key, tcb)`. Enable `patch_tcb_loads` for `_WIN32 || __APPLE__`. Check shadPS4 `src/core/tls.cpp` (APPLE branch) to confirm. Verify with a test that reads `gs:[key*8]` from inline asm under Rosetta and gets the value set.
   Also: `SYS_tgkill` in the watchdog sampler → `pthread_kill(pthread_t, SIGUSR2)`.
7. **CPU detection**: run `out/bb-probe` and log what `cpuid` reports under Rosetta (AVX, AVX2, BMI1,
   LZCNT, MOVBE, POPCNT, OSXSAVE/XGETBV). `cpu_patch.c` must see BMI1/LZCNT/MOVBE present or patch them;
   either outcome is fine, but AVX **must** be present (else macOS < 15.4 or Rosetta not installed).
8. **Smoke**: `out/bb-probe` with `src/vulkan_smoke.c` path (whatever upstream uses to test the loader
   without a game) if it exists as a mode; else go straight to the game.
9. **Run**: `BB_GAME_DIR=... BB_EXE=u1 ./run.sh` with the window backend still aborting at first
   `sceVideoOut`/GNM call. Watch `user/last_run.log`. Fix in order: ELF mapping, relocations, TLS,
   thread creation (`runtime_thread.c` — `pthread` attrs, affinity no-op, names), fibers, file I/O
   (`runtime_file.c` — path handling, `O_DIRECT`, `pread` sizes), time (`runtime_time.c`,
   `runtime_rtc.c` — `clock_gettime(CLOCK_MONOTONIC_RAW)` exists on Darwin), save data dir.
10. Confirm that Xbyak-emitted code and the `ms_abi` hook callbacks execute under Rosetta (they will
   only be exercised once GNM runs — note as "not yet verified" if M3 ends before that).

## Acceptance
- Log shows the guest reached the point where upstream Linux logs its first GNM/VideoOut call,
  and the process exits via the intentional window abort, not via a signal.
- No `SIGILL` from the guest.

## Hazards
- Rosetta and `sigaltstack`: works, but the alt stack must be allocated by the host, not in guest memory.
- Darwin `pthread_setname_np` takes only a name (current thread). Thread names set from another thread
  are not possible; make it a no-op in that case.
- `MXCSR`/`x87 CW` set in `runtime_thread.c` are honoured by Rosetta (it emulates them); keep the code.

## Result

**M3: runtime ready for a guest under Rosetta; the guest itself was NOT run.** Date 2026-11-01, branch `macos`, MacBook Pro M4 Max 48 GB, macOS 26, x86_64 under Rosetta. `BB_GAME_DIR` was `NONE`: **Task 9 (run the game to the first GNM/VideoOut call) was skipped**, so the acceptance items that need a game dump are open. Everything else in the task list is done and covered by tests that run under Rosetta.
All runs used `ROSETTA_ADVERTISE_AVX=1`. Upstream reference checked: shadPS4 `src/core/tls.cpp` (see "Guest TLS": **shadPS4 does not use the design below**).

Commits after `e85f91e` (plus the Result commit):
`0206aa2` user-range header, GPU-heap window, refused-map log (tasks 2, 4) · `0cea332` low allocator + test (task 3) · `9840e40` TLS, `fault()`, watchdog, tests (tasks 5, 6) · `b99dc58` pool backend = mach memory entry + measurement tool (task 1) · `1697b2d` CPU log, Vulkan smoke, `run.sh` (tasks 7, 8) · one small follow-up keeping `check_cpu()` order unchanged on other platforms.

### Acceptance
| Item | Result |
|---|---|
| `ROSETTA_ADVERTISE_AVX=1 bash build.sh --test` passes every test | **PASS**, exit 0, 11 test programs (8 old + 3 new) |
| pool write-back numbers recorded, backend decided | **PASS**: file-backed pool wrote ~2 GB bursts; switched to a mach memory entry; re-measured quiet (below) |
| `grep -rn "MACOS-STUB(M3)" src gpu` | **PASS**: no output. Remaining stub: `gpu/shim/window.cpp` `MACOS-STUB(M4)` only (1) |
| guest reaches the first GNM/VideoOut call; exit is the window abort; no guest SIGILL | **NOT RUN** (no game dump) |

### Tests
| Test (`out/*`) | What it proves | Result |
|---|---|---|
| pad-test, runtime-test, file-mods-test, sema-test, content-test, cpu-patch-test, avx-rosetta-test, memory-darwin-test | unchanged M2 set | PASS |
| `lowalloc-darwin-test` (new, `tests/test_lowalloc_darwin.c`) | 16 threads x 4000 mixed alloc/calloc/aligned(64)/large (up to 370 KB) with frees: every block < 2^40 (highest `0x831200010`), 0 live afterwards, memory reused; then the real guest-visible objects (mutex, rwlock, cond, thread, main-thread handle) all < 2^40. Same run shows plain `malloc` returns `0x60000091c100` | PASS |
| `tls-darwin-test` (new, `tests/test_tls_darwin.c`) | 13 threads (12 via `scePthreadCreate`, plus main): each reads its own TCB through inline-asm `gs:[off]` and through the exact 9-byte `65 48 8b 04 25 disp32` instruction run from RWX memory; `tcb_self==tcb`, `tcb_thread==handle`, all distinct, all < 2^40 | PASS |
| `signals-darwin-test` (new, `tests/test_signals_darwin.c`, includes the real `src/probe.c`) | below | PASS |

`signals-darwin-test`, with the shipped `fault()` and `watchdog()`: (1) cpu_patch SIGTRAP: 2004 breakpoints taken, thunk results equal `lzcnt`; (2) SIGSEGV/SIGBUS recovery through `runtime_fault_recover` + `siglongjmp`, 5 threads x 200 faults; (3) a handler of `fault()`'s shape on `SA_ONSTACK` with a host `sigaltstack`: `ud2` -> SIGILL with `rip` at the `ud2`; `int3` -> SIGTRAP with `rip` = int3 + 1 (so `cpu_patch_breakpoint(&to, to-1)` is right); faulting read/write: `si_addr` exact, `__es.__err` = 4 (read) / 6 (write), so `Common::IsWriteError` (`gpu/shadps4/common/signal_context.cpp`, reads `__es.__err & 2`) works under Rosetta; handler ran on the alternate stack; (4) the watchdog in a forked child with 3 idle threads: output has `STOP: watchdog timeout` and stack dumps from 4 distinct threads, exit status 142.

### Task 1: pool write-back measurement and decision
Tool: `tests/measure_pool_writeback.c` (not in `--test`, takes 120 s). Maps 2 GiB of direct memory through the runtime and rewrites all of it in a loop (about 2700 passes = 5.4 TB of stores), printing process `ri_diskio_byteswritten` and system pageouts every 5 s. Build line (from the repo root, with the `build.sh` Darwin environment): `clang -arch x86_64 -mmacosx-version-min=15.4 -std=c11 -O2 -pthread -Wl,-no_pie -Wl,-pagezero_size,0x1000 -I$BB_DEPS/include $(pkg-config --cflags vulkan sdl3 libavcodec libavutil) -I. -Isrc tests/measure_pool_writeback.c src/runtime*.c out/libatrac9.a -lm -Lout/gpu -lbbgpu -Wl,-rpath,$PWD/out/gpu -Wl,-export_dynamic -L$BB_DEPS/lib $(pkg-config --libs sdl3 libavcodec libavutil vulkan) -Wl,-rpath,$VULKAN_SDK/lib -o out/measure-pool-writeback`; run `out/measure-pool-writeback 120 2` next to `iostat -d -w 5 disk0`.
The per-process counter stayed 0 and `vm_stat` pageouts did not move in either run (the kernel's vnode pager does the work, unattributed), so **`iostat` is the evidence**. Idle baseline before the runs: 0.7-2.1 MB/s. (The machine already had 8 GB of swap in use, i.e. normal memory pressure.)
| Backend | iostat disk0 during the 120 s rewrite (5 s samples) |
|---|---|
| M2: unlinked sparse file, `MAP_SHARED` | 6 bursts of **380, 407, 406, 404, 402 MB/s** (plus a 31 MB/s tail) = **~10 GB in 140 s, mean 75 MB/s**; about every 30 s the dirty set (~2 GB) is flushed. Between bursts ~0.3 MB/s |
| M3: anonymous mach memory entry | **max 1.5 MB/s, mean 0.44 MB/s, ~60 MB total, 0 samples above 5 MB/s**: indistinguishable from the idle baseline. Passes done: 2810 vs 2716 (store speed unchanged) |
**Decision: switch.** A game that rewrites its GPU heaps would have written its dirty working set to the SSD every ~30 s, for no purpose. The pool is now one `mach_make_memory_entry_64(MAP_MEM_NAMED_CREATE | VM_PROT_ALL)` of POOL+FLEX_SPAN (6 GiB at the default); the host view is `mach_vm_map(VM_FLAGS_ANYWHERE)` with a start hint above the guest ranges; each guest mapping is `mach_vm_map(VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, entry, phys, copy=FALSE, max_prot=ALL)` inside the PROT_NONE reservation, then `mprotect` adds PROT_EXEC (same two-step as before). `memory-darwin-test` passes unchanged: aliasing at several addresses, RWX (an executed `ret` in a direct-memory page, RW->RWX->RX->RW), discard reads zero. **Discard is now `memset(0)` through the host view** (no hole punching on an entry): correct for every alias, but it commits the discarded pages. `mach_error_string` messages replace `perror` on failure. `pool_fd` is not compiled on Apple any more.

### Task 2: GPU-heap window and the hard-coded-constant grep
New header `gpu/shim/bb_user_range.h` holds `BB_USER_MIN` (Apple `0x7000000000`, else `0x1000000000`), `BB_LOW_MAX`, `BB_USER_MAX`; `src/runtime_memory.c` now takes its `USER_MIN/LOW_MAX/USER_MAX` from it (no duplicated literal). Users:
- `gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp`: `GpuUseLow/GpuUseHigh` = `BB_USER_MIN .. +0x200000000` on Apple (the review item), and the hot-window statistics (`NoteHotUse`, `RegionKey`, comments), which tested `address >> 28 == 0x104`, now use `HotRegion = (GpuUseLow + 0x40000000) >> 28` (same value, 0x104, on Linux). Diagnostics only.
- `gpu/shim/bbgpu.cpp` `AddressSpace::GetUsableRegions`: `[0x1000000000, 0xfc00000000)` -> `[BB_USER_MIN, BB_USER_MAX)` (this is what the video core believes is guest memory; it would have excluded the whole Apple range).
- `gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp` `StaleTracker` (`BB_STALE_CHECK=1`, debug): `0x1100000000` window -> `BB_USER_MIN + 0x100000000` on Apple.
Searched `gpu/shadps4`, `gpu/shim`, `src` for `0x10'0000'0000`-style literals, `1ull << 3x/4x`, `>> 28`, `0x104`, `0x11000...`, `UserMin`, `SystemReserved`; the rest are hash constants, shader/register bit masks and `bbport_toggles.h` bit flags. Checked and left alone:
- `gpu/shim/core/memory.h` `IsValidGpuMapping`: `address + size < 0x100'0000'0000` (1 TiB); `BB_USER_MAX` 0xfc00000000 is below it.
- `gpu/shim/core/address_space.h`: `SystemReservedVirtualBase() = 0x700000000`, size 4 GiB; `gnmdriver.cpp` puts the tessellation factor ring at its end (`0x7F0000000`). It is a GPU-visible address, never host-mapped by the runtime; it lies in `[0x700000000, 0x800000000)`, below `LOW_MIN` (so no clash with `runtime_low_map`) and below the commpage, so it would be mappable if M4/M5 needs host backing. Not verified with a game.

### Task 3: low allocator
`src/runtime_lowalloc.c` (compiled on Apple only; the file is empty elsewhere). Chunks of 1 MiB from `runtime_low_map` (so always inside `[0x800000000, 0xfc0000000)`), carved into power-of-two classes 32 B .. 64 KiB with per-class free lists behind one mutex, a 16-byte header in front of each block (magic, class, value); blocks > 64 KiB get their own `runtime_low_map` mapping and are `munmap`ed on free; `runtime_low_aligned` allocates `size+align+32` and stores a back-pointer header before the aligned address; `free` of a foreign pointer aborts. API in `runtime.h`: `runtime_low_alloc/calloc/aligned/free/live`, and macros `bb_guest_malloc/calloc/aligned_alloc/free` that map to the libc functions on every other platform, so the call sites are identical everywhere. Converted sites: `runtime_thread.c` (`new_thread` GuestThread, `attr_init` ThreadAttr + its free, `tls_block_alloc` 64-aligned TCB/static TLS block), `runtime_mutex.c` (GuestAttr, GuestMutex, GuestCond, every malloc/free), `runtime_rwlock.c` (Rwlock; the per-thread `Holder` records are host-internal and stay on the heap), `runtime.c` `guest_tls_get_addr` (dynamic TLS module blocks). **Not converted, because the guest only ever gets an integer id:** semaphores (`runtime_sema.c`, id), ajm contexts (id, index into a table), trophy handles (id), file descriptors (`runtime_file.c`; `Listing` is host-internal), equeues (`s64` handle in `equeue.cpp`). Thread stacks were already served by `runtime_low_map`. The malloc-below-1-TiB `MACOS-STUB(M3)` in `probe.c main()` is replaced by a comment.
Not done: I did not audit every `new`/`malloc` in `gpu/shadps4` core libraries for pointers handed to the guest (checked equeue and videoout handles: integers).

### Task 4: refused fixed maps
`runtime_memory.c place()`: on Apple a `MAP_FIXED` request outside `[USER_MIN, USER_MAX)` logs `MACOS: refused fixed map addr=0x... size=0x... (outside [0x7000000000, 0xfc00000000))` to stderr and returns `NO_MEMORY` as before. Seen in the test output for `0x1000000000` and `0x100000000`.

### Task 5: Darwin signals
- `probe.c fault()`: RIP via `UC_RIP` = `uc_mcontext->__ss.__rip` (macro already in place from M1; the stub tag is removed because the test above now runs it), fault address `info->si_addr`.
- **Bug found and fixed (matters for M5):** on Darwin a **protection fault (write to a write-protected, mapped page) raises SIGBUS (10), only unmapped addresses raise SIGSEGV (11)** (measured, `signals-darwin-test`). `fault()` offered faults to the GPU page tracker only for `sig == SIGSEGV`, so write-protected guest pages (the GPU's dirty tracking) would never have reached `bbgpu_handle_fault`. Apple branch: `(sig == SIGSEGV || sig == SIGBUS)`. The `runtime_fault_recover` path already accepted both.
- `bbgpu_handle_fault`/`bbgpu_dump_guest_writes`/`Common::GetRip`/`IsWriteError` already had Darwin branches (`__ss`, `__es.__err`); `IsWriteError` verified under Rosetta (err 4 read, 6 write).
- `sigaltstack`: works under Rosetta with a host-allocated stack (tested), but `probe.c main()` still installs its handlers with `SA_SIGINFO` only, **exactly as the Linux path does** (no alternate stack, handler runs on the faulting stack below the red zone). Adding `SA_ONSTACK` + per-thread `sigaltstack` is a possible later hardening, not done.
- `watchdog()` Apple branch: every Mach thread of the task (`task_threads` + `pthread_from_mach_thread_np`) except the current one gets `SIGUSR2` by `pthread_kill`, 20 ms apart, then `_exit(128+sig)`. Not async-signal-safe (the two Mach/pthread calls take locks); acceptable for a handler that exits, but it can hang if the interrupted thread holds the pthread list lock. Covers GPU/SDL threads too, not only guest threads.

### Task 6: guest TLS
- **What I used:** the Windows design, as instructed. On macOS x86_64 GS is the pthread TSD base: a thread's `pthread_setspecific(key)` value is at `gs:[key*8]`. `runtime_thread.c`: `runtime_thread_tcb_offset()` = `pthread_once(pthread_key_create(&key, NULL))` then `key*8` (fails with exit 21 if it does not fit 31 bits); `set_gs(base)` = `pthread_setspecific(key, base)`. `probe.c patch_tcb_loads()` (now compiled for `_WIN32 || __APPLE__`) rewrites every `65 48 8b 04 25 00 00 00 00` (`mov rax, gs:[0]`) in executable segments to the displacement; Apple call site prints `Guest TCB loads redirected to the pthread TSD slot gs:[0x...]: <count>` and warns if the count is 0.
- Measured: key **262**, displacement **`gs:[0x830]`** in the test process (libpthread hands out the key; the value is computed at run time, never hard-coded). Verified under Rosetta, per thread, with inline asm and with the patched instruction in RWX memory.
- **shadPS4 does something different** (`src/core/tls.cpp`, `__APPLE__ && ARCH_X86_64`): it leaves the guest's `fs:` accesses alone and makes **FS** a real segment: a 4 KiB LDT page per thread (`i386_set_ldt`, selector loaded with `mov %0,%fs`), with a `.zerofill` region at `0x4000..0x3fc000` reserved at link time for the LDT pages, TCB copied into it. That keeps every `fs:` form working (not only the 9-byte load) but needs the low 4 GiB region and `i386_set_ldt` under Rosetta (not tested here). I did not use it because the spec said to mirror the Windows design.
- Known limit, same as Windows: only the exact 9-byte `mov rax, gs:[0]` is redirected. If the real ELFs contain other `gs:` forms (other destination registers, `gs:[8]`, ...), they would read TSD garbage. **The patched-load count for `eboot.bin`/`big2`/`big3` is unknown (not run).** If Task 9 shows other forms, the LDT approach above is the fallback.
- `watchdog()`: `tgkill` replaced as described in Task 5.

### Task 7: CPU detection under Rosetta
`check_cpu()` (Apple only) now always logs the cpuid result; `cpu_patch_image()` logs its decision (Apple only).
```
ROSETTA_ADVERTISE_AVX=1:
CPU: cpuid avx=1 avx2=1 fma=1 bmi1=1 bmi2=1 lzcnt=1 movbe=1 popcnt=1 sse4.2=1 osxsave=1 xcr0=0x7 (ROSETTA_ADVERTISE_AVX=1)
unset:
CPU: cpuid avx=0 avx2=0 fma=0 bmi1=0 bmi2=0 lzcnt=0 movbe=0 popcnt=1 sse4.2=1 osxsave=0 xcr0=0 (ROSETTA_ADVERTISE_AVX=(unset))
STOP: unsupported CPU VirtualApple @ 2.50GHz: missing AVX (needs Intel Core 4th generation or newer, or AMD Ryzen)   [exit 24]
unset + BB_CPU_PATCH=0: STOP ... missing AVX, BMI1, MOVBE, LZCNT   [exit 24]
```
Decisions with the variable set: BMI1, LZCNT and MOVBE are all reported present, so `host_missing()` is 0 and `cpu_patch` has nothing to rewrite (it logs "has nothing to rewrite"; the code path that rewrites and the thunks are covered by `cpu-patch-test` under Rosetta, 800 breakpoints). Without the variable AVX is missing and `bb-probe` stops. `run.sh` now exports `ROSETTA_ADVERTISE_AVX=1` on Darwin.

### Task 8: smoke
`bb-probe --vulkan-only` existed but returned `-9` (VK_ERROR_INCOMPATIBLE_DRIVER): MoltenVK is a portability driver and the instance did not ask for it. Apple-only branch in `src/vulkan_smoke.c`: `VK_KHR_portability_enumeration` + `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`, and `VK_KHR_portability_subset` on the device. Result under Rosetta (with `VULKAN_SDK` set in the environment, no `VK_ICD_FILENAMES`): `Vulkan: Apple M4 Max; command submission + 4096-byte readback PASS`.

### Task 9: not run
No game dump. Nothing in this list was exercised on real game code: ELF mapping, relocations, the patched-TLS count, guest thread creation with real stacks, fibers (`runtime_fiber.c`), file I/O path handling and `O_DIRECT`/`pread` sizes (`runtime_file.c`), time (`runtime_time.c`, `runtime_rtc.c`), save-data directory. `last_run.log`: none. No runtime fix was made from a game run. Fixes below came from tests and review.

### Task 10: Xbyak and `ms_abi` hooks
Not exercised by the game (they only run once GNM runs / guest code reaches the hook sites): **not yet verified in the game**. Checked separately: (a) Xbyak JIT under Rosetta, standalone (`xbyak.h` from `gpu/third_party`): a generated function with `vzeroupper`/`add`/`ret`, called from the main thread and from 8 threads, correct; Xbyak on Apple maps with `MAP_JIT` (`XBYAK_USE_MAP_JIT`), which works for a non-hardened x86_64 binary under Rosetta (separate check: `MAP_PRIVATE|MAP_ANON|MAP_JIT` RWX executes). The real use, `flatten_extended_userdata_pass.cpp`, is not run. (b) The `ms_abi` hook callbacks (`runtime_pad.c`, installed by `probe.c install_guest_hooks`) have no test on any platform; nothing Rosetta-specific is expected (compiler-generated convention on both sides), but it is unverified.

### Every runtime fix / Darwin substitution made in M3
- `src/runtime_memory.c`: Darwin pool = mach memory entry (`pool`, `host_map`, `host_discard`); `place` logs refused fixed maps; constants from `bb_user_range.h`.
- `src/runtime_lowalloc.c` (new) + `src/runtime.h`: low allocator and `bb_guest_*` macros; call sites in `runtime_thread.c` (`new_thread`, `attr_init`, `attr_destroy`, `tls_block_alloc`), `runtime_mutex.c`, `runtime_rwlock.c` (`create_locked`, `rw_destroy`), `runtime.c` (`guest_tls_get_addr`).
- `src/runtime_thread.c`: Apple `runtime_thread_tcb_offset` and `set_gs` (pthread TSD key).
- `src/probe.c`: `patch_tcb_loads` enabled on Apple and called; `fault()` GPU path accepts SIGBUS; `watchdog()` Mach thread enumeration; `check_cpu()` log; the three M3 stub tags removed.
- `src/cpu_patch.c` `cpu_patch_image`: decision log (Apple).
- `src/vulkan_smoke.c`: portability enumeration (Apple).
- `gpu/shim/bb_user_range.h` (new), `gpu/shim/bbgpu.cpp`, `gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp`, `gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp`: Apple user range.
- `run.sh`: exports `ROSETTA_ADVERTISE_AVX=1` on Darwin.
- `build.sh`: three new tests wired for Darwin only (`lowalloc-darwin-test`, `tls-darwin-test`, `signals-darwin-test`; the last one links like `bb-probe`).
- Tests/tools: `tests/test_lowalloc_darwin.c`, `tests/test_tls_darwin.c`, `tests/test_signals_darwin.c`, `tests/measure_pool_writeback.c`.

### Open issues / for the reviewer and M4
1. **Task 9 and the game acceptance are open.** Needs `BB_GAME_DIR` (and `python3 scripts/prepare.py`/`link_modules.py` working on macOS). First things to read in the log: `Guest TCB loads redirected ...: N` (N=0 or a few `gs:` forms left would break TLS), `MACOS: refused fixed map` lines, any `STOP:`.
2. TLS covers the exact 9-byte `gs:[0]` load only (see Task 6); shadPS4's LDT/FS approach is the fallback and is untested under Rosetta here.
3. Discard commits pages (`memset`). If M5 shows RSS growth after large releases, try `MADV_FREE_REUSABLE` after the memset or re-creating the entry range; swapping under pressure is now possible for the pool (it is anonymous memory) and was not measured.
4. Thread stacks from `runtime_low_map` and the TLS block of a finished thread are never freed (existing behaviour on all platforms); the Darwin low range is 31 GiB, so only a game that churns thousands of threads would notice.
5. `fault()` and `bb-probe` still use no alternate signal stack (as on Linux). `watchdog()` is not fully async-signal-safe.
6. Linux and Windows were not compiled in this session; changes outside `#ifdef __APPLE__` are: `runtime.h` macros mapping `bb_guest_*` to libc, `patch_tcb_loads` `#if` regrouping in `probe.c`, `bb_user_range.h` include in `runtime_memory.c`/`bbgpu.cpp`/`buffer_cache.cpp`/`vk_rasterizer.cpp` (needs the repo root and `gpu/shim` on the include path, which both CMake and `build.sh` already have), `HotRegion` in `buffer_cache.cpp`. `src/runtime_lowalloc.c` is empty on non-Apple; the Windows/Linux `src/runtime*.c` glob includes it.
7. MoltenVK is found through `VULKAN_SDK`; `run.sh` does not set it. M4/M6 must decide how the ICD and `libvulkan.1.dylib` are located (packaged app).
8. Anything M4: the window abort stub (`gpu/shim/window.cpp`) is untouched.

### Needs a decision from the owner
- Provide a game dump path (`BB_GAME_DIR`) to finish Task 9, or accept M3 as "runtime verified, guest unverified" and carry Task 9 into M4.
- Keep the TSD-displacement TLS (spec) or move to shadPS4's LDT/FS approach if the real ELFs contain other `gs:` forms.

### Task 9 (done in M4)

Date 2026-11-02, `macos` branch, MacBook Pro M4 Max 48 GB, macOS 26, x86_64 under Rosetta, `ROSETTA_ADVERTISE_AVX=1`, `BB_GAME_DIR=/Users/akserg/Projects/CUSA02344` (TITLE_ID CUSA02344, APP_VER 01.00), `BB_EXE=u1`.
**Verdict: the guest boots and runs far past its first GNM/VideoOut activity (about 8-9 s of game code: ~40 guest threads incl. FIOS, job workers, GPU interrupt handlers, audio/Ajm, pad, UserService; levels/pak files opened) and then stops on a RENDERER-side assertion (`Failed to get buffer device address`, sparse buffer, see M4 Result). No guest SIGILL, no loader/runtime failure left.** Loader/runtime vs renderer separation is in the "Fixes" table.

**gs-load counts** (`mov rax, gs:[0]` = `65 48 8b 04 25 00 00 00 00`; `link_modules.py` rewrites the games' `fs:[0]` loads to this form and prints `fs->gs patched=N`; `probe.c patch_tcb_loads` rewrites the displacement at load and prints `Guest TCB loads redirected to the pthread TSD slot gs:[2264]: N`):
| ELF | `link_modules` fs->gs patched | `patch_tcb_loads` at run time | Zydis scan of the linked image: exact patched form |
|---|---|---|---|
| `eboot.bin` (U1) | 1953 | **1953** (run, log line above) | 1953 (all in the main ELF; libc.prx 0, libSceFios2.prx 0) |
| `big2-ps4_Shipping.elf` (U2) | 7924 | not run | 7924 |
| `big3-ps4_Shipping.elf` (U3) | 9266 | not run | 9266 |
TSD displacement in this run: key 283, `gs:[2264]` (0x8d8); computed at run time.

**Scan for other `gs:`/`fs:` accesses** (`tools/scan_gs_fs.c` + `tools/scan_gs_fs.py`, Zydis linear sweep over the executable segments of `out/<exe>/boot-linked.bin`): U1 has 6294 other decodes in the main ELF, 399 in libc.prx, 503 in libSceFios2.prx. **All of the main-ELF ones lie at image offset >= 0x76ad91, i.e. after the import stub table that starts at 0x768a20** (U2: stubs 0xde63a0, lowest hit 0xde93b6; U3: stubs 0x139c5a0, lowest hit 0x139ef2f): the sweep walks into relocation/NID tables and ASCII text that live in the same R+X segment (the hits are things like `and gs:[rdi+0x66], ch` = the bytes of a format string). Before the stub table there is **no** unpatched `gs:`/`fs:` instruction in any of the three main ELFs. **Absolute `fs:[disp]`/`gs:[disp]` with a small displacement (the TLS shape): 0 in all three ELFs.** For libc.prx / libSceFios2.prx the hits begin at module +0x82971 and +0x3d22e inside text tables; the sweep cannot tell code from rodata there, and I did not review the remaining 33 + 12 hits whose neighbourhood is not obviously ASCII one by one (they are listed by running the tool). **Not proven for those two modules**, but the runtime ran their code (libc `_init_env`, Fios2 threads, file reads of `.psarc`) without any TLS fault, which is what an unpatched `gs:` form would give. **No unpatched form found, so the LDT/FS fallback (M3 Review item 1) is not needed.**

**Fixes** (in the order of the spec list; "R" = loader/runtime, "G" = renderer/GPU side):
| # | Failure | Class | Fix (file:function) |
|---|---|---|---|
| 1 | `WindowSDL: No available video device` (the original 2026-11-02 failure) | G (host window) | Cocoa main thread, M4 task 1: `src/probe.c main()/probe_main()`, `gpu/shim/bbgpu.cpp bbgpu_init`. **`dispatch_main()` from the spec does not work**, see M4 Result |
| 2 | `ASSERTION vaddr == (void*)pageAddr` in `AllocateMemory` (memory-area-ps4.cpp:134), `int 0x41` at guest 0x461cd6 | R (macOS-specific: M2 USER_MIN) | The game places its memory areas from a 64-bit cursor global initialised to `0x1000000000` (image+0x955250 in U1), rounds it up to 4 GiB per area and asserts that `sceKernelMapDirectMemory(hint, flags=0)` returned exactly the hint. A Rosetta process cannot map `[0x1000000000, 0x7000000000)` (re-measured today: `mmap` hint ignored, `mach_vm_allocate(FIXED)` = `KERN_NO_SPACE`), the runtime answered `0x7000000000`. `src/probe.c patch_memory_area_base()` (Apple only, signature search: `movabs r64,0xffffffff00000000` with a `mov r,[rip+G]` before it and `mov [rip+G],r` after it, G == `0x1000000000`) writes `BB_USER_MIN` into G. Log: `MACOS: memory area cursor at image+0x955250 moved from 0x1000000000 to 0x7000000000`. Areas then map at 0x7000000000 / 0x7100000000 / 0x7200000000 as asked. **Searched for other users of the constant only by running: none surfaced in 8 s; the other U2/U3 binaries were not run (the signature is compiler-generated, may not match; a missing match prints a WARNING)** |
| 3 | `ASSERTION chunkSetNameIsValid` in `IsChunkSetEntirelyWithin` (game-loading.cpp:504), `int 0x41` at 0x495388 | R, **not macOS-specific, data** | The dump has no `/app0/shareddata/chunks.txt` (`Runtime: open(/app0/shareddata/chunks.txt) -> not found`; it is not in the dump root, nor in any of bin/core/uiextra/data.psarc: manifests listed). `LoadChunksTxt` tolerates that, `g_ChunkSets` stays empty, and the first level load asserts. The runtime already reports every PlayGo chunk installed (`runtime_services.c`), so `src/probe.c patch_missing_chunks_txt()` (Apple only, only when the file is missing, signature search around the assertion string) turns the assertion path into `mov al,1; jmp epilogue`, i.e. an unknown chunk set counts as installed (the function already returns true for a null name). **Workaround, not a fix: needs an owner decision** (is the dump incomplete? upstream Linux/Windows would hit the same assertion with this dump; I could not check) |
| 4 | none | R | file I/O (`runtime_file.c`): FIOS opened `bin.psarc`, `core.psarc`, `data.psarc` and many `.pak` files without error; new diagnostic `BB_FILE_TRACE=1` prints every successful guest `open` (`File trace: open(...)`). Many `-> not found` lines remain for files that the game probes optionally (`effect1/*.bin`, `bpdata1/particle.json`, `lut0/default-color-gray.dds`, `movie1/texture.txt`): the psarc archives were NOT extracted (LINUX_BUILD.md "optional but recommended"). Whether they matter is for M5 |
| 5 | none | R | ELF mapping, relocations (`applied 21330 relocations`), TLS, threads, fibers (a fiber ran: `fiber_main` frames in the first fault trace), time, save data (`SaveWorker` thread created, `user/` populated): no failures |

**Last 30 lines of `user/last_run.log`** (the output of `out/bb-probe` with the `run.sh` environment, saved as `user/last_run.log` by hand, since nothing in the repo writes that file; it is the 8 s run that gets past the first GNM/VideoOut activity and ends in the renderer assertion):
```
Runtime: audio port 17 opened (type 4, 1 ch, float, 1024 frames)
Runtime: Ajm context initialized (ATRAC9 via LibAtrac9)
Runtime: guest thread 'brbDriver' created (stack=65536, prio=272)
Runtime: guest thread 'snd_stream_parsing_thread' created (stack=65536, prio=700)
Runtime: guest thread 'snd_stream_reader_thread' created (stack=65536, prio=700)
Runtime: UserService initialized (user 1 logged in)
Runtime: pad opened for user 1 (SDL gamepad or keyboard)
Runtime: open(/app0/u1data/build/main/lut0/default-color-gray.dds) -> not found
Runtime: open(/app0/u1data/build/main/effect1/particle.bin) -> not found
Runtime: open(/app0/u1data/build/main/effect1/particle-ingame.bin) -> not found
Runtime: open(/app0/u1data/build/main/effect1/texturedict1/chunk-1-tstrm.bin) -> not found
Runtime: open(/app0/u1data/build/main/effect1/texturedict1/chunk-1-tstrm-1.bin) -> not found
Runtime: open(/app0/u1data/build/main/movie1/texture.txt) -> not found
Runtime: UserService initialized (user 1 logged in)
Runtime: system module 0xa0 loaded (host implementation)
Runtime: system module 0x2c loaded (host implementation)
Runtime: system module 0xac loaded (host implementation)
Runtime: system module 0xb2 loaded (host implementation)
Runtime: open(/app0/u1data/build/main/effect1/world-0.bin) -> not found
Runtime: open(/app0/u1data/build/main/effect1/world-0-ingame.bin) -> not found
Runtime: open(/app0/u1data/build/main/effect1/world-1.bin) -> not found
Runtime: open(/app0/u1data/build/main/effect1/world-1-ingame.bin) -> not found
Runtime: SystemService: splash screen hidden
Runtime: open(/app0/u1data/build/main/effect1/proto.bin) -> not found
Runtime: guest thread 'SaveWorker' created (stack=65536, prio=700)
Runtime: open(/app0/u1data/build/main/bpdata1/particle.json) -> not found
GPU [Debug] <Critical> buffer.cpp:214 Create: Assertion Failed!
Failed to get buffer device address (size 0x100000000, type 4, flags { SparseBinding | SparseResidency })
STOP: GPU library assertion failed (see GPU log above)
exit 23 after 8s
```
Task 10 (Xbyak JIT in `flatten_extended_userdata_pass.cpp`, `ms_abi` hooks): still **not verified in the game**. The first frame never rendered, so the shader recompiler did not run; the log shows `Callbacks: no thunk memory` and `Job wait: no thunk memory, not patched` (those game-callback patches did not find thunk memory; not investigated, it may be the normal U1 message).

### Review (Opus 5.5, 2026-11-01)

Accepted as **runtime verified, guest unverified**. Task 9 (boot to first GNM/VideoOut) moves to M4's
preconditions; it needs the game dump, which is not yet available.

Decisions:
1. **TLS: keep the TSD design** (`gs:[key*8]`, `patch_tcb_loads`, `pthread_setspecific`). The shipping
   Windows build uses exactly this mechanism (only the `mov rax, gs:[0]` form is redirected) and runs all
   three games, because upstream's `link_*.py` normalises guest TCB loads to that form (comment above
   `patch_tcb_loads` in probe.c). shadPS4's LDT/FS approach stays a fallback only if a real ELF shows an
   unpatched `gs:` access (would fault at a low address under Rosetta).
2. **Pool backend: mach memory entry, accepted** (10 GB written in 120 s with the file backend).
   Discard-by-memset keeps pages resident; revisit with `madvise(MADV_FREE_REUSABLE)` only if M5 shows
   memory growth.
3. **Linux regression: PASS** (reviewer, 2026-11-01). Branch `macos` at `4b735e9` built on Ubuntu 24.04 x86-64
   (gcc-14, LTO off; the changed `liverpool.cpp`, `buffer_cache.cpp`, `page_manager.cpp` recompiled) and
   `bash build.sh --test` passed all 6 Linux tests, exit 0. Windows not verified (no MSYS2 environment).
4. Watchdog Mach enumeration not async-signal-safe: acceptable (diagnostic path, process is already dying).

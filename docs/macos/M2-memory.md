# M2 — Guest address space on Darwin/Rosetta

## Goal
`src/runtime_memory.c` gets a real Darwin backend. The guest layout the game requires is reservable
in a Rosetta x86-64 process:
- main executable image in `[0x400000, 0x80000000)` (`IMAGE_MIN`/`IMAGE_LIMIT`, `runtime_image_map`)
- GPU-visible memory in `[0x0800000000, 0x1000000000)` (`runtime_low_map`, `USER_MIN`)
- direct memory + flexible memory as one backing object mapped at many guest addresses (aliasing)

## Preconditions
- M1 done; its Result lists the stubs in `runtime_memory.c`.

## Background (read before coding)
- Upstream Linux: one `memfd_create` of `POOL_SIZE+FLEX_SPAN`, `mmap(MAP_SHARED|MAP_FIXED, pool_fd, phys)`
  per guest mapping → aliasing for free. Discard = `fallocate(PUNCH_HOLE)`.
- Upstream Windows: `CreateFileMapping2` section + `MapViewOfFile3`.
- Darwin options, in order of preference:
  1. A regular sparse file (`mkstemp` in `$TMPDIR`, `unlink`, `ftruncate`) + `mmap(MAP_SHARED|MAP_FIXED)`.
     Identical code shape to Linux. Discard = `fcntl(F_PUNCHHOLE)` (APFS supports it).
     POSIX `shm_open` is **not** suitable: macOS caps shm segments far below the sizes needed.
  2. `mach_vm_map` with a `mach_make_memory_entry_64` object + `mach_vm_remap` for aliases.
     More work; only if (1) fails under Rosetta.
- **PAGEZERO**: a macOS x86_64 executable reserves `[0, 4 GiB)` as `__PAGEZERO` by default, which makes
  `0x400000` unmappable. Link `bb-probe` with `-Wl,-pagezero_size,0x1000 -Wl,-no_pie` (M1 should already
  have this; verify with `otool -l out/bb-probe | grep -A3 PAGEZERO`).
- **Page size**: `PAGE` is 16 KiB in the runtime. Rosetta reports 4 KiB `vm_page_size` to translated
  processes but the kernel still works in 16 KiB pages. Any `MAP_FIXED` at a 4 KiB-but-not-16 KiB
  aligned address will fail; the runtime's 16 KiB granularity should keep us safe. Confirm.
- **W^X**: Rosetta-translated processes may map `PROT_READ|PROT_WRITE|PROT_EXEC` (x86 apps expect it).
  Confirm with a test; if it fails, the image must be mapped RW, written, then `mprotect` RX.

## Tasks
0. Run `bash build.sh --test` (not run in M1). Record per-test pass/fail; fix Darwin-specific failures.
   All runs from here on need `ROSETTA_ADVERTISE_AVX=1` in the environment (see M1 Review).
0b. **AVX execution test**: `tests/test_avx_rosetta.c` (x86_64, built only on APPLE) executes AVX/AVX2
   instructions via inline asm (`vaddps ymm`, `vfmadd231ps`, `vpbroadcastd`, `vpermd`) and checks results;
   also BMI1/LZCNT/MOVBE/POPCNT. Run with and without `ROSETTA_ADVERTISE_AVX=1` and record both.
   If AVX does not execute correctly, STOP the milestone and report — the whole strategy depends on it.
1. Implement the Darwin `pool()`, `host_map()`, `host_unmap()`, `host_protect()`, `host_discard()`,
   `host_commit()`, `runtime_memory_reserve()` in `runtime_memory.c` under `#ifdef __APPLE__`
   (new branch between the `_WIN32` and Linux ones).
2. Write `tests/test_memory_darwin.c` (built only on APPLE in `build.sh --test`):
   - map the same backing offset at two guest addresses, write through one, read through the other
   - `runtime_image_map(64 MiB)` returns an address in `[0x400000, 0x80000000)`
   - `runtime_low_map(1 GiB)` returns an address in `[0x0800000000, 0x1000000000)`
   - `mprotect` a page RWX succeeds, or document that it does not
   - discard then re-touch a region works
3. Make `bash build.sh --test` run `tests/test_runtime.c` and the new test on macOS; both pass.
4. `MAP_FIXED_NOREPLACE` (`runtime_low_map`, `runtime_image_map`, `cpu_patch.c` thunk allocation): Darwin has no equivalent flag. Use `mmap` with the address as a hint (no `MAP_FIXED`), accept only if result == requested address, else `munmap` and continue the probe loop.
5. Remove the M1 stubs for memory.

## Acceptance
- `arch -x86_64 bash build.sh --test` passes `runtime-test` and `memory-darwin-test`.
- `vmmap <pid>` on a paused `bb-probe` shows the image region below 2 GiB and the pool mapped.

## Hazards
- `MAP_FIXED` silently replacing Rosetta's own translation cache or dyld shared cache regions.
  Use `MAP_FIXED` only inside ranges `runtime_memory_reserve()` has probed as free (`mmap` without
  `MAP_FIXED` + check address, or `mach_vm_region` walk). Document the probe strategy in Result.
- `F_PUNCHHOLE` requires the file to be on APFS; `$TMPDIR` is.

## Result

**M2: guest address space on Darwin/Rosetta** — date 2026-11-01, branch `macos`, MacBook Pro M4 Max, macOS 26.6.2, Apple clang, x86_64 under Rosetta. Source commits (after `fedc79b`): `59116e7` tests + test fixes, `168cfac` backend, `33a6e4d` NOREPLACE replacement; the Result commit follows.
All runs used `ROSETTA_ADVERTISE_AVX=1` unless stated. Upstream reference used: shadPS4 `src/core/address_space.cpp` (APPLE / `ARCH_X86_64` branches, read via GitHub raw): for the USER range split, constants and "no file-backed exec mapping". Everything else was measured here.

### Acceptance (all pass)
| Item | Result |
|---|---|
| `ROSETTA_ADVERTISE_AVX=1 bash build.sh --test` | exit 0, all 8 tests pass (table below) |
| aliasing: write through one mapping visible through another | PASS. Same offset at 3 guest addresses (one fixed, two runtime-chosen, one a 4 MiB short view), a `+16 KiB` sub-range, and the runtime's host-side backing view (`runtime_memory_write_backing`) all see the same bytes; a different offset does not alias |
| `runtime_image_map(64 MiB)` in `[0x400000, 0x80000000)` | PASS: returned `0xbe00000` in the test binary (see "image base" below) |
| `runtime_low_map(1 GiB)` in `[0x0800000000, 0x1000000000)` | PASS: `0x800000000`; second map `0x840004000` (8 MiB, after the 16 KiB gap) |
| discard then re-touch reads zero | PASS: direct 32 MiB released/reallocated at the same offset reads zero in every 4 KiB; flexible 4 MiB unmapped/remapped reads zero |
| RWX mprotect | WORKS (details below) |
| vmmap of a paused process | below |
| `grep -rn "MACOS-STUB(M2)" src gpu` | no output (0 M2 stubs left) |

### Task 0: `build.sh --test` (first run, then fixed)
First run, before any M2 code: pad-test aborted (dyld: `@rpath/libvulkan.1.dylib` not found, test binaries had no SDK rpath); `test_runtime.c` did not compile (`pthread_barrier_t` does not exist on macOS); after that, `runtime-test` hit the M3 `set_gs` stub (`exit(21)`), and then the pool stub (`Assertion ... direct_memory`, line 187); `test_cpu_patch.c` did not compile (`ucontext` needs `_XOPEN_SOURCE`, `gregs[REG_RIP]`, ELF-style asm labels) and after that failed `assert(image==0x20000000)` because test binaries were linked with the default 4 GiB `__PAGEZERO`.
Fixes: SDK rpath added to `libraries` on Darwin (removed from `gpu`, avoids a duplicate-rpath warning); `testpie` = `-Wl,-no_pie -Wl,-pagezero_size,0x1000` on the memory-mapping test links; `pthread_barrier_*` shim in `compat_darwin.h`; `set_gs` on Darwin records the TCB in a `__thread` variable instead of exiting (still `MACOS-STUB(M3)`: no guest code can read `gs:[0]` yet); `test_cpu_patch.c` Darwin ucontext (`__ss.__rip`), `.globl _name` Mach-O asm labels, `MAP_FIXED_NOREPLACE` as hint.
Final table (`out/*-test`, all `-Werror`):
| Test | Result |
|---|---|
| pad-test | PASS |
| runtime-test (`test_runtime.c`) | PASS |
| file-mods-test | PASS |
| sema-test | PASS |
| content-test | PASS |
| cpu-patch-test (50 cases x 400 trials, rewritten BMI1/LZCNT/MOVBE code executed under Rosetta, 800 breakpoints) | PASS |
| avx-rosetta-test | PASS |
| memory-darwin-test | PASS |

### Task 0b: AVX execution (`tests/test_avx_rosetta.c`)
Executes with inline asm and checks results: `vaddps/vmulps/vsubps ymm`, `vfmadd231ps ymm`, `vpbroadcastd/vpaddd/vpermd ymm`, ymm state across a preemption window, `andn/blsr/blsi/blsmsk/tzcnt`, `lzcnt` (63/64/15), `movbe` 16/32/64 load+store, `popcnt`, and AVX/AVX2/FMA in 4 concurrent threads. SIGILL/SIGSEGV per group is caught and reported.
**Verdict: AVX executes correctly.** Output with `ROSETTA_ADVERTISE_AVX=1`:
```
cpuid: avx=1 fma=1 avx2=1 bmi1=1 bmi2=1 movbe=1 popcnt=1 lzcnt=1 osxsave=1
xcr0=0x7 (ymm state enabled)
PASS AVX vaddps... / FMA3 / AVX2 / AVX ymm state / BMI1 / LZCNT / MOVBE / POPCNT / 4 threads
PASS: AVX, AVX2, FMA, BMI1, LZCNT, MOVBE, POPCNT execute and give correct results
```
Output **without** the variable:
```
ROSETTA_ADVERTISE_AVX=(unset)
cpuid: avx=0 fma=0 avx2=0 bmi1=0 bmi2=0 movbe=0 popcnt=1 lzcnt=0 osxsave=0
PASS (all 8 groups and the thread test, same results)
```
Finding: the variable only changes what **cpuid reports**; Rosetta translates and executes the AVX/BMI/LZCNT/MOVBE instructions correctly either way. Without it, `check_cpu()` in `bb-probe` stops (cpuid says no AVX) and `runtime`'s CPU-patch decisions (`BB_CPU_PATCH`: rewrite BMI1/LZCNT/MOVBE when cpuid lacks them) would also see "no AVX". Keep exporting it. Not tested: AVX-512 (not translated), x87/MXCSR state under signals.

### Backend chosen: unlinked sparse file + `mmap(MAP_SHARED|MAP_FIXED)` (spec option 1)
`pool()`: `mkstemp` in `$TMPDIR` (else `/tmp`), `unlink`, `ftruncate(POOL_SIZE + FLEX_SPAN)` (6.0 GiB at the default 5056 MiB pool), one host-only view with `mmap(MAP_SHARED|MAP_NORESERVE)` at hint `USER_MAX + 1 TiB` (`backing_base`); every guest mapping is `mmap(MAP_SHARED|MAP_FIXED, pool_fd, phys)`. Discard is `fcntl(F_PUNCHHOLE)` (APFS), falling back to `memset` through `backing_base` if it fails. `host_commit` is a no-op. The mach_vm option (2) was **not needed**: aliasing, punch-hole semantics and mprotect all work with the file. Measured with a scratch program before coding: after `F_PUNCHHOLE` the pages read zero **through live mappings** (two aliases and the host view) and neighbouring pages keep their data.
Why not shm: spec background (size cap). Why this over mach entries: the Linux code shape stays identical and discard has exact zeroing semantics.
Caveat (open issue 3): the pool is file-backed, so dirty pages can be written to the SSD by the kernel.

### Address-space facts under Rosetta (measured; these changed the design)
1. **`__PAGEZERO`** is `0x1000` in `out/bb-probe` and in the test binaries (`otool -l | grep -A3 PAGEZERO`: `vmaddr 0 vmsize 0x1000`). With the default 4 GiB the 0x400000 / sub-2 GiB mappings are impossible (the `cpu-patch-test` failure above is that exact case).
2. **`[0x1000000000, 0x7000000000)` cannot be mapped at all** in a Rosetta x86_64 process: `mmap(MAP_FIXED)` returns `EACCES`; a hint there is ignored and the kernel returns `0x7000000000`. Bisected: last OK below `0xfbfffc000`, first bad `0xfc0000000` (the commpage `0xFC0000000-0xFFFFFFFFF`), then bad until `0x6fffffc000`, first OK `0x7000000000`; 385 samples inside all fail. `0x7000000000` through `0x7fff00000000` map fine. shadPS4 makes the same cut on macOS x86_64 ("GPU-reserved memory ranges from 0x1000000000 - 0x6FFFFFFFFF", `USER_MIN = 0x7000000000`, commpage ends the system-reserved range at `0xFBFFFFFFF`).
   Consequence (**decision made, please review**): on Apple `USER_MIN = 0x7000000000` (the `[USER_MIN, USER_MAX = 0xfc00000000)` guest range is 560 GiB and still < 2^40), `runtime_low_map` stops at `LOW_MAX = 0xfc0000000` (31 GiB of low space from `0x800000000`). The acceptance range `[0x0800000000, 0x1000000000)` for `runtime_low_map` still holds. Guests that `MAP_FIXED` outside `[0x7000000000, 0xfc00000000)` get `NO_MEMORY` on macOS (tested: `0x1000000000` and `0x100000000` refused, nothing replaced).
3. **Low 4 GiB is crowded**: Rosetta's `.aot` translation files, dyld, and the host binary's dylibs (SDL, MoltenVK, FFmpeg, ...) map between `0x1000` and about `0xa40000` and beyond, so `0x400000` is taken in the test binary (regions listed in the test output). `runtime_image_map` is first-fit with a 1 MiB step, so it landed at `0xbe00000` (and a second 8 MiB map at `0xfe00000`). The PS4 base `0x400000` is not obtainable in a process that links this many dylibs; the spec requires only "below 2 GiB". The Uncharted `TGenericOffsetPtr` constraint (image pointers as [0, 2 GiB)) is satisfied.
4. Where non-hinted allocations go: lowest free address first (`mmap(NULL)` gave `0x2041f7000`), malloc'd memory lives at `0x6000xxxxxxxx` / `0x7f89xxxxxxxx` (see issue 2).

### Probe strategy for free ranges (hazard: never clobber Rosetta/dyld)
- **User range**: `runtime_memory_reserve()` (called from `pool()` and, on Apple, early in `main()` before any Vulkan/Metal/SDL allocation) maps the whole `[USER_MIN, USER_MAX)` once as `PROT_NONE|MAP_PRIVATE|MAP_ANON|MAP_NORESERVE` **without** `MAP_FIXED` (address is a hint); it is accepted only if the result equals `USER_MIN`, otherwise it unmaps and prints the occupants (`mach_vm_region` walk) and exits 21 (like the Windows path). Every later `MAP_FIXED` (host_map/unmap) lands inside that reservation, so it replaces only our own PROT_NONE pages. `host_map`/`host_unmap` refuse addresses outside the range (`in_user_range`). Unmapping re-reserves with a `PROT_NONE` `MAP_FIXED` anon mapping so the range stays ours.
- **Low and image ranges**: `bb_darwin_map_at()` in `compat_darwin.h`: `mmap` without `MAP_FIXED`, accept only `result == requested`, otherwise `munmap` the stray mapping and continue the probe loop. Used by `runtime_low_map`, `runtime_image_map`, and the `cpu_patch.c` thunk allocator (`near_image`). The kernel honours the hint whenever the range is free (verified for `0x400000`, `0x20000000`, `0x70000000`, `0x800000000`), and never replaces an existing mapping without `MAP_FIXED`.
- No `MAP_FIXED` anywhere else.

### Page size
`vm_page_size`, `getpagesize()`, `sysconf(_SC_PAGESIZE)` and `hw.pagesize` all report **4096** inside the Rosetta process; `vm_kernel_page_size` is 16384; `vmmap` says "Process VM page size: 4096, Kernel VM page size: 16384". Rosetta accepts `MAP_FIXED` at 4 KiB-aligned addresses (tested `0x401000`), so the 16 KiB runtime `PAGE` is only a runtime convention here, not a requirement; the runtime still rejects non-16 KiB-aligned guest addresses (tested) and uses 16 KiB-aligned file offsets. `mprotect` granularity under Rosetta is 4 KiB; the GPU page-tracking code (M5) works in its own page size and must not assume the host's.

### RWX finding
- anonymous `PROT_READ|WRITE|EXEC` `mmap` (non-fixed hint and fixed): **works** and the code executes (thunk memory in `cpu_patch.c`).
- `mmap(PROT_EXEC, MAP_SHARED, file_fd)` **fails with `EPERM`** (same as shadPS4's note that exec mappings cannot be file-backed on macOS), but `mprotect` of an existing RW `MAP_SHARED` file mapping to `RWX`, `RX` and back **works**, and code executes there. So `host_map` maps without `PROT_EXEC` and adds it with `mprotect` afterwards; guest `sceKernelMapDirectMemory(prot=RWX)` therefore works (tested, a `ret` runs). No need for the RW-then-RX workaround.

### vmmap (paused `memory-darwin-test`, `BB_TEST_PAUSE=15`, pid 30897)
`bb-probe` itself cannot reach the memory code without a real `boot.bin` (not available), so the paused process is the test binary, which links the same `src/runtime*.c`, with the same link flags as `bb-probe` (`-no_pie`, `__PAGEZERO` 0x1000).
```
Code Type: X86-64 (translated)    Process VM page size: 4096 bytes    Kernel VM page size: 16384 bytes
VM_ALLOCATE                       be00000-fe00000     [ 64.0M  64.0M  64.0M  0K] rw-/rwx SM=PRV           <- guest image (64 MiB, below 2 GiB)
VM_ALLOCATE (reserved)            fe00000-10600000    [ 8192K  ...                                         <- second image map region follows
mapped file                    7000000000-7004000000  [ 64.0M  8192K  0K  0K]  rw-/rwx SM=S/A   .../T/bb-guest-memory.rVOCsn   <- 64 MiB guest view of the pool
VM_ALLOCATE                    7004000000-fc00000000  [559.9G  0K  0K  0K]      ---/rwx SM=NUL               <- PROT_NONE user-range reservation
mapped file                   1fc00000000-1fd7c000000 [  5.9G  0K  0K  0K]      rw-/rwx SM=S/A   .../T/bb-guest-memory.rVOCsn   <- host view of the whole pool file (6.0 GiB)
```
Physical footprint 79.4 MB with 64 MiB image written and 8 MiB of the pool touched.

### Darwin substitutions made in M2 (non-stub)
- `runtime_memory.c`: new `#elif defined(__APPLE__)` host-mapping branch between `_WIN32` and Linux (Linux branch restored to upstream text); `USER_MIN` 0x7000000000 + new `LOW_MAX` on Apple; includes `<mach/mach.h>`, `<mach/mach_vm.h>`.
- `compat_darwin.h`: removed the `MAP_FIXED_NOREPLACE = 0` macro and `bb_darwin_hint_missed`; added `bb_darwin_map_at`; added `pthread_barrier_*` shim (mutex+cond).
- `cpu_patch.c` `near_image`, `runtime_memory.c` `runtime_low_map`/`runtime_image_map`: Apple branches use `bb_darwin_map_at`.
- `probe.c`: Apple calls `runtime_memory_reserve()` early (the Windows reason); `MACOS-NOTE(M2)` for malloc replaced by a tagged `MACOS-STUB(M3)` with the measurement.
- `runtime_thread.c`: Darwin `set_gs` records the TCB per thread (see Task 0).
- `build.sh`: SDK rpath in `libraries`, `testpie`, the two new tests wired for Darwin only (after the existing tests; AVX test first).
- Tests: `tests/test_avx_rosetta.c`, `tests/test_memory_darwin.c` (new), `tests/test_cpu_patch.c` (Darwin ucontext, asm labels, hint).

### Remaining stubs (5, none tagged M2)
`runtime_thread.c set_gs` (M3), `probe.c fault()` RIP (M3, untested), `probe.c watchdog()` (M3), `probe.c main()` malloc-below-1-TiB (M3, new tag, see below), `gpu/shim/window.cpp` (M4).

### Open issues / for the reviewer and M3
1. **Decision needed: `USER_MIN = 0x7000000000` on Apple.** Anything in the game or the GPU code that assumes guest user addresses start at `0x1000000000` would break (I found no such constant in `src` or `gpu/shim`, but did not run a guest). The alternative is none: the kernel forbids the lower range.
2. **Host heap is not below 1 TiB** (Rosetta: malloc at `0x6000xxxxxxxx`, `0x7f89xxxxxxxx`). `probe.c` says handles given to the guest (thread handles, TLS/TCB blocks, mutex/sema handles) must stay below 1 TiB because the guest packs 40-bit pointers. On Windows `--disable-high-entropy-va` achieves this; on Linux the brk heap with `mallopt`. A low allocator is needed for M3: a custom `malloc_zone_t` or an arena carved from `runtime_low_map` for those `calloc`/`aligned_alloc` sites (`runtime_thread.c` `new_thread`/`tls_block_alloc`, `runtime_mutex.c`, `runtime_rwlock.c`, `runtime_sema.c`, `runtime_file.c`, `runtime_ajm.c`, `runtime_trophy.c`). Not done in M2.
3. **The pool is file-backed**: the kernel may write dirty guest pages to `$TMPDIR` (on the Data volume) under memory pressure and periodically; it is an unlinked sparse file, so disk use is only touched pages (up to ~6 GiB at worst) and a nearly full disk causes `SIGBUS`, not `ENOMEM`. If M5 shows SSD write traffic or stalls, switch to `mach_make_memory_entry_64` (anonymous shared) + `mach_vm_map`; discard would then need `memset`/re-create instead of punch-hole.
4. `0x400000` image base is not available (issue 3 of address-space facts). If the game requires exactly `0x400000` the host binary would have to be moved/linked small, or loaded via zerofill reservations like shadPS4 (`.zerofill` sections reserve `[0x400000, 0xFBFFFFFFF]` and the user range at link time). Not needed so far.
5. The reservation `mmap` of 560 GiB relies on the hint being honoured at `0x7000000000`; it works now (dyld, Rosetta and malloc zones are elsewhere), but a different macOS or a library that maps there first makes `runtime_memory_reserve()` exit 21 with the occupant list. Linking zerofill sections (shadPS4's approach) would make this deterministic.
6. `bb-probe` was not run on a guest (no `boot.bin`): `runtime_image_map` is exercised only by the test. `ROSETTA_ADVERTISE_AVX=1 out/bb-probe` still prints the usage line (precondition 3 passed).
7. Verified: `memory-darwin-test`, `runtime-test`, `sema-test` and `cpu-patch-test` also pass **without** `ROSETTA_ADVERTISE_AVX=1` (only `bb-probe`'s `check_cpu()` needs it). `bash build.sh --test` as a whole was run with it, as required.
8. `ld: warning: -no_pie is deprecated` remains (works).

### Review (Opus 5.5, 2026-11-01)

Accepted. Excellent measurements; the address-space facts are now the reference for later milestones.

Decisions:
1. **`USER_MIN = 0x7000000000` on Apple: accepted** (the kernel forbids the lower range; same as shadPS4).
   Missed dependency, fixed in M3: `gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp:684`
   hard-codes the GPU-heap use window `GpuUseLow = 0x1000000000, GpuUseHigh = 0x1200000000`. On Apple it
   must be `USER_MIN .. USER_MIN + 0x200000000`. Outside the window `LastGpuUse` returns "now", so the bug
   would not crash — it would silently disable dead-buffer detection (memory growth). Also grep
   `gpu/shadps4` (not only `gpu/shim`) for other `0x10'0000'0000`-style constants.
2. **Low allocator for guest-visible host objects: M3 task.** Required before any guest thread runs.
3. **File-backed pool: measure in M3, not M5.** macOS writes back dirty pages of shared file mappings
   on its own schedule, not only under memory pressure; guest GPU memory is rewritten every frame, so this
   could mean continuous SSD writes. M3 task 1 measures it; switch to `mach_make_memory_entry_64` if it writes.
4. **`0x400000` image base / zerofill reservation: not now.** Upstream already supports a non-0x400000
   base below 2 GiB. Adopt shadPS4's link-time `.zerofill` reservations only if `runtime_memory_reserve()`
   or `runtime_image_map` fails with the real game in M3.
5. **Fixed-address guest mappings below `USER_MIN`** now return `NO_MEMORY`. M3 logs every such refusal
   with the requested address and size (one line, `MACOS:` prefix) so a game that depends on it is obvious.

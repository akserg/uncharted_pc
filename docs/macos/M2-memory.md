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
_(fill in: date, commit, backend chosen and why, PAGEZERO/no_pie confirmation, page-size findings,
RWX finding, test output, vmmap excerpt, open issues)_

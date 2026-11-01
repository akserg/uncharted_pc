# M0 — Baseline on Linux x86-64

## Goal
Know what "working" looks like before touching macOS: a Linux x86-64 build that boots a game,
plus reference performance numbers to compare the Rosetta build against in M5.

## Preconditions
- A Linux x86-64 machine or VM with a Vulkan 1.3 GPU (not a software renderer). If no such machine is
  available, mark this milestone **skipped** in Result and use the README's Windows numbers as the baseline.
- Decrypted game dump (folder with `eboot.bin`, `big2-ps4_Shipping.elf`, `big3-ps4_Shipping.elf`,
  `sce_sys`, `u1data`, `u2data`, `u3data`).

## Tasks
1. Follow `docs/LINUX_BUILD.md` to build `out/bb-probe` and `out/gpu/libbbgpu.so`.
2. Run `bash build.sh --test` and record which tests pass.
3. `BB_GAME_DIR=... BB_EXE=u1 ./run.sh` — reach the main menu, start chapter 1.
4. Record FPS in three fixed spots (use the in-game overlay or `user/last_run.log` frame stats):
   U1 main menu, U1 chapter 1 beach (first playable area), U1 first combat.
5. Note total process RSS at the first combat spot (`ps -o rss`).

## Acceptance
- Build and `--test` results recorded.
- Perf table filled with CPU/GPU model, resolution, and FPS at the three spots.

## Hazards
- Linux build needs GCC 14 and a source-built protobuf (see LINUX_BUILD.md). Don't spend more than a
  couple of hours on Linux toolchain issues; the baseline is useful but not blocking.

## Result
**M0: Linux x86-64 build baseline** (date 2026-11-01, commit `bbd1b3a`)

- Machine: cloud container, Ubuntu 24.04 (noble), x86-64, 2 cores, 7 GB RAM, no GPU.
- Status: Build-only baseline. Runtime/perf SKIPPED: no Vulkan GPU or game dump in the build environment. Reference requirements: README (Haswell+/Ryzen, Vulkan 1.3, 6 GB VRAM, ~8 GB RAM).
- Build: `export CC=gcc-14 CXX=g++-14 BB_LTO=OFF BB_JOBS=2; bash build.sh` succeeds (after the two source fixes and one build.sh fix below). `libbbgpu.so` (303 ninja steps, ~17 CPU-min, ~9 min wall at -j2) and `bb-probe`, `bb-gpu-capabilities` built.
- Tests: `bash build.sh --test` passes 6/6 (after one test fix below).

### 1. Dependencies actually used

Everything came from apt (Ubuntu 24.04 noble) except SDL3 (built from source) and the two bundled/submodule sets. The GPU CMake configure picked all of these up (see `out/gpu/CMakeCache.txt`).

| Package / lib | Version | Source | Used by |
|---|---|---|---|
| gcc-14 / g++-14 | 14.2.0-4ubuntu2~24.04.1 | apt (noble-updates; not the PPA) | all (CC/CXX) |
| cmake / ninja-build / pkg-config | 3.28.3 / 1.11.1 / 1.8.1 | apt | build |
| SDL3 | 3.4.18 (tag `release-3.4.18`) | **source** (`/tmp/sdl3src/SDL`, built with GCC 13.3, installed to `/usr/local`; video: x11 + wayland dynamic, kmsdrm, offscreen, dummy) | bb-probe, libbbgpu (pkg-config `sdl3`) |
| libvulkan-dev (loader) | 1.3.275.0 | apt | all |
| Vulkan-Headers | v1.4.344 | submodule `third_party/Vulkan-Headers` (build.sh adds `-I` and `-DVulkan_INCLUDE_DIR`) | needs newer than distro |
| FFmpeg libavcodec/format/util/swscale/swresample (-dev) | 6.1.1 (avcodec 60.31, avutil 58.29, swscale 7.5) | apt | libbbgpu (videodec/avplayer); bb-probe directly (runtime_ajm.c) |
| libboost-dev | 1.83.0 (headers only) | apt | libbbgpu |
| libfmt-dev | 9.1.0 | apt | libbbgpu |
| robin-map-dev (tsl-robin-map) | 1.2.1 | apt | libbbgpu |
| libxxhash-dev | 0.8.2 | apt | libbbgpu |
| libvulkan-memory-allocator-dev | 3.0.1 | apt | libbbgpu |
| glslang-tools (`glslang`/`glslangValidator`) | 15.1.0 | apt | host shader compilation |
| libspirv-cross-c-shared-dev / spirv-cross | 2021.01.15 (0.54.0) | apt (installed; no direct reference found in gpu/CMakeLists.txt) | probably unused, see note |
| spirv-headers | 1.6.1+1.4.309 | apt | sirit (submodule) |
| libzydis-dev (Zydis) | 4.0.0 | apt | libbbgpu, bb-probe (`-lZydis`) |
| libxbyak-dev | 7.05 | apt (not used: CMake uses submodule `gpu/third_party/xbyak`, v3.71-1956) | see note |
| libx11-dev, libxcb1-dev, x11proto-dev, libwayland-dev | 1.8.7 / 1.15 / - / 1.22.0 | apt | `PkgConfig::X11` (non-WIN32 only) |
| libprotobuf-dev + protobuf-compiler | 3.21.12 | apt | **required** (bbnet: shadnet.proto, `find_package(Protobuf REQUIRED)`) |
| libminiupnpc-dev | 2.2.6 | apt (static `libminiupnpc.a` is picked by `find_library`) | **required** (bbnet net_upnp.cpp) |
| nlohmann-json3-dev | 3.11.3 | apt | **required** (bbnet) |
| libssl-dev / zlib1g-dev | 3.0.13 / 1.3 | apt | bbnet (httplib TLS + gzip) |
| mesa-vulkan-drivers (lavapipe) | 25.2.8 | apt | tests only (software Vulkan) |
| Submodules | imgui v1.62-5575, magic_enum v0.9.7, miniz 3.1.2-12, xbyak, LibAtrac9 v0.0.3-16, fsr-vulkan c64f093 (+3 local patches applied by build.sh), sirit, Vulkan-Headers | git submodule | |

Also installed (for SDL3 from source, not needed by the project itself): libxext/xcursor/xi/xrandr/xss/xkbcommon/xfixes/xtst-dev, libasound2-dev, libpulse-dev, libdrm/gbm/egl/gl-dev, libudev-dev, libdbus-1-dev, libdecor-0-dev, wayland-protocols, libibus-1.0-dev.

**protobuf / miniupnpc: both are needed.** gpu/CMakeLists.txt:170-212 builds `bbnet` (PS4 network and PSN libraries plus shadNet client for co-op) unconditionally. `find_package(Protobuf ...)` is REQUIRED and `protobuf::libprotobuf` is linked PUBLIC into bbnet and bbgpu; miniupnpc is `find_path/find_library ... REQUIRED`. `docs/LINUX_BUILD.md` says Protobuf must be built from source (v29, -fPIC); on Ubuntu 24.04 the apt 3.21.12 worked as is (the fallback `find_package(Protobuf REQUIRED)` module mode on line 173 handles it, `libprotobuf.so` is already PIC). shell.nix lists neither protobuf nor miniupnpc nor nlohmann_json, nor openssl/zlib, so the Nix path appears stale vs. the CMake.

Notes on possibly-unneeded installs: `libxbyak-dev` (CMake includes the submodule xbyak, and a `find_package` for it is absent) and `spirv-cross` libs have no reference in gpu/CMakeLists.txt; they are harmless. Not verified by removing them.

### 2. Patches and non-obvious fixes

The `bbd1b3a` tree did not build bb-probe out of the box with GCC 14.2 / Ubuntu 24.04. `libbbgpu.so` built fine. Full diff: `docs/macos/M0-linux-build-fixes.patch`.

1. `src/runtime_thread.c` : `host_priority()` is only referenced inside `#ifdef _WIN32`, so on non-Windows `-Wunused-function -Werror` fails. Marked `__attribute__((unused))`.
2. `src/runtime_trophy.c` : `-Wformat-truncation -Werror` on `snprintf(text,128,"%s",set.title)` (title is 256 bytes). Changed to `%.127s` (same truncation behaviour, explicit).
3. `build.sh` : `src/runtime_ajm.c` calls `avcodec_open2` directly, but bb-probe only linked `vulkan sdl3`; relied on transitive linking through libbbgpu, which Ubuntu's linker (`--no-copy-dt-needed-entries`) rejects ("DSO missing from command line"). Added `libavcodec libavutil` to both `pkg-config --cflags` and `--libs` calls.
4. `tests/test_pad.c` : the test `#include`s `runtime_pad.c`, which now references `_Thread_local int runtime_ndgi_open` (defined in `runtime_fiber.c`, not linked into pad-test). Defined it locally in the test before the include.

```diff
--- a/build.sh
+++ b/build.sh
-read -r -a includes <<< "$(pkg-config --cflags vulkan sdl3)"
+read -r -a includes <<< "$(pkg-config --cflags vulkan sdl3 libavcodec libavutil)"
-read -r -a libraries <<< "$(pkg-config --libs vulkan sdl3)"
+read -r -a libraries <<< "$(pkg-config --libs vulkan sdl3 libavcodec libavutil)"
--- a/src/runtime_thread.c
+++ b/src/runtime_thread.c
-static int host_priority(int prio) {
+static __attribute__((unused)) int host_priority(int prio) {
--- a/src/runtime_trophy.c
+++ b/src/runtime_trophy.c
-        snprintf(text,128,"%s",set.title); snprintf(text+128,1024,"%s",set.detail);
+        snprintf(text,128,"%.127s",set.title); snprintf(text+128,1024,"%s",set.detail);
--- a/tests/test_pad.c
+++ b/tests/test_pad.c
 #include <unistd.h>
+_Thread_local int runtime_ndgi_open; /* normally defined in runtime_fiber.c; pad test does not link it */
 #include "../src/runtime_pad.c"
```

Environment notes:
- `CC=gcc-14 CXX=g++-14` must be exported; default `cc` is GCC 13.3 (C++23 issues per LINUX_BUILD.md, not retested with 13).
- `BB_LTO=OFF` and `BB_JOBS=2` used (LTO at 2 cores/7 GB is slow and memory-heavy; not attempted).
- `gpu/third_party/fsr-vulkan` shows as modified in `git status`: that is build.sh applying `gpu/patches/fsr-vulkan/*.patch` to the submodule working tree (expected).
- CMake prints several `CMake Warning` lines for CMakeLists.txt:161,284,291,299,307 (EXCLUDE_FROM_ALL/test targets); non-fatal.
- A first `out/gpu-build.log` was overwritten by the no-op rebuild; warning statistics below come from an identical clean rebuild in a scratch dir (same 467 count as the original log).

### 3. Test results (`bash build.sh --test`, exit 0)

| Test binary | Result | Needs GPU? |
|---|---|---|
| pad-test (`tests/test_pad.c`) | PASS (after fix 4; first link failed) | no (SDL only, no window) |
| runtime-test (`tests/test_runtime.c`) | PASS | no (links libbbgpu + Vulkan loader, but no device used) |
| file-mods-test | PASS | no |
| sema-test | PASS | no |
| content-test | PASS | no |
| cpu-patch-test | PASS (50 cases x 400 trials, 800 breakpoints) | no |

No GPU-dependent failures. Lavapipe (mesa-vulkan-drivers 25.2.8, llvmpipe LLVM 20.1.2, reports Vulkan 1.4.318) works: `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./out/bb-probe --vulkan-only` prints `Vulkan: llvmpipe ...; command submission + 4096-byte readback PASS`. `bb-gpu-capabilities` runs and reports that llvmpipe lacks blit support for B10G11R11 (scene scaling unavailable). The extra CMake test targets (`motion-shader-test`, `taa-shader-test`, etc., EXCLUDE_FROM_ALL) are not part of `build.sh --test` and were not built.

### 4. Artifacts

`file out/bb-probe`:
```
out/bb-probe: ELF 64-bit LSB executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=ec4c014e0d1b977a27b4333af1254b6d509805e8, for GNU/Linux 3.2.0, with debug_info, not stripped
```
(non-PIE executable, as intended by `-no-pie`)

`ldd out/bb-probe` (direct deps; the rest are transitive FFmpeg codec libs):
```
libm.so.6  libZydis.so.4.0  libbbgpu.so => /tmp/uncharted_pc/out/gpu/libbbgpu.so
libvulkan.so.1  libSDL3.so.0 => /usr/local/lib/libSDL3.so.0
libavcodec.so.60  libavutil.so.58  libc.so.6
libfmt.so.9  libssl.so.3  libcrypto.so.3  libz.so.1 => /usr/local/lib/libz.so.1 (container's zlib-ng, not the apt 1.3)
libprotobuf.so.32  libxxhash.so.0  libavformat.so.60  libswscale.so.7  libswresample.so.4
libstdc++.so.6  libgcc_s.so.1
+ ~100 transitive (libvpx, libx264 /usr/local/lib/glibc-hwcaps/x86-64-v2, libx265, libaom, librsvg, libgnutls, libzmq, libX11, ...)
```
`ldd out/gpu/libbbgpu.so` (non-libc): libfmt.so.9, libZydis.so.4.0, libssl.so.3, libcrypto.so.3, libz.so.1, libprotobuf.so.32, libvulkan.so.1, libxxhash.so.0, libSDL3.so.0 (/usr/local), libavformat.so.60, libavcodec.so.60, libavutil.so.58, libswscale.so.7, libswresample.so.4, libstdc++.so.6, libm, libgcc_s (+ the same transitive FFmpeg set). Note: libX11 appears only as a transitive dependency in this ldd output although CMake declares `PkgConfig::X11`. Runtime undefined symbols in libbbgpu (`runtime_memory_*`, `runtime_process_*`) resolve against bb-probe (`-Wl,--allow-shlib-undefined`, `-rdynamic` on bb-probe).

bb-probe compile/link line (build.sh, non-Windows path; `$CC` = gcc-14; `runtime` = `src/runtime*.c`):
```
gcc-14 -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -no-pie \
  -I<repo>/third_party/Vulkan-Headers/include <pkg-config --cflags vulkan sdl3 libavcodec libavutil> -I. -Isrc \
  src/probe.c src/cpu_patch.c src/runtime*.c src/vulkan_smoke.c out/libatrac9.a \
  -lm -lZydis \
  -Lout/gpu -lbbgpu -Wl,-rpath,'$ORIGIN/gpu' -Wl,-rpath,<repo>/out/gpu -rdynamic \
  <pkg-config --libs vulkan sdl3 libavcodec libavutil> \
  -o out/bb-probe
# pkg-config here: -I/usr/local/include -I/usr/include/x86_64-linux-gnu -lvulkan -L/usr/local/lib -Wl,-rpath,/usr/local/lib -Wl,--enable-new-dtags -lSDL3 -lavcodec -lavutil
```
libatrac9: `third_party/LibAtrac9/C/src/*.c` compiled with `-std=c99 -O2 -g -w` into `out/libatrac9.a`. `bb-gpu-capabilities`: `gcc-14 -std=c11 -O2 -Wall -Wextra -Werror tools/gpu_capabilities.c <libs> -o out/bb-gpu-capabilities`. The GPU lib: `cmake -S gpu -B out/gpu -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBB_PGO=off -DBB_LTO=OFF -DBB_PGO_DIR=$PWD/pgo -DVulkan_INCLUDE_DIR=<Vulkan-Headers>/include`, then `ninja -C out/gpu bbgpu`.

### 5. gpu-build.log warnings (467 lines containing "warning", 457 `warning:` diagnostics; no errors)

| Category | Count | Where |
|---|---|---|
| `-Wmissing-field-initializers` | 165 | mostly fsr-vulkan (FidelityFX) |
| `-Wstringop-overflow=` | 163 | all from `/usr/include` (glibc/libstdc++ fortify/inline headers; GCC 14 false positives) |
| `-Woverflow` | 47 | fsr-vulkan |
| `-Wunused-function` | 36 | |
| `-Wunknown-pragmas` | 16 | (`#pragma region`-style / MSVC pragmas) |
| `-Wsign-compare` | 15 | |
| `-Wunused-variable` | 9 | |
| `-Wunused-result` | 2 | |

By location: ~290 in `gpu/third_party/fsr-vulkan`, 163 in `/usr/include`, 4 in `gpu/shadps4`. bb-probe itself is built with `-Werror`, so it has none.

### Notes for M1 (macOS)

Observed from the Linux build (inferred items are marked):

1. **x86-64 guest on Apple Silicon.** The loader maps an x86-64 PS4 ELF in-process and jumps into it (`enter_on_stack`/fiber asm, `SYS_arch_prctl(ARCH_SET_GS)` for TLS, `REG_RIP`/`REG_RBP` from `ucontext_t` in the SIGSEGV/SIGUSR2 handlers, `xgetbv`, `fnstcw/ldmxcsr`, Zydis-based BMI1/LZCNT/MOVBE rewriting in `cpu_patch.c`). The whole loader therefore must itself be an x86-64 binary, so on Apple Silicon it has to run under Rosetta 2 (x86_64 slice, `arch -x86_64`), with all dependencies as x86_64 (separate x86_64 Homebrew under `/usr/local`). A native arm64 build would need an x86 emulator/JIT, which this project does not have. Rosetta 2 translates AVX/AVX2 since macOS 15 (shadPS4's macOS build depends on it; floor macOS 15.4), so `cpu_patch` needs no AVX coverage. *(corrected in review)*
2. **Linux-only APIs found in src/** (all need macOS replacements): `memfd_create` + `fallocate(FALLOC_FL_PUNCH_HOLE)` for the guest physical-memory pool (`runtime_memory.c:334,~`), `MAP_FIXED_NOREPLACE` (`cpu_patch.c`, `runtime_memory.c:775,796`; macOS has no such flag, use a hint address without `MAP_FIXED` and check the result), `syscall(SYS_tgkill/getdents64/close_range/arch_prctl)` (`probe.c`, `runtime_thread.c`), `/proc/self/task`, `getrandom`, `ucontext.h` `gregs[REG_*]` (macOS: `uc_mcontext->__ss.__rip` with `_XOPEN_SOURCE`/`_DARWIN_C_SOURCE`), `clock_nanosleep(TIMER_ABSTIME)` (absent on macOS), `_GNU_SOURCE` in ~15 files, `pthread_*` affinity/priority. `ARCH_SET_GS`: macOS has no `arch_prctl`; guest TLS/GS needs another mechanism (needs investigation; `thread_set_fs_base` private syscalls are possible only under Rosetta limits). `gpu/shadps4` also has Linux code: `vk_platform.cpp`, `page_manager.cpp` (`SYS_userfaultfd` write-protect tracking, falls back to mprotect; includes `<linux/userfaultfd.h>`), `p2p_sockets.cpp`/`net_epoll.h` (epoll; macOS needs kqueue), `shim/window.cpp` (X11/Wayland).
3. **Link/compile flags to change:** `-no-pie` (non-PIE exe so main image can sit at 0x400000; ld64 uses `-Wl,-no_pie` and macOS 10.15+ arm64 forbids it, x86_64 still allowed; also the `__PAGEZERO` segment must be shrunk with `-Wl,-pagezero_size,0x1000` so 0x400000 and the low 2 GiB are usable); `-rdynamic` (use `-Wl,-export_dynamic`); `-Wl,-rpath,'$ORIGIN/gpu'` (use `@executable_path/gpu`); `--allow-shlib-undefined` for libbbgpu (Mach-O needs `-undefined dynamic_lookup`/`-flat_namespace` or the runtime symbols moved into a shared lib); `.so` -> `.dylib`; `--enable-new-dtags` from pkg-config; `-pthread`; ELF-only `.globl`/`.text` asm directives in `__asm__` blocks (Mach-O needs `_` symbol prefix and different section directives).
4. **GCC-only / GCC-14-specific:** `build.sh` and the README assume GCC 14 (C++23). On macOS Apple Clang 15+/Homebrew LLVM or `gcc-14` from Homebrew; the code builds with Clang on Windows (MSYS2 CLANG64 path), so Clang should be fine. GCC-only flags: `-fprofile-generate/-fprofile-use/-fprofile-partial-training/-fprofile-correction/-fprofile-update=atomic` (PGO; disable with `BB_PGO=off`), `-Wno-missing-profile`, `-Wno-coverage-mismatch`; `-Werror` on bb-probe turns GCC-14 diagnostics into failures (Clang will add its own; the two fixes above were exactly this). The `pgo/*.gcda` profile is GCC-format only.
5. **Hard-to-get / version-sensitive libraries:** SDL3 is not in apt on 24.04 (built 3.4.18 from source); Homebrew has `sdl3`. Vulkan on macOS has no native loader: need MoltenVK (Vulkan 1.2/1.3 subset via the Vulkan SDK or Homebrew `molten-vk`, `vulkan-loader`), and the renderer uses very new extensions (headers v1.4.344, e.g. `VK_VALVE_shader_mixed_float_dot_product`) plus compute/geometry/tessellation/descriptor-indexing features and 6 GB VRAM expectations that MoltenVK may not provide; this is the biggest M1+ risk and cannot be tested on Linux/lavapipe-class software drivers. Zydis 4.0 (Homebrew has `zydis`), FFmpeg 6.1 (Homebrew has newer 7/8; avcodec/avformat API changes possible), Boost >= 1.83 headers, fmt 9.1 (newer fmt 10/11 may hit consteval changes), tsl-robin-map, xxhash, VMA 3.0.1 (Homebrew `vulkan-memory-allocator`), glslang, spirv-headers, nlohmann_json, protobuf (3.21 worked; Homebrew has 29+ which needs C++17 and `find_package(Protobuf CONFIG)` + abseil), miniupnpc 2.2.6 (Homebrew), OpenSSL 3, zlib. Drop `pkg_check_modules(X11)`/`PkgConfig::X11` on Darwin (CMakeLists.txt:73-75,251) and the X11/Wayland branches in `shim/window.cpp` and `vk_platform.cpp`. Submodule Vulkan-Headers v1.4.344 should still be used (not Homebrew's).
6. **Memory layout:** the guest expects the PS4 range 64 GiB - 1 TiB free (comment in CMakeLists.txt) and low addresses from 0x400000 to 2 GiB; macOS x86_64 under Rosetta has a 47-bit address space but reserves low memory differently; large `MAP_NORESERVE` reservations and `mprotect`-based write tracking are the likeliest M1 runtime blockers (not testable here).
7. `docs/LINUX_BUILD.md` and `shell.nix` are stale relative to CMake (no nlohmann_json/openssl/zlib/protobuf/miniupnpc in shell.nix; Protobuf "must be built from source" was not needed on 24.04). `run.sh` is Linux-only (Portuguese strings); `run.py` is the Windows launcher.
8. Test baseline for M1: the six `build.sh --test` binaries are device-independent and are the right smoke tests for a macOS build; `bb-probe --vulkan-only` is the first GPU check (needs MoltenVK).

### Review (Opus 5.5, 2026-11-01)

Verified against the tree. Additions the later milestones must account for, now written into their specs:

- **Guest TLS (M3, biggest new item).** Linux sets the guest TCB with `arch_prctl(ARCH_SET_GS)` (`runtime_thread.c` `set_gs`). macOS has no userspace GS-base setter. The **Windows path is the template**: `probe.c` `patch_tcb_loads` rewrites every `mov rax, gs:[0]` (`65 48 8b 04 25 00000000`) to a fixed displacement and `set_gs` stores the TCB in that slot. On macOS x86_64 GS already points at the pthread TSD array, so: `pthread_key_create` → displacement `key*8`, `pthread_setspecific(key, tcb)`. Same approach shadPS4 uses on macOS.
- **`MAP_FIXED_NOREPLACE` (M2)** in `runtime_low_map`, `runtime_image_map` and `cpu_patch.c` thunks. Darwin equivalent: `mmap` with the address as a hint (no `MAP_FIXED`) and check the result equals the request, else `munmap` and continue probing.
- **`userfaultfd` (M4)** in `page_manager.cpp`: already falls back to the mprotect/signal path when the syscall fails — on APPLE compile that path only (guard the `<linux/userfaultfd.h>` include).
- **`tgkill` (M3)** in `probe.c` watchdog sampling → `pthread_kill` on the stored `pthread_t`.
- **`--allow-shlib-undefined` (M1)** in `gpu/CMakeLists.txt` → `-undefined dynamic_lookup` on APPLE.
- **The four M0 patches apply to macOS too** (libav link, two `-Werror` fixes, `test_pad.c`). Apply `M0-linux-build-fixes.patch` as the first step of M1.

Patches: [M0-linux-build-fixes.patch](M0-linux-build-fixes.patch).

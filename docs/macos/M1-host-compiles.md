# M1 — Host compiles on macOS x86-64

## Goal
`out/bb-probe` and `out/gpu/libbbgpu.dylib` link on macOS, built for `x86_64`, with the window
backend stubbed so the binary starts and fails with a clear "no cocoa yet" message. Nothing runs yet.

## Preconditions
- macOS 15.4 or newer, Xcode Command Line Tools, Rosetta installed (`softwareupdate --install-rosetta`).
- LunarG Vulkan SDK for macOS installed (https://vulkan.lunarg.com/sdk/home#mac); `source ~/VulkanSDK/<ver>/setup-env.sh`.
  Verify `file $VULKAN_SDK/lib/libvulkan.dylib` lists x86_64.
- Build tools from arm64 Homebrew: `brew install cmake ninja pkg-config nasm`.
- **No x86_64 Homebrew** (unsupported since Homebrew 7.0). See README "Toolchain".

## Tasks
0. `git apply docs/macos/M0-linux-build-fixes.patch` (libav link in build.sh, two -Werror fixes, test_pad.c). Commit as `macos: apply M0 build fixes`.
1. **Dependencies**: write `scripts/macos/build-deps.sh` (idempotent; skip a library whose
   `$BB_DEPS/lib/pkgconfig/*.pc` or CMake config already exists). For each library: pinned release tag,
   download to `$BB_DEPS/src`, configure with `-DCMAKE_OSX_ARCHITECTURES=x86_64
   -DCMAKE_OSX_DEPLOYMENT_TARGET=15.4 -DCMAKE_INSTALL_PREFIX=$BB_DEPS` (CMake projects) or
   `CFLAGS/LDFLAGS="-arch x86_64"`, `--arch=x86_64 --enable-cross-compile` (FFmpeg). Libraries:
   Pin versions to M0's dependency table unless a build fails: SDL3 3.4.18, fmt 9.1, Zydis 4.0 (+ Zycore),
   xxhash, miniupnpc 2.2.6, FFmpeg 6.1, OpenSSL 3.x (`./Configure darwin64-x86_64-cc shared`).
   - zlib: use the macOS SDK's (universal), do not build.
   - protobuf **v21.12** (3.21 line; newer requires abseil). `protoc` must also be built — x86_64 is fine,
     it runs under Rosetta during the build.
   - FFmpeg: only avformat, avcodec, avutil, swscale, swresample; `--disable-programs --disable-doc
     --disable-network --enable-shared`; enable only the decoders the game needs if you can identify them
     (`runtime_ajm.c`, `gpu/shim` video decode), otherwise defaults.
   - Header-only: Boost >= 1.83 (headers only, `Boost::headers` is all that is used), tsl-robin-map, VMA 3.0.1, nlohmann_json
     → installed with their CMake config files into `$BB_DEPS`.
   Vulkan loader/MoltenVK/glslang come from the LunarG SDK, not this script.
   After the script: `lipo -archs` (or `file`) on every `$BB_DEPS/lib/*.dylib` and `*.a` reports x86_64.
   Record tag + configure line per library in Result.
2. **`build.sh`**: add a `Darwin)` case before the generic Linux path, or make the Linux path work on
   Darwin with minimal edits:
   - `CC=clang` (Apple clang); drop `-no-pie` → `-Wl,-no_pie -Wl,-pagezero_size,0x1000` (see M2 for why).
   - `-rdynamic` → `-Wl,-export_dynamic`.
   - `-Wl,-rpath,'$ORIGIN/gpu'` → `-Wl,-rpath,@loader_path/gpu`.
   - Library is `out/gpu/libbbgpu.dylib`, not `.so`.
   - PGO/LTO: start with `BB_LTO=OFF`, `BB_PGO=off`. GCC-only flags (`-fprofile-partial-training`,
     `-fprofile-correction`) must not reach clang.
   - Add `-arch x86_64` to every compile and link line; set `CMAKE_OSX_ARCHITECTURES=x86_64`.
3. **`gpu/CMakeLists.txt`**:
   - `if (NOT WIN32)` X11 requirement → `if (NOT WIN32 AND NOT APPLE)`. Same for any X11/Wayland link.
   - The clang-21 `-include` workaround at the top is `if(WIN32)`; Apple clang may need the same list.
     Enable it `if(WIN32 OR APPLE)` only if the build fails on missing `<cstdlib>`-style includes.
   - DLSS/XeSS/FSR4 sources and `ffx-vulkan::fsr4-v07-vulkan`: exclude on APPLE (grep `dlss`, `xess`,
     `fsr4`, `DXIL` in `gpu/`). Keep FSR 3.1 (`ffx-vulkan::portable`) compiling.
   - `bbgpu` as `SHARED` on APPLE (dylib), matching the Linux shape.
   - `-Wl,--allow-shlib-undefined` (line ~235) → `-undefined dynamic_lookup` on APPLE.
   - `page_manager.cpp` includes `<linux/userfaultfd.h>`: guard with `#ifdef __linux__` and compile only the mprotect/signal fallback on APPLE (full verification in M4).
4. **`gpu/shim/window.cpp`**: add a `cocoa` branch that sets `window_info.type = WindowSystemType::Metal`
   and `render_surface` from `SDL_PROP_WINDOW_COCOA_METAL_VIEW_TAG` / `SDL_Metal_CreateView` +
   `SDL_Metal_GetLayer`. For M1 it is acceptable to just `LOG_ERROR` and abort there — the point is that
   the `UNREACHABLE_MSG` no longer fires on "cocoa". Full wiring is M4.
5. **`src/`**: get `probe.c` and `runtime_*.c` past the compiler. Expected breakages:
   - `<sys/mman.h>`/`<ucontext.h>`: `REG_RIP`/`gregs` don't exist on Darwin — temporarily `#ifdef __APPLE__`
     stub the body of `fault()` so it compiles (real fix in M3).
   - `memfd_create`, `fallocate`, `MFD_CLOEXEC` don't exist — temporarily stub the Darwin `pool()`/
     `host_discard()` (real fix in M2).
   - `pthread_setaffinity_np`, `pthread_setname_np` signature, `clock_gettime` flags, `sched_*`,
     `O_DIRECT`, `posix_fadvise`, `sysinfo`: replace with Darwin equivalents or no-ops; list each in Result.
   - `arch_prctl(ARCH_SET_GS)` in `runtime_thread.c` `set_gs` and `SYS_tgkill` in `probe.c`: stub with `MACOS-STUB(M3)`.
   - `MAP_FIXED_NOREPLACE` in `runtime_memory.c` and `cpu_patch.c`: stub/define with `MACOS-STUB(M2)`.
   - `-Werror` will bite on unused-variable from the stubs; keep `-Werror`, fix properly.
6. Run `out/bb-probe` with no args: must print its usage/error and exit, not crash in the dynamic linker.
   `otool -L out/bb-probe` and `file out/bb-probe` show `x86_64` and `@loader_path/gpu/libbbgpu.dylib`.

## Acceptance
- `arch -x86_64 bash build.sh` completes from a clean `out/`.
- `file out/bb-probe out/gpu/libbbgpu.dylib` → both `Mach-O 64-bit x86_64`.
- `out/bb-probe` starts under Rosetta and exits cleanly with an error message.
- Result lists every stub introduced (file:function) so M2/M3 know what to replace.

## Hazards
- Mixing arm64 brew libs (pkg-config falling back to `/opt/homebrew/lib/pkgconfig` when `PKG_CONFIG_LIBDIR` is unset): link errors like "building for macOS-arm64 but attempting to link with
  file built for macOS-x86_64". Check `file $BB_DEPS/lib/libSDL3.dylib`.
- `fmt` / `boost` versions: start with what Ubuntu 24.04 had in M0 (dependency table) to avoid API drift.
- shadPS4's own tree has a working APPLE CMake path — copy decisions from there rather than inventing.

## Result

**M1: host compiles on macOS x86-64** — date 2026-11-01, branch `macos`, final source commit `3c0746f` (the Result commit follows it).
Machine: MacBook Pro M4 Max, macOS 26.6.2 (Darwin 25.6), Apple clang 21.0.0, macOS SDK 27.0, Vulkan SDK LunarG **1.4.363.0** (`$HOME/VulkanSDK/1.4.363.0/macOS`, universal; x86_64 slice verified for `libvulkan.dylib` and `libMoltenVK.dylib`). Build tools (cmake 4.4.5, ninja 1.13.2, pkg-config, nasm) are arm64 Homebrew; no Homebrew library is linked.

Commits (after `7f51f12`): `4aab8e1` apply M0 build fixes; `7464a95`, `33df906` build-deps.sh; `eda79c6` build.sh + gpu/CMakeLists.txt; `3c0746f` sources.

### Acceptance (all pass)
- Clean build from empty `out/`: `bash build.sh` (plain, arm64 shell; the Darwin path cross-compiles with `-arch x86_64`, `arch -x86_64` is **not** required). 50 s wall, exit 0, no GPU-library errors (260 `warning:` lines in `out/gpu-build.log`, third-party/FSR as on Linux; bb-probe itself builds with `-Werror`).
- `file out/bb-probe out/gpu/libbbgpu.dylib` → `Mach-O 64-bit executable x86_64` / `Mach-O 64-bit dynamically linked shared library x86_64`.
- `otool -L out/bb-probe` has `@rpath/libbbgpu.dylib`. `LC_RPATH`: `@loader_path/gpu`, `<repo>/out/gpu`, `$VULKAN_SDK/lib` (needed for `@rpath/libvulkan.1.dylib`), then `$BB_DEPS/lib` (added by the linker from `-L`).
- `__PAGEZERO` vmsize `0x1000`.
- `otool -L` on both binaries: 0 hits for `/opt/homebrew`. Everything is `/usr/lib/*`, `$BB_DEPS/lib/*` (absolute install names) or `@rpath/libvulkan.1.dylib` (SDK).
- `out/bb-probe` with no args under Rosetta: exits 24 with `STOP: unsupported CPU VirtualApple @ 2.50GHz: missing AVX ...` (clean, no dyld error). **Important finding:** Rosetta does not advertise AVX to the process unless `ROSETTA_ADVERTISE_AVX=1` is set in the environment. With it (or `BB_SKIP_CPU_CHECK=1`) the usage line prints, exit 1. `run.sh` for macOS (M6) and all later test runs must export `ROSETTA_ADVERTISE_AVX=1`; M2 should confirm AVX actually executes.
- `ROSETTA_ADVERTISE_AVX=1 out/bb-probe --vulkan-only` → `vkCreateInstance returned -9` (VK_ERROR_INCOMPATIBLE_DRIVER: loader does not find MoltenVK / portability enumeration is not enabled). Out of M1 scope; M4.
- Not run: `bash build.sh --test` (the build.sh test lines got the same arch/include changes but were not executed).

`grep -rn MACOS-STUB src gpu/shim gpu/shadps4`:
```
src/compat_darwin.h:25:/* MACOS-STUB(M2): macOS has no MAP_FIXED_NOREPLACE. ...
src/runtime_thread.c:91:    /* MACOS-STUB(M3): macOS has no arch_prctl(ARCH_SET_GS). ...
src/runtime_memory.c:317:    /* MACOS-STUB(M2): macOS has no memfd_create; ...
src/runtime_memory.c:342:    /* MACOS-STUB(M2): no fallocate(PUNCH_HOLE); ...
src/probe.c:419:        /* MACOS-STUB(M3): on Darwin RIP comes from the __ss thread state; ...
src/probe.c:505:    /* MACOS-STUB(M3): no /proc/self/task and no tgkill: ...
gpu/shim/window.cpp:122:        // MACOS-STUB(M4): the Metal surface is wired in M4. ...
```

### Stubs (7)
| Where | Tag | What it does now | Replacement |
|---|---|---|---|
| `src/compat_darwin.h` (`MAP_FIXED_NOREPLACE` = 0) + `bb_darwin_hint_missed` at `runtime_memory.c` `runtime_low_map`, `runtime_image_map` (`cpu_patch.c` `near_image` already checked the result) | M2 | address is only a hint; if the kernel returns another address it is unmapped and treated as taken | M2: verify under Rosetta that 0x400000.. and the low 2 GiB are honoured |
| `runtime_memory.c` `pool()` | M2 | prints a message, returns -1 (no memfd) | shared backing for the direct-memory pool |
| `runtime_memory.c` `host_discard()` | M2 | no-op | decommit / re-zero of released pages |
| `runtime_thread.c` `set_gs()` | M3 | prints `STOP: guest TLS ... not implemented on macOS`, `exit(21)` | pthread-TSD key + `patch_tcb_loads` (Windows-path template, see M0 review) |
| `probe.c` `fault()` | M3 | real code, not a stub: reads/writes RIP via `uc_mcontext->__ss.__rip` (macro `UC_RIP`); tagged because it is **untested** under Rosetta | verify in M3 |
| `probe.c` `watchdog()` | M3 | dumps only the thread that got SIGALRM, then `_exit`; no `/proc/self/task`/`tgkill` | thread table + `pthread_kill(SIGUSR2)` |
| `gpu/shim/window.cpp` cocoa branch | **M4** (tag outside the M2/M3 set; spec task 4) | `LOG_ERROR` + stderr message + `abort()` | `SDL_Metal_CreateView` / `SDL_Metal_GetLayer`, `WindowSystemType::Metal` |

### Darwin substitutions (non-stub)
- Asm symbol names (**needs a decision/review, see below**): the unmodified inline asm defines bare `enter_on_stack`, `fiber_swap`, `fiber_start` and calls bare `fiber_main`, but Mach-O C symbols carry a `_` prefix. The asm is untouched; the C declarations got `__asm__("name")` labels under `#ifdef __APPLE__` (`probe.c`, `runtime_fiber.c`).
- `src/compat_darwin.h` (included from `runtime.h` and `cpu_patch.c`): `st_atim/st_mtim/st_ctim` → `st_*timespec` (runtime_file.c, runtime_pad.c); `CLOCK_REALTIME_COARSE` → `CLOCK_REALTIME`; `pthread_mutex_timedlock`, `pthread_rwlock_timedrdlock/wrlock` → 50 µs try-lock polling until a CLOCK_REALTIME deadline; `pthread_condattr_setclock` → no-op macro (cond waits are CLOCK_REALTIME on macOS; `runtime_sema.c` `WAIT_CLOCK` is `CLOCK_REALTIME` on Apple); `PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP` → `PTHREAD_RECURSIVE_MUTEX_INITIALIZER`.
- `runtime_audio.c` `sleep_until`: `clock_nanosleep(TIMER_ABSTIME)` → `nanosleep` loop.
- `runtime_kernel.c`: `getrandom` → `arc4random_buf`; `RUSAGE_THREAD` → `CLOCK_THREAD_CPUTIME_ID` as user time.
- `runtime_thread.c`: `pthread_setname_np(name)` (one-arg form); `<asm/prctl.h>` skipped. Affinity/priority were already no-ops off Windows.
- `probe.c`: `_XOPEN_SOURCE 600` + `_DARWIN_C_SOURCE` for `<ucontext.h>`; `<malloc.h>` skipped; `gettid` → `pthread_threadid_np`; `process_vm_readv` → `mach_vm_read_overwrite`; `SYS_close_range` → close loop to `sysconf(_SC_OPEN_MAX)`; `mallopt` → none (`MACOS-NOTE(M2)`: check host heap objects stay below 1 TiB without the non-PIE brk heap); `#undef round_page/trunc_page` (Mach macros); one `%lx` cast.
- `gpu/CMakeLists.txt`: libc++ force-include list for `WIN32 OR APPLE` (+`exception`, needed by boost::asio); X11 only `NOT WIN32 AND NOT APPLE`; `-undefined dynamic_lookup` instead of `--allow-shlib-undefined`; force-include `shim/bb_mac_compat.h` (`gettid`) for bbgpu and bbnet; `epoll-shim::epoll-shim` for bbnet (kqueue `<sys/epoll.h>`, as shadPS4 does: `find_package(epoll-shim CONFIG)`); `target_link_directories` for Zycore (Zydis's exported target links the bare name `Zycore`). `bbgpu` stays SHARED.
- GPU sources: `bbport_threads.h` (Apple: QoS classes instead of SCHED_IDLE/nice, `hardware_concurrency`), `bbport_write_log.cpp` (`__ss` registers), `bbport_overlay.cpp` (Mach-O `.incbin` asm for the font, `__DATA,__const`, `_` symbols), `liverpool.h/.cpp` (`gpu_tid` on Apple; `pthread_getcpuclockid` and `RUSAGE_THREAD` statistics skipped), `pm4_cmds.h` (`Address<T>` C-style cast: `reinterpret_cast<unsigned long>(u64)` is ill-formed on macOS where `u64` is `unsigned long long`), `buffer_cache.cpp` (`std::min/max<VAddr>` for the same ull/ul mismatch).
- `page_manager.cpp`: **no change needed**: the userfaultfd code is already under `#ifdef __linux__` and the mprotect/signal path compiles on Apple. Upstream already has APPLE branches in `signal_context.cpp` (Darwin ucontext), `vk_platform.cpp` (Metal surface, `VK_USE_PLATFORM_METAL_EXT`), `thread.cpp`, `rdtsc.cpp`, `path_util.cpp`; used as is.
- DLSS/XeSS: nothing to exclude, they are `_WIN32`-only (no-ops elsewhere). **FSR4 was not excluded** (see decisions).
- `build.sh` Darwin path (same file, no fork): sets `VULKAN_SDK`, `BB_DEPS`, `PKG_CONFIG_LIBDIR=$BB_DEPS/lib/pkgconfig:$VULKAN_SDK/lib/pkgconfig` (the SDK dir is added for `vulkan.pc`), `CMAKE_PREFIX_PATH`, `MACOSX_DEPLOYMENT_TARGET=15.4`, `PATH+=$VULKAN_SDK/bin` (glslang); `CC=clang`; `cc=(clang -arch x86_64 -mmacosx-version-min=15.4)` on every compile/link line (incl. libatrac9 and tests); `BB_PGO=off`, `BB_LTO=OFF` defaults; cmake gets `CMAKE_OSX_ARCHITECTURES=x86_64`, deployment target, clang compilers, `CMAKE_PREFIX_PATH`, `CMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local`, `CMAKE_POLICY_VERSION_MINIMUM=3.5`. Link: `-Wl,-no_pie -Wl,-pagezero_size,0x1000 -Wl,-export_dynamic -Wl,-rpath,@loader_path/gpu ...`. Test lines also got `${includes[@]}`.

### bb-probe link line (Darwin, from build.sh)
```
clang -arch x86_64 -mmacosx-version-min=15.4 -std=c11 -O2 -g -Wall -Wextra -Werror -pthread \
  -Wl,-no_pie -Wl,-pagezero_size,0x1000 -I$BB_DEPS/include -Ithird_party/Vulkan-Headers/include \
  <pkg-config --cflags vulkan sdl3 libavcodec libavutil> -I. -Isrc \
  src/probe.c src/cpu_patch.c src/runtime*.c src/vulkan_smoke.c out/libatrac9.a -lm -L$BB_DEPS/lib -lZydis \
  -Lout/gpu -lbbgpu -Wl,-rpath,@loader_path/gpu -Wl,-rpath,<repo>/out/gpu -Wl,-rpath,$VULKAN_SDK/lib -Wl,-export_dynamic \
  <pkg-config --libs vulkan sdl3 libavcodec libavutil> -o out/bb-probe
```
Linker prints `ld: warning: -no_pie is deprecated when targeting new OS versions` (works; x86_64 only). `libbbgpu.dylib` links with `-undefined dynamic_lookup` and resolves `runtime_*` against bb-probe.

### Dependencies (`scripts/macos/build-deps.sh`, idempotent, markers per library)
Common cmake flags: `-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.4 -DCMAKE_INSTALL_PREFIX=$BB_DEPS -DCMAKE_INSTALL_NAME_DIR=$BB_DEPS/lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_PREFIX_PATH=$BB_DEPS -DCMAKE_IGNORE_PREFIX_PATH='/opt/homebrew;/usr/local'`. Absolute install names mean no rpath is needed for these dylibs.
| Library | Tag / source | Notes (extra configure) |
|---|---|---|
| SDL3 | 3.4.18 (`release-3.4.18` tarball) | shared; `-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF` |
| fmt | 9.1.0 | static; `-DBUILD_SHARED_LIBS=OFF -DFMT_TEST=OFF -DFMT_DOC=OFF` |
| Zycore | v1.4 (Zydis 4.0.0 submodule, `git clone --recurse-submodules`) | shared; `-DZYCORE_BUILD_SHARED_LIB=ON`; the Zydis release tarball has no submodules, and Zydis does not install Zycore, so it is built/installed separately |
| Zydis | v4.0.0 | shared; `-DZYAN_SYSTEM_ZYCORE=ON -DZYDIS_BUILD_SHARED_LIB=ON -DCMAKE_C_FLAGS=-I$BB_DEPS/include -DCMAKE_SHARED_LINKER_FLAGS=-L$BB_DEPS/lib` |
| xxHash | v0.8.2 | static only: `make CC=clang CFLAGS="-O3 -fPIC -arch x86_64 -mmacosx-version-min=15.4" libxxhash.a libxxhash.pc`, files copied by hand (its dylib rule rejects `-arch`) |
| miniupnpc | miniupnpc_2_2_6 | static; `-DUPNPC_BUILD_SHARED=OFF -DUPNPC_BUILD_STATIC=ON -DUPNPC_BUILD_TESTS=OFF -DUPNPC_BUILD_SAMPLE=OFF` |
| OpenSSL | 3.0.15 | `./Configure darwin64-x86_64-cc shared no-tests --prefix=$BB_DEPS --openssldir=$BB_DEPS/ssl --libdir=lib -mmacosx-version-min=15.4`; `make build_sw install_sw` (3.0 rejects `no-docs`) |
| protobuf | v21.12 (3.21.12) | static; `-Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_WITH_ZLIB=ON`; x86_64 `protoc` runs under Rosetta |
| FFmpeg | 6.1.1 | `./configure --prefix=$BB_DEPS --arch=x86_64 --target-os=darwin --enable-cross-compile --cc="clang -arch x86_64" --cxx="clang++ -arch x86_64" --x86asmexe=<nasm> --extra-cflags/ldflags="-arch x86_64 -mmacosx-version-min=15.4" --install-name-dir=$BB_DEPS/lib --enable-shared --disable-static --disable-programs --disable-doc --disable-network --disable-autodetect --enable-zlib --disable-iconv --disable-avdevice --disable-avfilter --enable-pic`. Decoders left at defaults (avplayer picks the codec from the stream). `--disable-iconv`: `--enable-iconv` failed to link (`_iconv` needs `-liconv`); only subtitle charset conversion is lost. nasm objects produce `ld: no platform load command` warnings (harmless). |
| epoll-shim | v0.0.20240608 | static; not in the M1 spec list; required by `net_epoll.h` (`<sys/epoll.h>`) |
| Boost | 1.83.0 | headers copied to `$BB_DEPS/include/boost` (found via CMake's FindBoost module, CMP0167 warning only) |
| robin-map | v1.2.1 | cmake install |
| VMA | v3.0.1 | header copied to `$BB_DEPS/include/vma/` (picked up by the existing `find_path` fallback) |
| nlohmann_json | v3.11.3 | cmake install, `-DJSON_BuildTests=OFF` |
zlib: macOS SDK's (`/usr/lib/libz.1.dylib`). glslang, Vulkan loader, MoltenVK: LunarG SDK. magic_enum, miniz, xbyak, sirit (+spirv-headers), imgui, fsr-vulkan: submodules, as on Linux.
Verification at the end of the script: every `*.dylib`, `*.a` and executable in `$BB_DEPS/lib|bin` is `lipo -archs` = `x86_64` (all pass).
Time: the successful script run was about 2.5 minutes of wall time for the build steps on this machine (16 cores), about 5-6 minutes summed over the attempts, including four failed ones (xxhash dylib rule, OpenSSL `no-docs`, FFmpeg iconv, Zycore headers) and the slow Boost download (137 MB). A fresh run from nothing should take about 4-5 minutes plus downloads.

### Open issues / for the M2 reviewer
1. Rosetta needs `ROSETTA_ADVERTISE_AVX=1` (see above). Not yet confirmed that AVX instructions run, only that cpuid reports them.
2. `-Wl,-no_pie` deprecation warning from ld64; keep an eye on it on newer SDKs.
3. Real symbol lookup of `runtime_*` from libbbgpu through `dynamic_lookup` + `-export_dynamic` links, but was never executed (nothing past argument parsing runs). First real test is M2/M3.
4. `check_cpu()` and everything after it needs `ROSETTA_ADVERTISE_AVX=1`; `--vulkan-only` fails at `vkCreateInstance` (-9): needs `VK_KHR_portability_enumeration` / `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` or an ICD path (`VK_ICD_FILENAMES=$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json`). Not investigated.
5. `bbport_threads.h` QoS replacements and the `liverpool` stats skips are behaviour changes only on macOS.
6. `submodule gpu/third_party/fsr-vulkan` shows as modified (build.sh applies `gpu/patches/fsr-vulkan`), as on Linux.
7. `shell.nix`, `docs/LINUX_BUILD.md` untouched.
8. `build.sh --test` has not been run on macOS.

### Decisions needed
- FSR 4 (`ffx-vulkan::fsr4-v07-vulkan`) is **still compiled and linked** on Apple. The spec says to exclude it, but it is portable Vulkan code (no DLL/DXIL) used from `vk_fsr4.cpp`, `vk_temporal_upscaler.cpp`, `bbport_settings.cpp`, `bbport_overlay.cpp`, `vk_instance.h`; excluding it would need non-additive edits in about 10 files. Proposal: leave it compiled and disable it at run time (settings / capability check) in M4/M5.
- Asm-label approach for the three asm symbols (above) versus editing the asm to add `_`: the first keeps the "do not touch the asm" rule; please confirm.

### Review (Opus 5.5, 2026-11-01)

Accepted. Decisions on the two open questions:
1. **FSR 4**: keep it compiled; disable it at run time on Apple (M4 task). Agreed.
2. **Asm labels** (`__asm__("name")` on the C declarations for `enter_on_stack`, `fiber_swap`,
   `fiber_start`): agreed — additive and leaves the asm byte-identical to upstream.

Notes for later milestones (written into their specs):
- `ROSETTA_ADVERTISE_AVX=1` is read by Rosetta at process launch, so it cannot be set from inside
  `bb-probe`. Dev runs export it; `run.sh` (M6) exports it; the `.app` sets it via `LSEnvironment` in
  Info.plist or a launcher that re-execs. M2 must prove AVX instructions *execute*, not just that
  cpuid reports them.
- `bash build.sh --test` was not run in M1 — first task of M2.
- Non-additive edits that also change Linux/Windows behaviour, to watch on upstream rebases:
  `pm4_cmds.h` `Address()` (`reinterpret_cast` → C-style cast; equivalent for pointer `T`, fixes
  `u64`≠`uintptr_t` on Darwin) and `buffer_cache.cpp` explicit `std::min/max<VAddr>` (same reason).
  Both are harmless on other platforms; consider upstreaming them.
- `runtime_sema.c`: Apple uses `CLOCK_REALTIME` for condvar deadlines (as Windows). A wall-clock jump
  can stretch/shorten guest waits. Acceptable for now; if waits misbehave in M5, switch Darwin to
  `pthread_cond_timedwait_relative_np`.
- Per-thread CPU stats (`pthread_getcpuclockid`, `RUSAGE_THREAD`) are compiled out on Apple — the
  GPU-thread share in M5's perf table needs `thread_info(THREAD_BASIC_INFO)` instead, or is omitted.

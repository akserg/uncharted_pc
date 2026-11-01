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
_(fill in: date, commit, Vulkan SDK version, per-library tag + configure line from build-deps.sh, formulas built from source, every stub
introduced, every Darwin substitution made, link line of bb-probe, open issues)_

# macOS (Apple Silicon) port — plan

Branch: `macos` on `akserg/uncharted_pc` (fork of `Supermedo/uncharted_pc`, upstream remote `upstream`).
Base commit: `bbd1b3a` (0.3.11).

## Strategy

The guest (PS4 ELF, x86-64 with AVX) stays x86-64. The host (`bb-probe` + `libbbgpu`) is built as an
**x86-64 macOS binary** and runs under **Rosetta 2 on macOS 15.4+**, which translates AVX/AVX2 since
Sequoia. This is exactly how shadPS4 ships its macOS build. No arm64 translator, no FEX.

Consequences:
- All x86 asm in `src/` (`enter_on_stack`, `fiber_swap`, `ldmxcsr`/`fldcw`, `ms_abi` hooks) and the
  Xbyak JIT in `flatten_extended_userdata_pass.cpp` stay as-is.
- Work is confined to: build system, host memory backend, signal/ucontext layout, window/surface,
  MoltenVK portability, upscaler exclusions, packaging.
- Shelf life: Rosetta is fully supported through macOS 27, then limited to "older unmaintained games".
  A native arm64 port (in-process x86-64 translator) is a separate, later project.

Hardware target: M-series Mac, **24 GB unified memory or more** (6 GB "VRAM" + ~8 GB game + macOS).
16 GB is expected to swap.

## Milestones

| # | Spec | Goal | Done when |
|---|---|---|---|
| — | [GAME_FILES.md](GAME_FILES.md) | Game folder layout the loader expects | reference |
| M0 | [M0-baseline.md](M0-baseline.md) | Reference numbers from the Linux x86-64 port | FPS/scene table recorded |
| M1 | [M1-host-compiles.md](M1-host-compiles.md) | `bb-probe` + `libbbgpu.dylib` link on macOS x86-64 | `ls out/bb-probe` succeeds |
| M2 | [M2-memory.md](M2-memory.md) | Guest address space reservable under Rosetta | `tests/test_runtime` + new alias test pass |
| M3 | [M3-guest-boots.md](M3-guest-boots.md) | ELF runs until first GNM/VideoOut call | log shows guest `main` reached + first `sceGnm*` |
| M4 | [M4-window-moltenvk.md](M4-window-moltenvk.md) | Cocoa window + MoltenVK device + first frame | U1 main menu renders |
| M5 | [M5-playable.md](M5-playable.md) | One game playable at 1080p60, no upscaler | Chapter 1 of U1 completed; perf table vs M0 |
| M6 | [M6-ship.md](M6-ship.md) | `run.sh` + signed `.app` + docs | Clean machine (macOS 15.4+) runs from the zip |

Milestones are sequential. Do not start M(n+1) until M(n)'s **Result** section is filled in.

## Conventions

- Every spec has a `## Result` section. The agent/person who completes the milestone fills it in:
  date, commit hash, what was done, measurements, deviations from the spec, open issues.
  This is the handoff between sessions and models — keep it factual and complete.
- Platform conditionals: `#ifdef __APPLE__` in C, `if (APPLE)` in CMake. Prefer small additive branches
  next to the existing `_WIN32` / Linux ones; do not restructure upstream code (rebase cost).
- Commit per milestone (or per logical step inside one). Commit messages start with `macos:`.
- Keep `main` identical to `upstream/main`; rebase `macos` onto it periodically.
- Toolchain: Apple clang via Xcode CLT targeting `-arch x86_64`. **No x86_64 Homebrew**: Homebrew 7.0
  (2026) dropped x86_64 installs on Apple Silicon. Libraries come from:
  - LunarG Vulkan SDK for macOS (universal binaries): Vulkan loader, MoltenVK, glslang, spirv-cross.
  - `scripts/macos/build-deps.sh`: builds SDL3, fmt, xxhash, Zydis, FFmpeg, protobuf 21.x, miniupnpc
    as x86_64 into `$BB_DEPS` (default `$HOME/.local/uncharted-x86_64`); header-only deps (Boost,
    robin-map, VMA) are copied there too.
  - Build *tools* (cmake, ninja, pkg-config, nasm, git) may come from arm64 Homebrew — they run on
    the host and are never linked. Libraries from `/opt/homebrew` must never be linked: set
    `PKG_CONFIG_LIBDIR=$BB_DEPS/lib/pkgconfig` (LIBDIR, not PATH) and `CMAKE_PREFIX_PATH=$BB_DEPS`.
- Build output stays in `out/` like upstream. Scripts: `build.sh` gets a `Darwin)` case; do not fork it.

## Reference material

- Upstream Linux build: `../LINUX_BUILD.md`, `../../shell.nix`, `../../build.sh` (non-Windows path).
- shadPS4 macOS build (how they solved the same MoltenVK/subgroup/memory questions):
  https://github.com/shadps4-emu/shadPS4 — look at `CMakeLists.txt` (APPLE branches),
  `src/core/address_space.cpp` (Darwin memory backend), `src/video_core/renderer_vulkan/vk_instance.cpp`
  (portability subset, wave32 handling), `src/common/signal_context.cpp` (Darwin ucontext).
- Apple: Rosetta end-of-life schedule https://developer.apple.com/news/?id=w5ngl9k2

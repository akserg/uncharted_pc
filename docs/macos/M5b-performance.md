# M5b — Performance

## Goal
U1 gameplay at a steady 60 fps at 1920×1080, upscalers off, on the M4 Max under Rosetta — or, if that is not
reachable, a measured explanation of the limit and the best achievable number.

## Baseline (M5 session 2, `user/m5-session-2.log`)
138 five-second windows: 28 ≥ 58 fps, 66 at 45–58, 44 below 45; mean 48.3 fps; p99 frame time 33.3 ms in gameplay.
GPU-command-thread CPU 75 % on average in windows below 45 fps (54 % otherwise), peak 81 %. Footprint 12.9 GB.
The likely limit is the GPU command thread (shadPS4 Liverpool command processing, translated by Rosetta).

## Preconditions
- M5 playable (see M5 status). Owner's `user/` profile and save data in place.
- **Work in a separate git worktree** (`../uncharted_pc-perf`, branch `macos-perf` from `macos`) so builds and
  save data stay apart. The worktree does NOT make concurrent play possible: **benchmarks need an idle machine** —
  no second bb-probe (the owner's game shares GPU/CPU, and with background controller events the owner's pad also
  drives the replay copy) and no Unity compiles. The owner doesn't play while benchmarks run. Share `$BB_DEPS` and the Vulkan SDK; the worktree gets its own `out/`
  and its own `user/` copy (copy the owner's `user/` once, never write back).

## Tasks
1. **Repeatable benchmark.** The owner records one pad session with `BB_PAD_RECORD` at a fixed gameplay spot
   (save loaded, ~60 s of movement + camera + combat). The agent replays it with `BB_PAD_REPLAY` and
   `BB_PERF_LOG=1`; one benchmark = mean fps, p50/p99 frame time, GPU-thread CPU, footprint over the replay.
   Run 3× per variant; report mean and spread. Script: `tools/macos/bench.sh <label>` writing `out/bench/<label>.txt`.
2. **Profile.** `sample <pid> 10` and/or `xctrace record --template 'Time Profiler'` during the replay; attribute
   the GPU command thread's time to functions (host x86_64 code translated by Rosetta; symbols from the
   RelWithDebInfo build). List the top 20 functions with % of that thread.
3. **Free wins first, each measured on its own:**
   a. LTO for libbbgpu and bb-probe on macOS (upstream defaults to LTO ON elsewhere; macOS builds with `BB_LTO=OFF`).
      Clang ThinLTO.
   b. `-O3` vs the current `RelWithDebInfo` (-O2) for libbbgpu.
   c. Target flags for the host code: `-mavx2 -mfma -mbmi -mbmi2` (Rosetta translates AVX2; measure whether it
      helps or hurts translated code).
   d. Clang PGO (`-fprofile-instr-generate` → replay → `llvm-profdata merge` → `-fprofile-instr-use`). Upstream's
      `pgo/*.gcda` is GCC-only; the clang profile goes to `pgo/macos/` and is used only on Apple.
4. **Targeted fixes** from the profile (only after 3): e.g. hot locks, per-draw allocations, redundant
   descriptor/pipeline lookups, logging in hot paths. Each change measured; no change without a benchmark gain.
5. **Settings sweep**: `BB_ARENA_PAGE_BITS` (1 GiB vs 256 MiB), draw-preparation worker count, MoltenVK config
   (`MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS`, `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0`, etc.).

## Acceptance
- Benchmark table: baseline vs every variant (3 runs each), with spread.
- Profile top-20 of the GPU command thread, before and after.
- The chosen build configuration is the default on macOS (`build.sh` Darwin path), all tests pass, no visual
  regression (frame dump at the benchmark spot compared before/after).
- Either steady 60 fps in the benchmark (≥ 58 mean, p99 ≤ 20 ms), or a written limit analysis.

## Hazards
- Rosetta: translation cost is fixed per code page; code-size growth from `-O3`/LTO/inlining can hurt. Measure.
- PGO profile must come from the macOS build (clang), never reuse GCC data.
- Don't change game-visible behaviour (frame pacing, vblank) to win numbers.

## Result
_(fill in: date, commits, benchmark table, profiles, chosen configuration, open issues)_

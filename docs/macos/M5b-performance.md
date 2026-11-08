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

**Date:** 2026-11-08. **Branch:** `macos-perf` (worktree `../uncharted_pc-perf`, from `macos` `3434d25`); commits `macos: M5b benchmark harness ...` and `macos: M5b result` (build.sh opt-in knobs). Nothing pushed or merged. **Status: partial.** Steps 1-4 a-c done; 4d (PGO) and steps 5 (targeted fixes, settings sweep) **not done** (reasons below). 60 fps mean is reached; the p99 <= 20 ms criterion is not.

### Benchmark
`tools/macos/bench.sh <label> [runs]`: starts U1, drives the menus (title -> Continue -> "Sanctuary?" -> Friend Stats OK, ~125 s after launch), then plays `user/bench-route.pad` (209.9 s, `BB_PAD_REPLAY` + the token `replay` in `BB_PAD_FILE`), `BB_PERF_LOG=1`, `BB_GPU_ID=0`, save data restored from a snapshot before every run, game quit afterwards. Statistics cover the replay window only (new `BB_PERF_FRAMES` per-flip log; GPU-thread CPU and footprint from the PERF windows inside the replay). While `BB_PAD_FILE` is set the game ignores the host controller. Every run records Unity and other-`bb-probe` state (`.mon` file, `max_unity_cpu_pct` in the result); a run with Unity > 100 % CPU or a second `bb-probe` is marked `*.invalid.txt` and redone. The script also refuses to start while another `bb-probe` runs or Unity is busy (re-check every 60 s). Per-run files: `out/bench/<label>-<n>.txt` (+ `-start.png` start frame, `.png` frame at replay +60 s).

**Discarded:** all runs and profiles taken while the owner's second game copy and a Unity editor were running (15:35-15:45, plus a run that never left the title screen) are in `out/bench/invalid*` and are not used. Runs baseline 3-5 (Unity 257-491 % CPU) were rejected automatically.

| variant (3 valid runs, each) | mean fps (min-max) | p99 ft | frames > 20 ms | GPU-thread CPU | peak footprint |
|---|---|---|---|---|---|
| baseline, first series (cold pipeline cache; runs 1,2,6) | 57.7 (55.65-58.92) | 33.3 ms | 1.6-7.6 % | 74-77 % | 11.6-11.7 GB |
| baseline B (control, after the sweep, warm cache) | 59.33 (59.22-59.45) | 16.7 / 33.3 / 33.3 | 0.9-1.3 % | 72-79 % | 11.7 GB |
| ThinLTO (libbbgpu) | 59.23 (59.16-59.28) | 33.3 | 1.1-1.4 % | 73-80 % | 11.6-11.7 GB |
| -O3 (libbbgpu, LTO off) | 59.25 (59.06-59.42) | 16.7 / 33.3 / 33.3 | 1.0-1.5 % | 73-78 % | 11.7 GB |
| -mavx2 -mfma -mbmi -mbmi2 (libbbgpu, LTO off) | 59.22 (59.01-59.35) | 33.3 | 1.1-1.6 % | 76-81 % | 11.7-11.9 GB |
| profiling run (sample running) | 58.3-58.5 | 33.3 | 1.4 % | 67-79 % | 11.7 GB |

**Reading:** the first baseline series is lower only because the pipeline/shader cache was still warming; every later series, including the unchanged baseline B control, sits at 59.2-59.4 fps with a run-to-run spread of 0.1-0.4 fps. **No variant beats the control beyond that spread**, so none is kept as a default (the rule "no change without a gain"). The replay is vsync-capped at 60 fps; the mean is ~59.3. Remaining gap to the acceptance line: 1.1-1.3 % of frames take 33 ms (a missed vblank), just over the 1 % that p99 <= 20 ms needs; one baseline run and one -O3 run happened to get 0.87-0.95 % (p99 16.7 ms).

The >20 ms frames fall into the same route segments in every run and every variant (first ~60 s of the replay, 20-30 per 10 s) = scene-bound, not a compiler matter.

### Profile (`sample` 10 s at replay +100 s, baseline, `out/bench/sample-baseline.txt`; summarised with `tools/macos/sample_top.py`, unsymbolized/Rosetta leaf frames credited to the nearest symbolized caller)
GPU command thread `shadPS4:GpuCommandProcessor` (5310 samples), top by self time: `cthread_yield` 51.3 % (inside `Rasterizer::DrainDrawPipe`, spinning until the draw pipeline has drained) / `pthread_mutex firstfit_lock_wait` 12.9 % / `Rasterizer::Draw` 6.8 % / `pthread_mutex firstfit_wake` 4.3 % / `ProcessCompute<true>` 2.4 % / `pthread_cond_signal` 2.1 % / `ProcessGraphics` 2.0 % / `PipelineCache::TryPreparedPipeline` 1.6 % / `Rasterizer::PostDraw` 1.4 % / `Liverpool::Process` 1.4 % / `CollectRingBindings` 1.1 % / `Shader::Info::RefreshFlatBuf` 0.9 % / MoltenVK `MVKCommandBuffer::begin` 0.9 % / `std::mutex::unlock/lock` 0.8/0.7 % / `PipelineCache::GetProgram` 0.6 % / `MVKCommandBuffer::reset` 0.6 % / `vkGetSemaphoreCounterValue` 0.4 % / ... Inclusive: `Process` 58 %, `OnSubmit` -> `DrainDrawPipe` 35 %, `ProcessCompute` 25 % (of which `DispatchDirect` 6 % incl. pipeline refresh 6 %), `ProcessGraphics` 17 %, `Scheduler::WaitHostCopies` 5.4 %.
**Finding: ~64 % of the command thread is waiting (yield-spin + mutex), not translated compute.** The "GPU-thread CPU 75 %" in PERF is mostly this spin; it is not the limit by itself. The busiest worker is `bb:DrawRec` (draw recording into MoltenVK; its lambda + `AGXG16XFamilyComputeContext`, `MVKCommandEncoder::encode`, `BindResources`); `bb:VkRecorder` 45 % cond-wait, `DrawPrep0-7`, `DrawScan`, `Present`, `NdJobWorker` mostly idle (60-95 % waiting). After the three variants there is no "after" profile: the builds did not change the numbers, so none was profiled.

### Chosen configuration
**Unchanged Darwin defaults** (RelWithDebInfo -O2, LTO OFF, PGO off). Added to the Darwin path only, opt-in: `BB_OPT` (e.g. `-O3`) and `BB_HOST_FLAGS` (e.g. `-mavx2 -mfma -mbmi -mbmi2`) in `build.sh`; `BB_LTO=ON` already existed. Build-time change: none to the defaults. Reference: a ThinLTO libbbgpu build took 36 s wall (1257 % CPU, incremental) and the library is 3 % larger; -O3 and AVX2 builds took a similar time.

### Not done / open issues
1. **PGO (4d)**: not run. With the three codegen variants indistinguishable from the control and the thread dominated by waiting, a gain is unlikely; it would need a clang instrumented build (`-fprofile-instr-generate`, `pgo/macos/`) + replay + `llvm-profdata`, ~1 h.
2. **Targeted fixes / settings sweep (steps 5)**: not done. Leads from the profile: (a) the `DrainDrawPipe` `sched_yield` spin on the command thread burns a core for nothing (replace by a wait; does not change frame pacing) and the `DrawRec` thread is the real critical path (MoltenVK command encoding); (b) the 33 ms frames in the first minute of the route: profile that segment (`BENCH_SAMPLE_AT=20`) to see whether it is pipeline creation, descriptor/`CopySparseMemory` or a MoltenVK stall; (c) sweep `BB_ARENA_PAGE_BITS`, DrawPrep worker count, `MVK_CONFIG_*` against `frames_over_20ms_pct`, since the mean is at the vsync cap.
3. p99 <= 20 ms is borderline (0.9-1.6 % of frames over 20 ms against a 1 % line); the mean (59.3 >= 58) passes.
4. The frame comparison at replay +60 s is by eye only (combat is not deterministic, so pixel diffs are meaningless); start frames match the owner's "Sanctuary?" checkpoint in every run; no visual difference between variants.
5. The first baseline series is a cold-cache number; a fair comparison needs a warm cache (the replay in the harness does not warm it separately; run a throwaway replay first for new variants).
6. Machine contention is the main risk to these numbers (the owner's game, Unity); the harness now guards against it.

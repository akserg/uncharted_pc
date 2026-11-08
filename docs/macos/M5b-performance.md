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

## Round 2 (hitches and the spinning GPU command thread)

**Branch** `macos-perf` (from `macos` `f869f7b`). Machine: M4 Max 48 GB, macOS 26.6.2, no thermal warning recorded (`pmset -g therm`), Unity idle (0-5 % CPU), no other `bb-probe`. Per-run files: `out/bench/<label>-<n>.txt`, traces `out/bench/frames-<label>-<n>.txt`, machine state `out/bench/r2ctrl-machine-*.txt`.

### Harness changes
- `bench.sh`: every series starts with one **warm-up replay** (run, not counted, `<label>-warmup.txt`, header `warmup=1`); `BENCH_WARMUP=0` turns it off. New `process_cpu_pct` (process CPU seconds over the replay window, from `ps -o time`), `BENCH_TRACE=1` (frame trace), `BENCH_SAMPLE_S` (length of the `sample`).
- **`BB_FRAME_TRACE=1`** (`gpu/shim/bbport_frame_trace.h`; file `BB_FRAME_TRACE_FILE`; `bench.sh` sets it with `BENCH_TRACE=1`): one line per flip: frame, t, replay time (`BB_REPLAY_T0_US`, exported by `runtime_pad.c` on Darwin), frame ms, then per slot the summed and the largest single time since the previous flip: `drain` (command thread waiting in `DrawPipe::Drain`), `rec` (bb:DrawRec running packets), `submit` (`vkQueueSubmit`), `present` (acquire, present, fence waits), `pipe` (pipeline creation). Off: one branch per scope, no logging; empty on other platforms. `tools/macos/trace_hitches.py` summarises traces.

### What invalidates the cache (item 2)
Checked on the worktree's `user/cache/CUSA02344-nan1-106b-1a060209` (3816 files, 61 MB: 1312 `.spv`, 1313 `.meta`, 1188 `.key`, `profile.bin`, `device.bin`, `vulkan_driver.vkc` 18.6 MB) before and after a **rebuild with no content change** (`touch` of three sources, `build.sh`): both binaries came out different (`LC_UUID` and md5 of `libbbgpu.dylib` and `bb-probe` changed), and the cache directory was **untouched**: same 3816 files and size, 3786 blobs older than the rebuild, only `vulkan_driver.vkc` and 4 new pipeline keys rewritten by the game. So a rebuild or an update of the binary does **not** throw the game's cache away. What does: (a) `ShaderBinaryVersion` (12) / `ShaderMetaVersion` (8) / `PipelineKeyVersion` (6) in `vk_pipeline_serialization.cpp`, bumped by hand when the serialization changes (a stale blob is rebuilt, "1 stale pipelines" in the log); (b) `profile.bin` (the `Shader::Profile`) or `device.bin` (`<vendor>-<device>`) differing -> `db.Clear()`, and the per-GPU directory name; (c) the driver cache `vulkan_driver.vkc` is only handed to Vulkan when its header (vendor, device, `pipelineCacheUUID`) matches, i.e. it follows the MoltenVK/driver version, not the game build. Players: an update that does not bump those versions or change MoltenVK keeps the cache. The Metal compiler's own cache is separate and system wide (`$DARWIN_USER_CACHE_DIR/com.apple.metal/<gpu>_<os>/functions*.data`, `.../32024/libraries*.data`, generations rotate by size); it is keyed by GPU/OS, not by the binary.
**The round-1 "cold cache" explanation is not supported:** every baseline log of round 1 and round 2, including the 55-58 fps ones, preloads 1179-1187 of the same pipelines in about 200 ms, and no pipeline is created during the replay (`pipe` slot = 0.00 in every frame). The 55-58 fps series happened with an equally full game cache. What differs is run-to-run noise of the machine (see below), not the pipeline cache.

### Control baseline with warm-up (unmodified code, rebuilt binaries)
| series | mean fps | frames > 20 ms | GPU-thread CPU | process CPU |
|---|---|---|---|---|
| `r2base` (before the warm-up existed; 3 runs) | 55.92 / 58.51 / 55.19 | 7.1 / 2.5 / 6.0 % | 74 / 77 / 70 % | 359 / 382 / 355 % |
| `r2ctrl` warm-up (not counted) | 59.16 | 1.36 % | | |
| `r2ctrl` runs 1-3 | **59.20 / 59.15 / 58.84** (spread 0.36) | 1.31 / 1.40 / 1.89 % | 79.7 / 76.6 / 80.4 % | 387 / 369 / 390 % |
Accepted: spread under 0.5 fps; the hitch rate of run 3 (1.89 %) is just above round 1's warm range (0.9-1.6 %), so the hitch rate of one run can only be trusted to about +-0.5 percentage points. **The noise is not gone:** later series (with tracing) gave 58.1 (warm-up), 59.09, **53.91**; and 57.46 (warm-up), **56.29**, 58.84. The 53.9 run (`r2trace-2`) was slower from about replay second 90 on, in all stages at once (see below), without any contention recorded (Unity <= 5 %, no second bb-probe). Anything below about 1 fps difference between single runs is therefore noise.

### Hitch causes (Step 2; traced runs `r2trace-1` 59.09 fps / 1.48 % of frames > 25 ms, and `r2trace-2` 53.91 fps / 10.6 %)
Mean of the per-flip slot sums (ms), normal frames (<= 20 ms) vs 33 ms frames:
| run | | drain | rec | submit | present | pipe |
|---|---|---|---|---|---|---|
| 1 | normal | 8.2 | 9.8 | 5.1 | 0.03 | 0.00 |
| 1 | hitch (> 25 ms, 183 frames) | 15.0 | 18.6 | 9.0 | 0.03 | 0.00 |
| 2 | normal | 8.5 | 9.8 | 6.0 | 0.03 | 0.00 |
| 2 | hitch (1197 frames) | 16.0 | 17.5 | 8.6 | 0.06 | 0.00 |
- **Not** a pipeline compile (`pipe` 0.00 everywhere in the replay), **not** the present wait (0.03 ms), **not** a single long stall in most cases (no single slot event >= 8 ms in 92 of 183 hitch frames; DrawRec and drain both >= 8 ms in 86).
- The hitch frames are the **heavy frames of the DrawRec thread**: 167 of 183 hitch frames (91 %) have > 14 ms of DrawRec work (median 15.7 ms) against 9.8 ms on average; `rec` includes the 5-10 ms `vkQueueSubmit` of the flip (`submit` doubles from 5.1 to 9.0 in hitch frames). The command thread waits behind it (`drain` ~ `rec` - 3 ms). The frame budget is 16.7 ms; DrawRec has about 14 ms before the vblank is missed.
- Group by replay time (hitches per 10 s, run 1): 0-10 s: 12, 20-60 s: 12/18/23/8 (the densest segment, 30-50 s), 60-70: 12, 80-90: 11, 110-130: 13/9, 150-170: 11/17, otherwise 0-9. The same segments are dense in every run (scene-bound). Run 2 shows the same pattern at 1.5-2x the density, and from second 90 on the whole GPU stack is slower (per frame: rec 10.3 -> 12.8 ms, submit 5.3 -> 8.3 ms, hitches 100-137 per 10 s from second 110): a **uniform CPU-side slowdown of the run**, not one component. Cause not found (no Unity/probe load; footprint equal).
- Rare second mode (run 2: 4 + 6 + 1 + 4 + 11 frames per 30 s window with drain - rec > 20 ms, e.g. 200 ms frame at replay 22 s: drain 190 ms, rec 22 ms): the command thread waited while DrawRec was not running packets - DrawRec not scheduled (wake-up latency of its sleep, `Run`'s `wake.wait`) or running something outside the handler. In run 1 this never happens (0 frames in 12396 with gap > 20 ms).
- **`sample` 3 s at replay 38 s** (`out/bench/r2samp-1.sample.txt`, the densest segment): command thread `GpuCommandProcessor`: `cthread_yield` 59 % self (inside `Drain`, 33.5 % inclusive under `OnSubmit`/`ProcessCompute`), `Rasterizer::Draw` 6.4 %, mutex lock-wait 6.2 %, wake 2.9 %, `pthread_cond_signal` 2.2 %. **`bb:DrawRec` (the critical path):** 58 % in `DrawPipe::Run` (29.8 % of it spinning in the wait-for-work loop, 4 % blocked in `atomic_wait`), 35 % in `RunDrawPacket`, of which **33 % of the thread's time is `Scheduler::SubmitExecution` -> `vkQueueSubmit` -> `MVKQueue::submit` -> `MVKCommandBuffer::submit` -> `MVKCommandEncoder::encode` (515 of 540 samples)**: MoltenVK encodes the Metal command buffer synchronously inside the call, on the DrawRec thread (`MVKCmdDispatch::encode` -> `getMTLComputeEncoder` -> `AGXG16XFamilyComputeContext initWithCommandBuffer`, about 20 % of the submit). The rest of its record time is `BindTextures`/`DrawRecord`/`BindResources`.
- **Conclusion for step 4:** the hitch cause is DrawRec-bound frames whose recording plus MoltenVK encode exceeds ~14 ms. Nothing in the port stalls (no compile, no arena growth, no texture upload visible as a slot). The lever is the synchronous encode in `vkQueueSubmit` (`MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS`, sweep in step 5) and the number of compute context switches per command buffer (`AGXG16XFamilyComputeContext initWithCommandBuffer` appears per dispatch encoder).

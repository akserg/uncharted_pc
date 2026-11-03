# M5 — One game playable

## Goal
Uncharted 1 chapter 1 is completable at 1080p, 60 FPS target, upscalers off, on the Apple GPU under
Rosetta, with audio and a controller.

## Preconditions
- M4 done.

## Who does what
The agent cannot play the game. M5 is a loop: the **agent** prepares instrumentation and fixes; the **owner**
plays and hands back `user/last_run.log` (and, for visual bugs, a screenshot + where it happened). The agent
must never wait idle for gameplay: finish its own tasks, then stop with clear instructions for the owner.

## Agent tasks (before and between play sessions)
A1. **Instrumentation**: a perf log line every 5 s (`BB_PERF_LOG=1`): FPS, frame-time p50/p99, GPU-command-thread
    CPU share (Darwin: `thread_info(THREAD_BASIC_INFO)` for that thread — the Linux `pthread_getcpuclockid` /
    `RUSAGE_THREAD` paths are compiled out), process `phys_footprint` (`task_info(TASK_VM_INFO)`).
A2. **Crash capture**: on any fatal signal/assert, the last 200 log lines + the M3 thread dump are already in
    `user/last_run.log`; make sure the guest RIP, image offset and GPU checkpoint (`BB_GPU_CHECKPOINTS=1`) are
    printed, so one log is enough to start a fix.
A3. **MigrateArena**: force it (e.g. `BB_ARENA_PAGE_BITS` small enough that a resource spans two arenas) in a
    run to the START menu; verify no corruption/assert.
A4. **Wave32**: find a recompiled game shader that uses ballot/subgroup ops (shader dump), confirm
    `LowerWave64BallotPass` ran on it, and compare its output with expectations where possible.
A5. **nullDescriptor audit**: MoltenVK lacks `nullDescriptor`. Grep the renderer for null buffer/image
    descriptors (VK_NULL_HANDLE in descriptor writes, `nullDescriptor` branches) and replace them with dummy
    resources where the feature is off. List each site.
A6. Rebuild, run to the first cutscene, verify nothing regressed, then hand over to the owner.

## Owner tasks
1. Play through U1 chapter 1 end to end (BB_PERF_LOG=1). For each crash/assert/visual bug: note where,
   keep `user/last_run.log` (copy it, it is overwritten next run), screenshot if visual.
2. Test audio (speakers/headphones), a controller (USB and Bluetooth), keyboard + mouse look.

## Agent tasks after each play session
1. Triage each crash/assert from the owner's logs with the stopping point and the last
   lines of `last_run.log`; fix those that are macOS-specific. Game-logic asserts that reproduce on
   Linux/Windows upstream are **not** this milestone's problem — note them and move on.
2. Audio (`runtime_audio.c`, SDL3): confirm output device selection, sample rate, no underruns.
3. Pad (`runtime_pad.c`, SDL3): confirm a DualSense or Xbox controller over USB and Bluetooth;
   keyboard+mouse; the `ms_abi` camera hooks fire (mouse look).
4. Perf table at the same three spots as M0: FPS, frame time, and the GPU-command-thread share
   (BB_PERF_LOG, A1). M0 had no runtime baseline: report absolute numbers. Record unified memory use (`footprint`
   or Activity Monitor) at first combat.
5. Try FSR 3.1 once everything else is stable. If it crashes in a compute pipeline, disable it on
   macOS and note why.
6. Record macOS version, Mac model, memory size.

## Acceptance
- Chapter 1 completed without a macOS-specific crash.
- Perf table vs M0 in Result.
- Memory footprint recorded; say explicitly whether a 16 GB Mac is viable.

## Hazards
- Rosetta overhead on the GPU command thread: if the frame time is dominated there, there is no
  cheap fix — note it, do not start optimising inside this milestone.
- Shader cache first-run stutter is expected; measure on a second run.

## Result
_(fill in: date, commit, hardware, crash list with resolution, audio/pad findings, perf table,
memory footprint, FSR 3.1 outcome, open issues)_

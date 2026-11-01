# M5 — One game playable

## Goal
Uncharted 1 chapter 1 is completable at 1080p, 60 FPS target, upscalers off, on the Apple GPU under
Rosetta, with audio and a controller.

## Preconditions
- M4 done.

## Tasks
1. Play through U1 chapter 1 end to end. Log every crash/assert with the stopping point and the last
   lines of `last_run.log`; fix those that are macOS-specific. Game-logic asserts that reproduce on
   Linux/Windows upstream are **not** this milestone's problem — note them and move on.
2. Audio (`runtime_audio.c`, SDL3): confirm output device selection, sample rate, no underruns.
3. Pad (`runtime_pad.c`, SDL3): confirm a DualSense or Xbox controller over USB and Bluetooth;
   keyboard+mouse; the `ms_abi` camera hooks fire (mouse look).
4. Perf table at the same three spots as M0: FPS, frame time, and the GPU-command-thread share
   (upstream's overlay / log stats). Compare with M0 baseline. Record unified memory use (`footprint`
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

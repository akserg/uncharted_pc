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
_(fill in: date, commit, machine, build status, test results, perf table, RSS, open issues)_

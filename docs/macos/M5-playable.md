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

### Agent part (A1–A6), 2026-11-03
**Agent tasks A1–A6 are done. The playtest doc is ready and M5 now waits for the owner's play session.** M5 itself stays open: chapter 1, audio, pad and the perf table at the three spots need gameplay.

Machine: MacBook Pro M4 Max, 48 GB, macOS 26, x86_64 under Rosetta (`ROSETTA_ADVERTISE_AVX=1`), MoltenVK 1.4.2 (`BB_GPU_ID=0`), Vulkan SDK 1.4.363.0. Branch `macos`.
Preconditions (Step 0): tree clean apart from `m gpu/third_party/fsr-vulkan`; `build.sh --test` green; `run.sh` reached the START menu (frame dump); **`screencapture -x` still fails** (`could not create image from display`: no Screen Recording permission for this shell), so `BB_DUMP_FRAME` was used throughout.

Commits (all `macos:`): `51ddbe5` A1 PERF log · `e48b8fb` A2 crash record · `ea5c808` A3 forced arena migration knob · `5a74f88` A5 null-descriptor stand-ins · `f39ff44` A1 follow-up (`t=` in PERF, F8 marker) · `67febf1` M5-PLAYTEST.md · this Result. A4 and A6 are measurements with no code change.

### Acceptance (agent part)
| Item | Result |
|---|---|
| `ROSETTA_ADVERTISE_AVX=1 bash build.sh --test` | **PASS**: exit 0, 18 `PASS` lines. Run after each code commit, last one after `f39ff44` (the doc commits change no code) |
| PERF lines every 5 s with all five values | **PASS** (sample below; `window_s=5.00`) |
| Deliberate fault leaves a complete crash record in `user/last_run.log` | **PASS**: SIGSEGV and GPU assertion both tested (below) |
| MigrateArena run clean | **PASS** with `BB_ARENA_PAGE_BITS=24`: 13 migrations, START menu, no assert, frames look correct |
| Ballot lowering evidence | **PASS for ReadLane** (6 lowerings in `cs 0x43b8ee5e`, IR below). No `Ballot`/`SubgroupLtMask` instruction occurs in U1 up to the first cutscene |
| nullDescriptor sites listed and handled | **PASS** (table below; 2 of the stand-ins were used in a real run) |
| No regression to the first cutscene | **PASS** (same route as M4b, 60 FPS, footprint as in M4b) |
| `docs/macos/M5-PLAYTEST.md` exists | **PASS** |

### A1: BB_PERF_LOG
`gpu/shim/bbport_perf_log.h` (new, `__APPLE__` only). It is hooked into `VideoOutDriver::Flip` (`driver.cpp`) and `Liverpool::Process` (`liverpool.cpp`, which registers the GPU command thread's Mach port). All hooks are `#ifdef __APPLE__`. The format is one line every 5 s of guest flips:
```
PERF: t=<s since launch> fps=<flips/s> ft_p50=<ms> ft_p99=<ms> gpu_thread_cpu=<%> footprint_mb=<MB> frames=<n> window_s=<s>
```
- **fps / ft_p50 / ft_p99**: intervals between guest flips (`sceVideoOutSubmitFlip`), measured on the CPU. With the 60 Hz vblank they are paced (16.67 ms); they do not show stalls inside MoltenVK presentation.
- **gpu_thread_cpu**: user+system time of `shadPS4:GpuCommandProcessor` from `thread_info(THREAD_BASIC_INFO)`, as a % of one core over the window.
- **footprint_mb**: `task_info(TASK_VM_INFO).phys_footprint`, the number Activity Monitor shows as "Memory".
- **F8** in the game window prints `MARK <n> t=<s> (F8)`, so the owner can tie PERF lines to places (`window.cpp`, Apple only). **Not exercised**: this shell may not send keystrokes (`osascript is not allowed to send keystrokes`).

Sample (START-menu run, `m5-t`):
```
PERF: t=26 fps=60.0 ft_p50=16.67 ft_p99=16.69 gpu_thread_cpu=20.3% footprint_mb=8660 frames=300 window_s=5.00
PERF: t=31 fps=60.0 ft_p50=16.67 ft_p99=16.69 gpu_thread_cpu=20.5% footprint_mb=8685 frames=300 window_s=5.00
```

### A2: crash record
On a fatal signal (SIGSEGV/SIGBUS/SIGILL/SIGTRAP, and now **SIGABRT**), a GPU-library assertion (`assert_fail_impl`) or a guest debug exception (`raise_exception`), `user/last_run.log` (run.sh tees stdout+stderr) now gets one block. It sits between `CRASH: ===== crash record (...) =====` and `CRASH: ===== end of crash record =====` and contains:
- `rip=` and whether it is in the guest image (image base + **image offset**) or in host code (module+offset, symbol); the fault address; general registers.
- **Every thread's frames**: the crashing thread from its signal context, all others through the existing SIGUSR2 dump. Each line has `rip`, `image-relative` (= guest offset when the frame is in the guest) and `host-relative` (= bb-probe offset). For an assertion (no signal context) the thread dumps itself through SIGUSR2.
- **GPU checkpoint**: `BB_GPU_CHECKPOINTS=1` relied on `VK_NV_device_diagnostic_checkpoints`, which MoltenVK does not have (it was a no-op on macOS). On Apple without the extension it now keeps the CPU-side ring: `NoteCheckpoint` still records but returns null, so no `setCheckpointNV` is emitted. The report prints the last 4 draws/dispatches the GPU command thread **recorded**, with shader hashes, counts and bound buffers. These are not necessarily the last ones the GPU executed. Without the variable, the record says `off (BB_GPU_CHECKPOINTS=1 records the last draws)`.
- Code: `src/probe.c` (`crash_record`, `crash_hook`, SIGABRT, test hook), `src/runtime.h` + `runtime_kernel.c` (`runtime_crash_hook` pointer), `gpu/shim/common/assert.cpp`, `gpu/bbgpu.h` + `bbgpu.cpp` (`bbgpu_crash_report`, `bbgpu_crash_test_assert`), `vk_instance.cpp`/`vk_rasterizer.cpp` (CPU-only checkpoints). All `#ifdef __APPLE__`. The exit codes are unchanged (139 for SIGSEGV, 23 for a GPU assertion).
- Test hook, **left in, off unless set**: `BB_CRASH_TEST=<seconds>[:segv|abort|gpu-assert]` starts thread `bb:crash-test`, which faults after `<seconds>`.

Sample (`BB_GPU_CHECKPOINTS=1 BB_CRASH_TEST=45:segv`, exit 139, 61 threads dumped, 455 frame lines):
```
CRASH: ===== crash record (fatal signal, signal 11) on thread 'bb:crash-test' tid 49420673 =====
CRASH: rip=0x5550 in host code .../out/bb-probe+0x4550 (crash_test_thread), image base 0xbf00000; fault address 0x10
CRASH: rax=000000000000000c rbx=000000020430cdde ... rbp=0000000208414fb0 rsp=0000000208414fa0
CRASH: thread stacks (tid, rip, image-relative = guest image offset, host-relative = bb-probe offset):
  tid=0000000002f21981 rip=0000000000005550 image-relative=fffffffff4105550 host-relative=0000000000004550
  ...
  tid=0000000002f2197f rip=000000000c34aa2a image-relative=000000000044aa2a host-relative=000000000c349a2a   <- guest frame
  ...
CRASH: GPU checkpoint:
GPU checkpoint (CPU side, no VK_NV_device_diagnostic_checkpoints): 201816 draws/dispatches recorded; the last ones:
GPU crash:   #201815 draw 00000000ad649c68 000000002e403aa4 counts 42 1 0
GPU crash:   #201816 draw 00000000ad649c68 00000000f90eaf63 counts 6 1 0
CRASH: ===== end of crash record =====
```
`BB_CRASH_TEST=30:gpu-assert` gave `crash record (GPU library assertion, signal 0)`, `no signal context (assertion)`, all thread stacks and the checkpoint, then `STOP: GPU library assertion failed`, exit 23.
Not exercised: a fault *inside guest code* (the in-image branch of `crash_record` and the pre-existing `Guest fault ... at guest offset` line), SIGABRT, `raise_exception`.

### A3: MigrateArena
`BB_ARENA_PAGE_BITS` accepts 24..32 on Apple (was 30..32). This is `MIN_FORCED_ARENA_PAGE_BITS` in `buffer_cache.h`; it equals `MIN_ARENA_PAGE_BITS` elsewhere, so `MAX_ARENA_PAGES` and the clamp are unchanged off Apple. The default and the driver-limit loop are unchanged.
- `=28` (256 MiB): 7 arenas, **no migration** up to the START menu.
- `=24` (16 MiB): 32 arenas, **13 migrations** (`Buffer cache: arena 24 created by migration for guest 0x7017000000, 48 MiB` … `arena 45 ... 0x7014000000, 224 MiB`, merges growing repeatedly), through START menu → Friend Stats dialog, 125 s. No assert, crash or device loss, 60 FPS. Frame dumps of the START menu and dialog look correct.
- Side observation: with 16 MiB pages the process footprint at the START menu was **~6.1–6.3 GB vs 8.6–8.8 GB** with the default 1 GiB pages. Merged arenas are never freed (as in the sparse path), so a long session may grow back. Not adopted: owner decision below.

### A4: wave32 / LowerWave64BallotPass
Cold shader cache (cache moved aside and restored afterwards), `BB_GPU_LOG=info BB_DUMP_SHADERS=1`, run to the first cutscene. The pass ran on **`cs 0x0000000043b8ee5e`**, an 8×8×1 = 64-thread compute shader (the one M4b found using ShuffleXor/BroadcastFirst):
```
GPU [Render.Recompiler] <Info> lower_wave64_pass.cpp:138 LowerWave64BallotPass: Lowering ReadLane instruction for wave64   (x6)
```
Final IR (`out/m5-a4/cs_0x0000000043b8ee5e.irprogram.txt`, not committed). Before the pass each was `%x = ReadLane %v, #0` used directly. After:
```
%105 = GetAttributeU32 LocalInvocationIndex
%106 = ShiftRightLogical32 %105, #5          ; 32-lane subgroup id
%107 = ShiftLeftLogical32 %106, #2
%108 = IAdd32 %107, #512                      ; scratch after the shader's 512 B of LDS
%109 = ReadLane %100, #0                      ; lane 0 of this subgroup
       WriteSharedU32 %108, %109
       Barrier
%110 = BitwiseAnd32 %108, #0xfffffff8          ; slot of the even subgroup of the 64-lane pair (+ half*4, half=0)
%111 = LoadSharedU32 %110                     ; = GCN v_readlane lane 0 of the wave64
       Barrier
```
This is what wave64 semantics need: lane 0 of the 64-wide wave is lane 0 of the even 32-wide subgroup. No `... in non uniform control flow` warning. **No `Ballot` or `SubgroupLtMask` lowering occurred**: U1 has no such shader up to the first cutscene (every compute shader of the cold run was recompiled with the pass). Numeric results were not compared against a reference; the frames using it look right.

### A5: nullDescriptor audit
`Instance::IsNullDescriptorSupported()` (new). It is set from the robustness2 feature in the existing Apple branch of `CreateDevice` and is always true elsewhere, where the feature is asserted. When it is false (MoltenVK), the rasterizer creates `NullResources` (`vk_null_resources.{h,cpp}`, new, Apple only):
- a 64 KiB zero-filled buffer (uniform | storage | vertex | index);
- 1×1 zero images in layout General for 1D, 1DArray, 2D, 2DArray (6 layers), 3D, 2DMsaa and 2DMsaaArray (4 samples, sampled only), each × {RGBA8 unorm (float), R32 sint, R32 uint};
- D32 depth 2D / 2DArray.

The dummy is chosen like the shader's declaration: `T#.GetViewType(is_array)` and `GetNumberClass(T#.GetNumberFmt())`, depth for `is_depth`, storage for `is_written`. The first use per site logs `Null descriptor stand-in first used: <site>`.

| # | Site (`vk_rasterizer.cpp`) | Null written | Done on Apple without nullDescriptor | Seen in a run |
|---|---|---|---|---|
| 1 | `BindBuffers`, `BufferType::ClipPlanes` with no user clip planes | `buffer = VK_NULL_HANDLE, VK_WHOLE_SIZE` | dummy buffer | **yes** (START menu) |
| 2 | `BindBuffers`, V# with zero address or size | same | dummy buffer | no |
| 3 | `ResolveVertexBuffers`, vertex stream with zero address/size | `VK_NULL_HANDLE` in `bindVertexBuffers(2)` | dummy buffer, size clamped to 64 KiB | no |
| 4 | `BindTextures`, T# address 0 / invalid format, and "Rejecting invalid T#" (both create a null `image_bindings` entry; the view is chosen there and used in the second pass) | `imageView = VK_NULL_HANDLE` (sampled or storage) | dummy view of matching type/class; **MSAA storage keeps null** (Metal has no MSAA storage textures) | **yes**: sampled (START menu) |
| 5 | `BindTexturesFromSet` (memoized texture set), entry without image | `imageView = VK_NULL_HANDLE` | dummy view from `stage.images[i]` and the prepared T# | no |

Checked and **not** null writes: `vk_camera_motion.cpp` (object view falls back to the depth view), `vk_temporal_upscaler.cpp:1332` (falls back to the motion view) and `:805` (null sampler only for storage-image bindings, where it is ignored), `vk_scene_resolution.cpp` (the stencil view is written only when it exists), `vk_frame_generation.cpp:490` (DLSS, Windows only), `fsr411.cpp` (FSR 4, disabled on macOS). `dstSet = VK_NULL_HANDLE` lines are push descriptors, not null resources. The renderer has no texel-buffer descriptors. Before this change MoltenVK accepted the null writes silently: no error was ever logged in M4b.

### A6: rebuild, run to the first cutscene, PERF
Same route as M4b (language → titles → START → New Game → Friend Stats → Game Mode → Campaign → quote → opening cutscene on the boat), warm shader cache, all A1–A5 changes in. No assert, crash, device loss, MoltenVK error or VUID in the log. Frames: START menu, Drake quote, boat cutscene all correct.

The "first cutscene" is a **decoded movie** (`MovieDecodeThread` created, `sceVideodec2AllocateComputeQueue`), not real-time 3D. That explains the low GPU-thread share. Real-time 3D numbers need the owner's session.

| Spot | fps | ft_p50 / ft_p99 (ms) | GPU-thread CPU | footprint |
|---|---|---|---|---|
| Language/boot screens (t≈10–16 s) | 60.0 | 16.67 / 16.68 | 5–6 % | 6.4–6.6 GB |
| START menu (and title, menus) | 60.0 (one window 57.4 at load) | 16.67 / 16.67–16.69 | **19–21 %** | **8.4–8.8 GB** |
| Opening cutscene (movie) | 59.6–60.0 | 16.67 / 16.67 | **4.5–5.6 %** | **9.2–9.4 GB** |
```
PERF: fps=60.0 ft_p50=16.67 ft_p99=16.67 gpu_thread_cpu=20.6% footprint_mb=8686 frames=300 window_s=5.00   (START menu)
PERF: fps=60.0 ft_p50=16.67 ft_p99=16.67 gpu_thread_cpu=5.0% footprint_mb=9181 frames=300 window_s=5.00    (cutscene)
```
(These lines predate the `t=` field.) Compared with M4b: same route and timing, START-menu footprint 8.6–8.8 GB vs 8.7 GB then. **No regression found.**

### Shared-file changes (for the Linux/Windows reviewer)
All of these are `#ifdef __APPLE__` or have no effect off Apple. Linux/Windows were **not** compiled here.
- `gpu/shadps4/core/libraries/videoout/driver.cpp`, `video_core/amdgpu/liverpool.cpp`, `gpu/shim/window.cpp`, `gpu/shim/common/assert.cpp`, `gpu/shim/bbgpu.cpp`, `gpu/bbgpu.h`, `src/probe.c`, `src/runtime.h`, `src/runtime_kernel.c`: Apple-only blocks.
- `renderer_vulkan/vk_instance.cpp`: `g_gpu_checkpoints_nv` and its CPU-only fallback (Apple); `null_descriptor = ...` inside the existing Apple branch. `vk_instance.h`: new member `null_descriptor{true}` and `IsNullDescriptorSupported()` (all platforms; always true off Apple).
- `renderer_vulkan/vk_rasterizer.{h,cpp}`: Apple-only blocks (NoteCheckpoint early return, ReportCheckpoints CPU branch, NullResources member and the 5 sites).
- `buffer_cache/buffer_cache.h`: `MIN_FORCED_ARENA_PAGE_BITS` (= `MIN_ARENA_PAGE_BITS` off Apple, so `MAX_ARENA_PAGES` is unchanged); `buffer_cache.cpp`: the `BB_ARENA_PAGE_BITS` clamp uses it.
- New files: `gpu/shim/bbport_perf_log.h`, `renderer_vulkan/vk_null_resources.{h,cpp}` (bodies under `#ifdef __APPLE__`; the .cpp compiles to nothing elsewhere, picked up by the existing glob).

### Open issues
1. **Overlay key**: the settings overlay opens with Insert, which MacBook keyboards lack. Only the controller's L3+R3 works on a MacBook. Not changed: a decision on an Apple key is needed (see below).
2. The F8 marker is untested (no keystroke injection for this shell). The owner's session tests it.
3. Screen capture is still not permitted for the agent's shell (M4b review decision 3 not yet in effect); `BB_DUMP_FRAME` was used.
4. A2: a fault inside guest code, SIGABRT and `raise_exception` were not triggered. On MoltenVK the GPU checkpoint is "last recorded", not "last executed".
5. A4: only ReadLane lowering is exercised in U1; Ballot/SubgroupLtMask lowering is still unseen. Compute results were checked by IR inspection only.
6. A5: sites 2, 3 and 5, storage-image, depth and MSAA dummies were not hit in the runs. Reads from a dummy return 0, but a shader that queries the image size gets 1×1 (a true null descriptor gives 0).
7. PERF frame times are guest flip intervals (60 Hz paced). Presentation-side hitches would not show.
8. Real-time 3D perf, memory at first combat, audio, pad (USB/Bluetooth), mouse look, chapter 1 completion and FSR 3.1: owner session and later agent rounds.
9. `BB_CRASH_TEST` is left in the build, off unless set.

### Needs a decision from the owner
- Arena pages: 16 MiB pages (`BB_ARENA_PAGE_BITS=24`) cut the START-menu footprint by ~2.5 GB in the A3 run, at the price of many migrations (old arenas are not freed). Measure them in gameplay as a candidate default? This is not a performance change, so not done in M5.
- An Apple key for the overlay (e.g. F12 or Cmd+,) in addition to Insert?

### Owner play sessions


# M4 — Cocoa window, MoltenVK, first frame

## Goal
U1 main menu renders in an SDL3 Cocoa window through MoltenVK.

## Preconditions
- Game dump available (`BB_GAME_DIR`). **First do M3 task 9** (boot to first GNM/VideoOut call) and record it
  in M3's Result under "Task 9 (done in M4)", including the patched gs-load count per ELF.
- M3 done. LunarG Vulkan SDK (universal) installed; `$VULKAN_SDK/lib/libMoltenVK.dylib` lists x86_64.
- `VK_ICD_FILENAMES=$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json` (or the loader finds it).
  `vulkaninfo` (x86_64) lists the Apple GPU.

## Tasks
1. **Cocoa main-thread requirement** (found on the first real run, 2026-11-02): `bbgpu_init` → `bbgpu.cpp:239`
   creates the SDL window and pumps events on a background `std::thread`, while the guest runs on the
   process main thread (`enter_on_stack`, probe.c). Cocoa allows windows and event pumping only on the
   main thread; SDL fails with `Failed to initialize SDL video: No available video device`.
   Fix on `__APPLE__` only:
   - `probe.c` `main()`: first thing, start a pthread "bb:main" (stack >= 16 MiB, `pthread_attr_setstacksize`)
     that runs the existing main body (rename to `probe_main(argc, argv)`), then the real main thread calls
     `dispatch_main()` (never returns). Process exit stays `exit()/_Exit()` from any thread, as today.
     The whole loader + guest setup (signals/sigaltstack, `runtime_memory_reserve`, `set_gs`, enter_on_stack)
     stays on bb:main, so per-thread TLS is unaffected. Check every place that assumes "main thread"
     (e.g. `pthread_main_np`, watchdog, signal masks) and list them in Result.
   - `bbgpu.cpp`: instead of `std::thread` for the window loop, `dispatch_async_f(dispatch_get_main_queue(), ...)`
     with the same body (create WindowSDL, signal ready, PollEvents loop). The existing condition-variable
     wait for `g_window_ready` stays.
   - Any other SDL video/window call made off the main thread (resize, fullscreen toggle, title, cursor,
     clipboard, message boxes): route through the main queue (`dispatch_sync` only if the main thread can
     never be waiting on the caller, else `dispatch_async`). Grep `SDL_` in `gpu/shim` and list them.
   - Verify: `SDL_IsMainThread()` is true inside WindowSDL's constructor (log it once).
2. **Window** (`gpu/shim/window.cpp`): finish the `cocoa` branch from M1. Use
   `SDL_Metal_CreateView(window)` + `SDL_Metal_GetLayer(view)`; set `window_info.type = Metal`,
   `render_surface = CAMetalLayer*`. `vk_platform.cpp` already creates `vk::MetalSurfaceCreateInfoEXT`
   from that (`VK_USE_PLATFORM_METAL_EXT` is defined at the top of the file — check it is defined for
   this build, not only in shadPS4's original CMake).
3. **Instance**: enable `VK_KHR_portability_enumeration` + `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`,
   and `VK_KHR_portability_subset` on the device. Mirror whatever shadPS4's `vk_instance.cpp` does under
   `#ifdef __APPLE__`; this tree is a fork of it so the diff should apply almost verbatim.
4. **Feature gates** (`vk_instance.h`): `subgroupSizeControl && maxSubgroupSize >= 64` is false on Apple
   (wave32). Find every use of that predicate and the `shaderFloat16`/`shaderInt8`/`shaderInt16` gates;
   confirm `lower_wave64_pass` is taken when subgroup 64 is unavailable, and that the "NVIDIA crash fix"
   64-wide compute special case has a wave32 path. Record which gates are false on the Apple GPU.
5. **Host shaders**: `glslang --target-env vulkan1.3` output must be accepted by MoltenVK. Any that use
   unsupported features will fail at pipeline creation — list them.
6. **Upscalers**: DLSS/XeSS are `_WIN32`-only; FSR 4 is still compiled on Apple (M1 decision) — force it off at run time on `__APPLE__` (settings default + refuse selection with a log line) and that config defaults on macOS select
   TAA or "off". FSR 3.1 compiles but is not required to work in M4.
7. **Run** to the main menu. Capture `user/last_run.log` and MoltenVK's own log
   (`MVK_CONFIG_LOG_LEVEL=2`) for the first frame.

## Acceptance
- Screenshot of the U1 main menu rendered on the Apple GPU (attach path in Result).
- Result lists every Vulkan feature/extension the renderer wanted that MoltenVK does not provide,
  and what was done about each (lowered, disabled, ignored).

## Hazards
- MoltenVK advertises Vulkan 1.2/1.3 depending on version; if `VK_API_VERSION_1_3` device creation
  fails, check the MoltenVK version shipped in the Vulkan SDK and whether `vk13_features` are queried as optional.
- Timeline semaphores, `VK_KHR_dynamic_rendering`, descriptor indexing: supported by recent MoltenVK
  but check `vulkaninfo` for each the renderer requires.
- 64-wide compute shaders that assume `gl_SubgroupSize == 64` will produce garbage, not errors.

## Result

**M4 is NOT complete. Tasks 1, 2, 3 and 6 are done; 4 and 5 are done as far as they can be checked without a first frame; task 7 (U1 main menu, screenshot) is BLOCKED by one architectural gap: the buffer cache is built on sparse-residency buffers, and neither MoltenVK nor KosmicKrisp provides `sparseBinding`.** U1 boots and runs ~8-9 s of game code, then stops with `Failed to get buffer device address (size 0x100000000, type 4, flags { SparseBinding | SparseResidency })`. Decision needed (see "Blocked").

Date 2026-11-02, branch `macos`, MacBook Pro M4 Max 48 GB, macOS 26, x86_64 under Rosetta, Vulkan SDK 1.4.363.0 (universal), `ROSETTA_ADVERTISE_AVX=1`. `BB_GAME_DIR=/Users/akserg/Projects/CUSA02344`.
Commits (all `macos:`): `a1e7c50` main thread (task 1) · `4ebe35b` Metal surface + portability + robustness2 gaps (tasks 2, 3) · `5541520` task 9 fixes (game memory area base, chunks.txt workaround, `BB_FILE_TRACE`) · `33f00cb` upscalers (task 6) + BDA assert diagnostics · `fd5ebef`, `4f2433e` gs/fs scan tool · M3 task 9 result · this Result.

### Acceptance
| Item | Result |
|---|---|
| `ROSETTA_ADVERTISE_AVX=1 bash build.sh --test` passes all tests | **PASS** (exit 0, all 11 programs; run after the last code change) |
| `grep -rn "MACOS-STUB" src gpu` returns nothing | **PASS** |
| `SDL_IsMainThread()` true for window creation | **PASS**, log line below |
| U1 reaches its main menu, rendered on the Apple GPU in a Cocoa window, screenshot in `out/m4-menu.png` | **FAIL / BLOCKED**: no frame, no screenshot (`out/m4-menu.png` does not exist). The Cocoa window and the Metal-backed swapchain presenter were created |
| Result lists every Vulkan feature/extension the renderer wanted that MoltenVK lacks, and what was done | PASS (tables below). One of them (sparse binding) could not be worked around |

### Task 1: Cocoa main thread
Log (every run): `WindowSDL: SDL_IsMainThread() = 1, pthread_main_np() = 1`.
Design, as built:
- `src/probe.c` (`__APPLE__` only): `main()` stores `argc/argv`, starts the pthread **"bb:main"** (64 MiB stack; spec asked >= 16 MiB) that runs the old body, now `probe_main()`, and calls `exit(probe_main(...))` (so `exit()` from a non-main thread as before). The real main thread runs the main run loop.
- **Deviation from the spec: not `dispatch_main()`.** Measured: with `dispatch_main()` the window block ran with `pthread_main_np() == 0` (libdispatch ends the main thread with `pthread_exit`, a worker drains the main queue) and Cocoa refused: first `SDL ... cocoa not available` (SDL's own idea of the "main thread" was whichever thread init'ed first), then, after calling `SDL_InitSubSystem` on the true main thread, `NSWindow should only be instantiated on the main thread!`. Fix: the main thread adds a far-future `CFRunLoopTimer` and runs `CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1e9, false)` in a `for(;;)` loop; the GCD main queue is drained by the main CFRunLoop on the real main thread. `build.sh` links `-framework CoreFoundation` (Darwin libraries). The run loop **returns once by itself** (reason 2 = `kCFRunLoopRunStopped`, something in AppKit/SDL/MoltenVK stops it after the window exists; `BB_RUNLOOP_LOG=1` prints it): without the `for(;;)` the process `exit(0)`ed silently after ~0 s. Kept as a loop.
- `gpu/shim/bbgpu.cpp` (`__APPLE__` only): `dispatch_async_f(dispatch_get_main_queue(), ...)` creates `WindowSDL`, signals `g_window_ready` (the condition-variable wait in `bbgpu_init` is unchanged), then **re-posts itself every 2 ms with `dispatch_after_f`** instead of looping `PollEvents()+SDL_Delay(2)` (so other work routed to the main queue is not starved). Closing the window still calls `Vulkan::FlushPipelineCacheForExit()` + `_Exit(0)` from the main queue.
- `gpu/shim/window.cpp`: logs `SDL_IsMainThread()` once.

SDL video/window calls in `gpu/shim` / `src` and where they run (grep `SDL_`):
| Call | Where | Thread |
|---|---|---|
| `SDL_InitSubSystem(VIDEO\|GAMEPAD)`, `SDL_CreateWindowWithProperties`, `SDL_Metal_CreateView/GetLayer`, `SDL_SetWindowIcon`, `SDL_GetWindowSizeInPixels`, `SDL_DestroyWindow` | `window.cpp` `WindowSDL` | main (constructor and destructor on the main queue) |
| `SDL_PollEvent`, `SDL_GetKeyboardState`, `SDL_SetWindowRelativeMouseMode`, `SDL_StartTextInput/StopTextInput`, `SDL_SetWindowTitle` (text entry title), `BbOverlay::UpdateTextInput/HandleEvent` | `WindowSDL::PollEvents`, `UpdateTextTitle` | main (all inside the 2 ms tick); `BeginTextInput` from a guest thread only sets a flag under `text_mutex`, the SDL call happens in `PollEvents` |
| `SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay())` | `bbgpu.cpp BbDisplayRefreshHz` | whichever thread asks first (the presenter); reads cached display data, no Cocoa call; it logs `CurrentVBLDelta ... ignoring unreasonable value` from CoreVideo once |
| `SDL_GetScancodeFromName`, `SDL_GetKeyFromScancode`, `SDL_GetKeyName` | `button_prompts.cpp` | any (string tables only) |
| `SDL_InitSubSystem(GAMEPAD)`, `SDL_GetGamepads`, `SDL_OpenGamepad`, rumble | `src/runtime_pad.c` | guest/pad threads. Not routed to the main queue: no window/video use; events for it are pumped by the main thread. Risk: SDL gamepad on macOS from a non-main thread, **unverified** (no pad attached) |
| `SDL_InitSubSystem(AUDIO)`, audio streams | `src/runtime_audio.c`, `trophy_ui.cpp` | guest/audio threads (CoreAudio), not window related |
Nothing was found that needed routing besides the window loop. There is no resize / fullscreen toggle / cursor / clipboard / message-box call outside `PollEvents` and the constructor.

Everything that assumed "main thread" (all checked; none needed a change besides those above):
- `probe.c`: `dladdr((void*)main)` (exe_base for stack dumps) still valid. `check_cpu`, `runtime_memory_reserve`, `sigaction` (process-wide), `alarm()` and the loader now run on bb:main; the guest "main" thread = bb:main (its pthread TSD slot holds the TCB; `tls-darwin-test` and `signals-darwin-test` pass, they exercise the real `fault()`/`watchdog()`).
- Signals: `SIGSEGV/SIGBUS/SIGILL/SIGTRAP` go to the faulting thread, so the CF main thread is irrelevant. `SIGALRM` (watchdog) and `SIGUSR2` (thread dump) can be delivered to any thread, including the CF main thread: the watchdog enumerates every Mach thread except the current one (`task_threads` + `pthread_from_mach_thread_np`), which now includes the main thread (a stack line with no guest frames); nothing assumed a guest thread. No `sigaltstack` in `probe.c` (same as Linux). No `pthread_main_np`, `pthread_sigmask` changes or signal masks outside what `fault()` already does.
- `exit()` runs atexit handlers on bb:main while the main thread sits in the run loop; many paths use `_exit` already (watchdog, `fail()` uses `exit(1)`). No static destructor of the main thread is involved.
- Thread names: `bb:main` (pthread_setname_np on the thread itself), `bb:window` no longer exists on Apple.
- stdout is unbuffered process-wide (`setvbuf` at the start of `probe_main`).

### Task 2: window
`window.cpp` cocoa branch: window created with `SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN` (+ `HIGH_PIXEL_DENSITY`; the `VULKAN` property is **not** set on Apple, SDL would try to load a Vulkan loader itself), then `SDL_Metal_CreateView` + `SDL_Metal_GetLayer` -> `WindowSystemType::Metal`, `render_surface = CAMetalLayer*`, `render_surface_scale = pixel density`. `vk_platform.cpp` already had `VK_USE_PLATFORM_METAL_EXT` defined for Apple (checked in the translation unit; `createMetalSurfaceEXT` compiled and ran). Result: `GPU: window and Vulkan presenter ready; SDK 0x02508051, 1738 HLE symbols`. `MACOS-STUB(M4)` removed.

### Task 3: instance / device portability
What I used: **the Khronos portability rules (VK_KHR_portability_enumeration / VK_KHR_portability_subset), the same approach as `src/vulkan_smoke.c` from M3. shadPS4 upstream `main` (fetched 2026-11-02) has NO portability handling in `vk_instance.cpp`/`vk_platform.cpp` (only `VK_DRIVER_FILES` pointing to the bundle), so there was nothing to mirror.**
- `vk_platform.cpp GetInstanceExtensions`: adds `VK_KHR_portability_enumeration` on Apple; `CreateInstance`: `InstanceCreateFlagBits::eEnumeratePortabilityKHR`.
- `vk_platform.cpp CreateInstance`: upstream sets `VK_DRIVER_FILES` to the executable's folder unconditionally; here it is set only when no `VK_DRIVER_FILES`/`VK_ICD_FILENAMES` is given **and** `MoltenVK_icd.json` exists next to the executable (otherwise `out/` would hide MoltenVK).
- `vk_instance.cpp CreateDevice`: `add_extension(VK_KHR_portability_subset)` on Apple.
- `vk_instance.cpp CreateDevice`: the three hard `ASSERT`s on `robustBufferAccess2`, `robustImageAccess2`, `nullDescriptor` became a warning on Apple and the device is created with the features the driver reports (see gap table).
- Loader behaviour worth knowing: with `VULKAN_SDK` set the loader also scans `$VULKAN_SDK/share/vulkan/icd.d` even when `VK_ICD_FILENAMES` names only MoltenVK, so **two physical devices appear** (below). The renderer's device sort has no tie-break between them; in all runs here it picked MoltenVK (`driver 0.2.2210 (id 14)`, `Apple M4 Max`). `BB_GPU_ID=<index>` forces one.

MoltenVK / vulkaninfo excerpt (native arm64 `vulkaninfo` from the SDK, same GPU; `arch -x86_64 vulkaninfo` cannot find the loader because `arch` strips `DYLD_*`; the x86_64 `bb-probe --vulkan-only` and the renderer log below confirm the same device under Rosetta):
```
Vulkan Instance Version: 1.4.363          Instance ext: VK_KHR_portability_enumeration, VK_EXT_metal_surface, VK_EXT_swapchain_colorspace, ...
GPU0 (MoltenVK):   apiVersion 1.4.357, driverID DRIVER_ID_MOLTENVK, driverInfo 1.4.2, deviceName Apple M4 Max, 130 device extensions
                   subgroupSize 32, minSubgroupSize 4, maxSubgroupSize 32, subgroupSizeControl true
                   maxBufferSize / maxMemoryAllocationSize 0x7051ec000 (~28 GiB), maxStorageBufferRange 4294967295, maxComputeSharedMemorySize 32768
GPU1 (KosmicKrisp): apiVersion 1.4.363, driverID DRIVER_ID_MESA_KOSMICKRISP, 145 device extensions, subgroup 32/32
[mvk-info] MoltenVK version 1.4.2, supporting Vulkan version 1.4.357.  GPU memory available: 38338 MB.  Metal Shading Language 4.0, GPU Family Apple 9 / Metal 4
[mvk-info] Vulkan semaphores using Metal implicit guarantees within a single queue.   Descriptor sets binding resources using Metal3 argument buffers.
```

### Task 4: feature gates (MoltenVK 1.4.2 on the M4 Max)
| Gate / feature | Value | Used for / consequence |
|---|---|---|
| `IsSubgroupSize64Supported()` = `subgroupSizeControl && maxSubgroupSize >= 64` | **false** (true && 32 >= 64) | users: `vk_compute_pipeline.cpp:28` and `vk_graphics_pipeline.cpp:249` only attach `RequiredSubgroupSize=64` when true -> not attached on Apple (wave32). |
| `LowerWave64BallotPass` (`recompiler.cpp:181`) | **taken**: returns early only when `profile.subgroup_size == 64`; `profile.subgroup_size` = `vk11_props.subgroupSize` = 32 (log: `Physical device subgroup size 32` at Info level). `shared_memory_barrier_pass.cpp:374/406` also branch on `subgroup_size != GcnSubgroupSize` / `< GcnSubgroupSize` (wave split). Not exercised by a real shader (no frame) | |
| "NVIDIA crash fix" 64-wide compute special case | the only 64-wide compute code found is the above `RequiredSubgroupSize` (no wave32 *special* path needed: pipelines just lack the attribute) and host shaders with `local_size_x = 64` (`fsr.comp`, `buffer_multi_copy.comp`, `fault_buffer_process.comp`, `tiling.comp`), which are workgroup-size, not subgroup-size, dependent. `ffx_a.h` mentions subgroup; not checked in depth | |
| `shaderFloat16` / `shaderInt8` | true / true | |
| `shaderInt16`, `shaderInt64` | true / true | |
| `storageBuffer16BitAccess`, `storageBuffer8BitAccess` | true / true | |
| `shaderFloat64` | **false** | |
| `shaderBufferInt64Atomics`, `shaderSharedInt64Atomics` | **false** / false | |
| `shaderImageFloat32Atomics` | **false**; `shaderBufferFloat32Atomics/AtomicAdd` true | |
| `timelineSemaphore`, `dynamicRendering`, `synchronization2`, `maintenance4`, `scalarBlockLayout`, `runtimeDescriptorArray`, `shaderDemoteToHelperInvocation`, `bufferDeviceAddress`, `samplerMirrorClampToEdge` | all true | |
| `drawIndirectCount` | false | |
| `geometryShader`, `logicOp`, `depthBounds`, `wideLines`, `shaderCullDistance` | false | |
| `extendedDynamicState3`: ColorBlendEnable/Equation/WriteMask, RasterizationSamples, SampleMask, AlphaToCoverage, LogicOpEnable, ProvokingVertexMode, ... | false (DepthClampEnable, PolygonMode, DepthClipEnable true) | the renderer's `dynamic_state_3_features` log prints `extendedDynamicState3ColorWriteMask: false` at Info |
| `sparseBinding`, `sparseResidencyBuffer`, `sparseResidencyImage2D/3D`, `sparseResidencyAliased` | **all false** (also on KosmicKrisp) | **see Blocked** |
| `robustBufferAccess2`, `nullDescriptor` | **false** (`robustImageAccess2` true) | see gap table |
Some formats are "not fully supported" (logged as warnings): `A2B10G10R10 Snorm/Uint/Sint`, `R32G32B32 Uint/Sint/Sfloat`, `R5G6B5`, `A1R5G5B5`, `R5G5B5A1`, `B4G4R4A4`, `D16UnormS8Uint` (no depth/stencil attachment). Present mode `Mailbox` falls back to `Fifo`.

### Vulkan features / extensions the renderer asked for and MoltenVK does not provide
| Wanted | MoltenVK 1.4.2 | Handled |
|---|---|---|
| `sparseBinding` + `sparseResidencyBuffer`, `vkQueueBindSparse` (BufferCache arenas, `buffer_cache.cpp` 1364, 1382, 1553, 1819-1857; `buffer.cpp:203`) | missing (also KosmicKrisp) | **NOT handled: blocker** |
| `robustBufferAccess2`, `nullDescriptor` (VK_EXT_robustness2; asserted upstream, enabled in the device chain) | present extension, features false | **ignored**: asserts relaxed to a warning, features requested only if reported. Null-descriptor uses in the renderer were **not audited**; a null buffer/image descriptor would be an error here |
| VK_EXT_attachment_feedback_loop_layout / _dynamic_state, VK_EXT_depth_range_unrestricted, VK_EXT_custom_border_color, VK_EXT_depth_clip_enable, VK_EXT_vertex_input_dynamic_state, VK_EXT_primitive_topology_list_restart, VK_AMD_shader_explicit_vertex_parameter, VK_EXT_provoking_vertex, VK_AMD_gcn_shader, VK_NV_framebuffer_mixed_samples, VK_AMD_mixed_attachment_samples, VK_EXT_shader_atomic_float2, VK_KHR_workgroup_memory_explicit_layout, VK_EXT_image_view_min_lod, VK_KHR_compute_shader_derivatives, VK_KHR_shader_clock, VK_EXT_device_fault | unavailable (device log `Extension ... unavailable`, 17 lines) | all were already optional in `CreateDevice` (flag false -> the existing fallback path); not exercised. `vertex_input_dynamic_state` is requested unconditionally in the device `StructureChain` even when the extension is absent; device creation still succeeded |
| `subgroup size 64` (`RequiredSubgroupSize`) | max 32 | lowered (see Task 4) |
| VK_EXT_shader_stencil_export, VK_EXT_image_2d_view_of_3d, VK_EXT_extended_dynamic_state3 (partly), VK_KHR_maintenance5, VK_EXT_memory_budget, push descriptors, vertex attribute divisor | present | used |
| FSR 4 shader features (INT8/FP8 dot, compute-shader derivatives, mixed float dot product) | missing | upscaler forced off (Task 6) |

### Task 5: host shaders
All 33 `glslang --target-env vulkan1.3` host shader modules (every header in `out/gpu/include/video_core/host_shaders`: buffer_multi_copy, camera_motion(+debug), color_to_ms_depth, depth_resample, depth_stencil_bits/resample, fault_buffer_process, frame_gen_inputs/ui, fs_tri, fsr_easu/rcas, ms_image_blit(+msaa), post_process, taa(+sharpen, +ldr), tiling macro/micro 8..128, upscale_merge/reactive) were converted SPIR-V -> MSL with the SDK's `MoltenVKShaderConverter`: **33 OK, 0 FAIL**. That checks SPIRV-Cross acceptance only: the Metal Toolchain is not installed (`xcodebuild -downloadComponent MetalToolchain`), so MSL was not compiled, and **no host-shader pipeline was created by MoltenVK** (the run stops before any frame; the only pipeline created: `Pipeline cache: preloaded 1 of 1 pipelines`). Failed shaders at pipeline creation: unknown.

### Task 6: upscalers
- `gpu/shim/bbport_settings.{h,cpp}` (`__APPLE__` only): `DefaultUpscaler = UpscalerOff` (was FSR 3.1; FSR 3.1 compiles and the log says `FSR 3.1 has not been checked on this driver`); `RefuseFsr4()`: a configuration/ini/`BB_UPSCALER` value of `fsr4`/`fsr411` is refused with `Upscaler: fsr4 is disabled on macOS (M4); using off`; `ConfigureUpscalerSupport()` forces `fsr4_supported = fsr411_supported = false`, so the overlay (`bbport_overlay.cpp:196`) does not offer them either. Verified: `BB_UPSCALER=fsr4` -> that line, default run -> `Upscaler: FSR 3.1 available (off)`.
- DLSS / XeSS are `_WIN32`-only (unchanged). TAA is selectable (`BB_UPSCALER=taa`, "live path" needs a restart in `run.sh`); not tried.

### Task 7: run to the main menu (BLOCKED)
Timing: launch -> assertion **8-9 s** (first and second run: no shader cache involved yet; `user/cache/.../profile.bin` missing on the first run is logged as an error and harmless). No menu timing exists.
First frame never happened. bb log up to the stop (`user/last_run.log`, see also the M3 Result):
```
WindowSDL: SDL_IsMainThread() = 1, pthread_main_np() = 1
GPU [Render.Vulkan] <Warning> vk_instance.cpp:294 CreateDevice: robustness2: robustBufferAccess2=false robustImageAccess2=true nullDescriptor=false
GPU [Render] <Warning> vk_swapchain.cpp:219 FindPresentMode: Requested present mode Mailbox is not supported, falling back to Fifo.
GPU: window and Vulkan presenter ready; SDK 0x02508051, 1738 HLE symbols
Guest TCB loads redirected to the pthread TSD slot gs:[2264]: 1953
Mapped 21349372 bytes, 9 segments; applied 21330 relocations
Entering original x86-64 code at guest offset 0x350d70
...
GPU [Debug] <Critical> buffer.cpp:214 Create: Assertion Failed!
Failed to get buffer device address (size 0x100000000, type 4, flags { SparseBinding | SparseResidency })
```
MoltenVK log (`MVK_CONFIG_LOG_LEVEL=2`): only the instance/device banner shown above and `Vulkan semaphores using Metal implicit guarantees...`; no MoltenVK error or warning before the assertion.

### Blocked: the buffer cache needs sparse residency buffers
`BufferCache` (`buffer_cache.cpp:1340-1600`, `GetArena`, `AllocateArenaMemory`, `BindsForArena`, `bindSparse` at 1857) models the PS4 GPU address space as 4 GiB (`ARENA_PAGE_BITS` 32, 40-bit space) `SparseBinding|SparseResidency` buffers whose blocks are bound to device-memory chunks as guest ranges become resident; shaders use the arena's buffer device address plus a BDA page table (`bda_pagetable_buffer`). The first arena creation (`Buffer::Create` -> `createBuffer` with the sparse flags) returns a buffer whose `getBufferAddress` is 0 (observed); `vkQueueBindSparse` would be the next call and the device reports no sparse support. This is a driver capability, not a bug (SDK 1.4.363 / MoltenVK 1.4.2; KosmicKrisp has the same `sparseBinding = false`). I did not check how upstream shadPS4's buffer cache avoids it. I did not try to design around it in this session. Options, in my order of preference:
1. **Non-sparse arenas** (spike): make an arena an ordinary `VkBuffer` of its full size on a host-visible+device-local memory type (unified memory), bind its memory once, treat the bind list as a no-op and keep the resident-range bookkeeping for `CopySparseMemory`. Cost: the memory behind each arena is committed up-front (U1's memory areas span 0x7000000000..0x7214000000 = ~8.3 GiB of guest address space, i.e. up to three 4 GiB arena pages; whether Metal commits the pages lazily is not known); `maxBufferSize` is 28 GiB so a 4 GiB arena is allowed. Touches `buffer.cpp` `UniqueBuffer::Create`, `buffer_cache.cpp` (arena creation, `BindsForArena`, the `bindSparse` submit at ~1857, residency / host fallback) and the `GetSparsePageShift()` user in `vk_rasterizer.cpp`/pipeline cache (I only grepped these sites, did not read them).
2. Address translation in the shader recompiler through the existing BDA page table instead of a flat arena (larger: every guest pointer access).
3. Port the fork's buffer cache design to a non-sparse variant from upstream (largest; loses the fork's batching).
Whichever is chosen, expect further MoltenVK-only failures behind this one (null descriptors, `vertexInputDynamicState`, MSAA/format gaps, pipeline creation of the 33 host shaders and of the recompiled game shaders, the `Float32 denorm flushing is not supported by the GPU` warning of `emit_spirv.cpp:460` already seen at startup).

### Every Darwin substitution made in M4
- `src/probe.c`: pthread "bb:main" + CFRunLoop main thread; `patch_memory_area_base()`; `patch_missing_chunks_txt()`; (`dispatch_main()` replaced, see Task 1). `build.sh`: `-framework CoreFoundation`.
- `gpu/shim/bbgpu.cpp`: window on the main queue, self-reposting 2 ms tick. `gpu/shim/window.cpp`: Metal window + view + layer, `SDL_IsMainThread()` log; `SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN` not set on Apple.
- `gpu/shadps4/.../vk_platform.cpp`: portability enumeration, conditional `VK_DRIVER_FILES`. `vk_instance.cpp`: `VK_KHR_portability_subset`, robustness2 gaps tolerated. `buffer.cpp`: the BDA assertion message now prints size/type/flags (all platforms).
- `gpu/shim/bbport_settings.{h,cpp}`: Apple default upscaler off, FSR 4 refused.
- `src/runtime_file.c`: `BB_FILE_TRACE=1` (all platforms; prints each successful guest `open`).
- `tools/scan_gs_fs.{c,py}`: new analysis tool (not part of the build).

### Open issues
1. **Blocker above (sparse residency).**
2. **`/app0/shareddata/chunks.txt` is missing from the dump** and the game's chunk-set assertion is bypassed (M3 Result, Task 9). Owner: is the dump complete? A real file would make the workaround unnecessary.
3. The game-memory-area cursor patch is signature based and was only exercised on U1; U2/U3 untested.
4. psarc archives were not extracted; many optional `effect1/*.bin`, `bpdata1/particle.json` etc. report `not found`. Unknown whether Linux logs the same.
5. Two Vulkan devices (MoltenVK + KosmicKrisp) are visible whenever `VULKAN_SDK` is set; selection depends on `std::sort` of equal keys. M6 must decide what is packaged and pin the choice (`BB_GPU_ID` exists).
6. The main CFRunLoop returns by itself once (reason 2) after the window exists; cause not identified (looped around).
7. `run.sh` does not set `VULKAN_SDK`; `libvulkan.1.dylib` is found through the rpath baked in by `build.sh`, MoltenVK through the SDK scan described above. `nullDescriptor`-style failures will need an audit once rendering starts.
8. `user/last_run.log` is not written by `run.sh` (I saved it by hand); tell me if you want `run.sh` to `tee` it.
9. Linux/Windows were not compiled: shared-file edits are `buffer.cpp` (assert text), `runtime_file.c` (env diagnostic), `bbport_settings.h` (`DefaultUpscaler` constant, `UpscalerFsr3` elsewhere), everything else is `#ifdef __APPLE__`.

### Needs a decision from the owner
- Sparse buffers: approve a spike on option 1 (non-sparse arenas)? It is the only way to continue M4; expect days, not hours.
- chunks.txt: is the dump missing `shareddata/`? Keep the workaround?
- Which Vulkan driver to target if option 1 works: MoltenVK (as planned) or KosmicKrisp (has `robustBufferAccess2` + `nullDescriptor` true, 1.4.363).

### Review (Opus 5.5, 2026-11-02)

Tasks 1–3, 6 accepted; task 9 (M3) accepted. M4 stays open. Good diagnosis; the main-thread deviation
(CFRunLoop instead of `dispatch_main()`) is correct — `dispatch_main()` does `pthread_exit` on the main thread.

Decisions:
1. **Sparse buffers: spike approved, in this order** (M4 continuation, "M4b" below):
   a. First (short): read how **upstream shadPS4** (which runs on macOS/MoltenVK today) structures its
      buffer cache without sparse residency, and whether this fork's arenas are a bbport addition. If upstream
      has a non-sparse path that maps cleanly, prefer it (option 3 may be smaller than estimated).
   b. Otherwise option 1 (non-sparse arenas), behind a capability check (`sparseBinding == false`), so
      Linux/Windows keep the sparse path byte-for-byte.
   c. Measure what option 1 costs: process footprint and wired memory (`footprint -p`, `vmmap --summary`)
      with 1, 2, 3 arenas live. If Metal wires the full 4 GiB per arena, try smaller arena pages
      (`arena_page_bits` already adapts to the driver limit) before anything else.
2. **Driver: develop against KosmicKrisp first, keep MoltenVK working.** The renderer upstream asserts on
   `robustBufferAccess2`/`nullDescriptor`; KosmicKrisp provides them, so null-descriptor bugs won't hide
   behind relaxed asserts. Pin with `BB_GPU_ID` (no reliance on `std::sort` order). Once a frame renders,
   run both and pick by correctness, then performance. Packaging choice stays in M6.
3. **chunks.txt: the dump is incomplete.** The game itself opens `/app0/shareddata/chunks.txt`, so
   `shareddata/` is part of the package; the owner re-checks the dump. Keep the workaround only as a
   fallback when the file is absent, with a one-line `MACOS: chunks.txt missing, treating all chunks as
   installed` warning. GAME_FILES.md updated.
4. `run.sh`: yes, `tee` the probe output to `user/last_run.log` (Darwin path at least).
5. psarc extraction: not now; revisit only if a missing-file path blocks the menu.

## M4b (continuation: non-sparse buffer arenas, run to the menu)

Started 2026-11-03, branch `macos`, same machine and toolchain as M4. Step 0 preconditions all held (clean tree except `m gpu/third_party/fsr-vulkan`; `build.sh --test` green; the M4 stop reproduced; `shareddata/chunks.txt` present in the dump).

### Upstream investigation (shadPS4 `main`, fetched 2026-11-03 with a summarising fetch tool, so treat line-level claims as second-hand)
- **Does upstream use sparse residency?** Yes. `src/video_core/buffer_cache/` (buffer.{h,cpp}, buffer_cache.{h,cpp}, fault_manager.{h,cpp}, memory_tracker.h, range_set.h, region_*.h) has the same design as this fork: `MemoryType::Sparse` buffers created with `eSparseBinding|eSparseResidency` and no VMA allocation, `arenas`, `address_space[]`, `resident_ranges`, `EnsureResident` building `vk::SparseMemoryBind`s, `SubmitPendingArenaBinds` calling `bindSparse`, and a `bda_pagetable_buffer` that `EnsureResident` fills with block device addresses. So **the arenas are not a bbport addition.**
- **Is it optional?** No. Upstream `vk_instance.cpp` only copies `sparseBinding`/`sparseResidencyBuffer` into the device feature request; there is no assert, no capability check and no non-sparse path in the buffer cache. It has no `__APPLE__`/MoltenVK code in either file. I did not find how (or whether) current upstream `main` runs on MoltenVK; a web search found nothing, and Metal cannot place sparse tiles in a buffer (MoltenVK issue 1700), so I expect it does not use this buffer cache path on macOS.
- **What is a bbport addition** (comments in this fork): `arena_page_bits` shrinking to the driver's `maxBufferSize` (upstream uses a constant `ARENA_PAGE_BITS`), `AllocateArenaMemory` chunking and the system-memory fallback, the side queue, landing gate, diagnostics.

### Decision
Upstream has no non-sparse structure that could be adopted, so option 3 (port upstream's) is moot. **Option 1 it is: non-sparse arenas.**
- Capability switch: `Instance::IsSparseBufferSupported()` = `features.sparseBinding && features.sparseResidencyBuffer` and `BB_NO_SPARSE` not `1`. Everything non-sparse hangs off it; the sparse path keeps its code and behaviour.
- Non-sparse arena: an ordinary `VkBuffer` (no sparse flags) of the full arena size, one dedicated allocation (VMA, `AUTO_PREFER_DEVICE`; on Apple silicon this is the unified host-visible + device-local type), memory bound once at creation. `EnsureResident` keeps its resident-range bookkeeping and BDA page-table writes (the shaders still see per-block addresses), but allocates nothing and records no sparse bind; the `pending_binds` list stays empty so `SubmitPendingArenaBinds` is a no-op. `CopySparseMemory` is untouched (it is the guest-side copy, not a Vulkan call).
- Arena migration (`GetArena`, a request spanning two VA arena pages that belong to different arenas) cannot rebind memory any more: the merged arena is a new buffer, so resident blocks are copied old arena -> new arena on the GPU and their BDA page-table entries are rewritten.

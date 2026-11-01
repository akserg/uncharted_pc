# M4 — Cocoa window, MoltenVK, first frame

## Goal
U1 main menu renders in an SDL3 Cocoa window through MoltenVK.

## Preconditions
- M3 done. LunarG Vulkan SDK (universal) installed; `$VULKAN_SDK/lib/libMoltenVK.dylib` lists x86_64.
- `VK_ICD_FILENAMES=$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json` (or the loader finds it).
  `vulkaninfo` (x86_64) lists the Apple GPU.

## Tasks
1. **Window** (`gpu/shim/window.cpp`): finish the `cocoa` branch from M1. Use
   `SDL_Metal_CreateView(window)` + `SDL_Metal_GetLayer(view)`; set `window_info.type = Metal`,
   `render_surface = CAMetalLayer*`. `vk_platform.cpp` already creates `vk::MetalSurfaceCreateInfoEXT`
   from that (`VK_USE_PLATFORM_METAL_EXT` is defined at the top of the file — check it is defined for
   this build, not only in shadPS4's original CMake).
2. **Instance**: enable `VK_KHR_portability_enumeration` + `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`,
   and `VK_KHR_portability_subset` on the device. Mirror whatever shadPS4's `vk_instance.cpp` does under
   `#ifdef __APPLE__`; this tree is a fork of it so the diff should apply almost verbatim.
3. **Feature gates** (`vk_instance.h`): `subgroupSizeControl && maxSubgroupSize >= 64` is false on Apple
   (wave32). Find every use of that predicate and the `shaderFloat16`/`shaderInt8`/`shaderInt16` gates;
   confirm `lower_wave64_pass` is taken when subgroup 64 is unavailable, and that the "NVIDIA crash fix"
   64-wide compute special case has a wave32 path. Record which gates are false on the Apple GPU.
4. **Host shaders**: `glslang --target-env vulkan1.3` output must be accepted by MoltenVK. Any that use
   unsupported features will fail at pipeline creation — list them.
5. **Upscalers**: DLSS/XeSS are `_WIN32`-only; FSR 4 is still compiled on Apple (M1 decision) — force it off at run time on `__APPLE__` (settings default + refuse selection with a log line) and that config defaults on macOS select
   TAA or "off". FSR 3.1 compiles but is not required to work in M4.
6. **Run** to the main menu. Capture `user/last_run.log` and MoltenVK's own log
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
_(fill in: date, commit, MoltenVK version, vulkaninfo excerpt (API version, subgroup size, the feature
list that matters), gates that are false, lowering decisions, screenshot path, log excerpts, open issues)_

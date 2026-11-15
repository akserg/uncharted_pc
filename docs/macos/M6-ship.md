# M6 — Ship

## Goal
A zip that a macOS 15.4+ user can unpack and run.

## Preconditions
- M5 done.

## Tasks
1. `run.sh`: export `ROSETTA_ADVERTISE_AVX=1` on Darwin (Rosetta reads it at launch). Make it work on Darwin (BSD `sed`/`readlink` differences, `arch -x86_64`
   not needed for a native x86_64 binary — Rosetta picks it up automatically).
2. Packaging: `packaging/macos/package.sh` producing `dist/uncharted-macos/` with `bb-probe`,
   `gpu/libbbgpu.dylib`, bundled `libMoltenVK.dylib`, `libSDL3.dylib`, `libvulkan.dylib` and the other
   `$BB_DEPS` dylibs (`otool -L` walk, `install_name_tool` to `@loader_path`), `licenses/`, `run.sh`.
   No DLSS/XeSS/FSR4 binaries.
3. `.app` bundle (optional for first release): `Info.plist`, `LSMinimumSystemVersion 15.4`,
   `LSArchitecturePriority x86_64`, `LSEnvironment` → `ROSETTA_ADVERTISE_AVX=1` (verify it takes effect; else a tiny launcher that re-execs `bb-probe` with it); entitlements `com.apple.security.cs.allow-jit`,
   `com.apple.security.cs.allow-unsigned-executable-memory`, `com.apple.security.cs.disable-library-validation`;
   `codesign --force --deep --sign - --entitlements ...`. Ad-hoc signing is enough for a GitHub release;
   note the Gatekeeper right-click-Open step in the README.
4. `docs/MACOS_BUILD.md` (user-facing, sibling of `LINUX_BUILD.md`): requirements, Vulkan SDK + `scripts/macos/build-deps.sh`, build,
   run, known problems, the Rosetta sunset note.
5. Test on a clean user account (or second Mac): unpack, run, reach the menu.
6. Rebase `macos` onto current `upstream/main`; open a draft PR to upstream with the milestone docs
   linked, or keep it on the fork if upstream doesn't want it — record the decision.

## Acceptance
- Clean-machine test passed.
- `docs/MACOS_BUILD.md` exists and is accurate.
- Release zip attached to a GitHub release on the fork.

## Result

### Tasks 1 and 4 (2026-11-15, branch `macos`)

| Task | State |
|---|---|
| 1. `run.sh` works from any new terminal | **done**, commit `a104ddc` |
| 2. Packaging (`package.sh`, dylib bundle) | not started |
| 3. `.app` bundle, signing | not started |
| 4. `docs/MACOS_BUILD.md` | **done**, commit in the docs commit that follows `a104ddc` (`git log -- docs/MACOS_BUILD.md`) |
| 5. Clean-machine test | not started |
| 6. Rebase onto upstream, PR decision | not started |

**Task 1.** The Darwin block of `run.sh` now defaults `VULKAN_SDK` (as `build.sh`; stops with a message if the folder
is missing), sets `VK_DRIVER_FILES` and `VK_ICD_FILENAMES` to `$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json`
(only when the caller set neither; stops if the file is missing), defaults `BB_GPU_ID=0`, and prints
`macOS: VULKAN_SDK=..., MoltenVK ICD found, BB_GPU_ID=0`. Not set, because not needed: `DYLD_LIBRARY_PATH`
(binaries carry an rpath to `$VULKAN_SDK/lib`), `VK_ADD_LAYER_PATH` (validation stays off). The Linux path is unchanged.
Note: `vk_platform.cpp` only sets `VK_DRIVER_FILES` itself when an ICD json sits next to the executable; run.sh's
setting takes precedence, so a packaged build (task 2) must not rely on run.sh's SDK default.

Verification (clean environment, `env -i HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin TERM`, `BB_GAME_DIR=.../CUSA02344
BB_EXE=u1 bash run.sh`). With `BB_TIMEOUT=40` the watchdog fired before the presenter line (GPU init was still
running, line 39 of `user/last_run.log`: `STOP: watchdog timeout`); with `BB_TIMEOUT=100` (exit 142 = the watchdog ending the run):
```
macOS: VULKAN_SDK=/Users/akserg/VulkanSDK/1.4.363.0/macOS, MoltenVK ICD found, BB_GPU_ID=0
GPU: device 0: Apple M4 Max, driverID Moltenvk, 1.4.2
GPU: window and Vulkan presenter ready; SDK 0x02508051, 1738 HLE symbols
Entering original x86-64 code at guest offset 0x350d70
Runtime: guest thread 'NdJobWorkerThread 0' created (stack=65536, prio=767)
```
No `ErrorIncompatibleDriver`. `GetInstanceLayers: Failed to query layer properties: Success` still appears (harmless
now). Gate: `CHECK PASS gate [1:40]`.

**Task 4.** `docs/MACOS_BUILD.md` written (sections: what this is and requirements, game files, one-time setup, build,
run, controls, controller stick fix, known limits, troubleshooting). Unverified: the exact names of the SDK
installer's components (the guide says only what to untick in general terms), and a from-scratch run of the guide on a
machine without the SDK/deps.

# Building and running on macOS (Apple Silicon)

## 1. What this is

The game is a PS4 program (x86-64 code). This port runs it inside an **x86_64 macOS build** of the loader
(`bb-probe` plus `libbbgpu.dylib`), which runs on Apple Silicon under **Rosetta 2**. Graphics go through
Vulkan on **MoltenVK** (Vulkan on top of Metal).

Requirements:

- Apple Silicon Mac (M1 or newer), **macOS 15.4 or newer** (Rosetta must translate AVX/AVX2, which it does from 15.4 on)
- **24 GB unified memory or more.** 16 GB is not supported (the game alone needs about 13 GB, see Known limits)
- Rosetta 2
- Xcode Command Line Tools (`xcode-select --install`)
- About 60 GB of free disk space for the game files

Rosetta: Apple supports it fully through macOS 27; after that it is limited to older, unmaintained software.
A native arm64 port would be a separate, later project.

Tested on a MacBook Pro M4 Max, 48 GB, macOS 26. Only Uncharted 1 has been played through (to chapter 7).

## 2. Game files

You need your own decrypted dump of the game, made from your own console and copy. It is never part of this
project. The folder layout the loader expects is in [macos/GAME_FILES.md](macos/GAME_FILES.md). The folder with
`eboot.bin` in it is what you pass as `BB_GAME_DIR` below.

## 3. One-time setup

**Rosetta 2:**

```bash
softwareupdate --install-rosetta --agree-to-license
```

**Vulkan SDK.** Install the LunarG Vulkan SDK **1.4.363.0 for macOS** from <https://vulkan.lunarg.com/sdk/home#mac>
into the default location, `~/VulkanSDK/1.4.363.0`. It provides the Vulkan loader, MoltenVK, glslang and
SPIRV tools (all universal binaries). In the installer:

- Leave **System Global Installation off.** `build.sh` and `run.sh` use the SDK from your home folder; a global
  install copies files into `/usr/local`, needs administrator rights, and is deliberately ignored by the build.
- Untick the optional components you do not need (the iOS parts, for example). The macOS runtime, MoltenVK and
  the command line tools are the parts used here.

You do **not** need to run the SDK's `setup-env.sh`: `run.sh` sets what the game needs by itself. If the SDK is
somewhere else, set `VULKAN_SDK=/path/to/VulkanSDK/<version>/macOS` for both `build.sh` and `run.sh`.

**Build tools** (arm64 Homebrew is fine; these only run on your Mac and are never linked into the game):

```bash
brew install cmake ninja pkg-config nasm
```

There is no x86_64 Homebrew: Homebrew dropped x86_64 installs on Apple Silicon. Libraries therefore do **not**
come from Homebrew. The build never looks in `/opt/homebrew` or `/usr/local`.

**Libraries.** This builds SDL3, fmt, xxhash, Zydis, FFmpeg, protobuf and miniupnpc as x86_64 into
`~/.local/uncharted-x86_64` (override with `BB_DEPS`). It takes a while and can be re-run safely (finished
libraries are skipped):

```bash
bash scripts/macos/build-deps.sh
```

## 4. Build

From the repository folder:

```bash
bash build.sh
```

Output goes to `out/` (`out/bb-probe`, `out/gpu/libbbgpu.dylib`). To build and run the tests:

```bash
ROSETTA_ADVERTISE_AVX=1 bash build.sh --test
```

(`ROSETTA_ADVERTISE_AVX=1` makes Rosetta report AVX to the test programs; see Troubleshooting.)

## 5. Run

```bash
BB_GAME_DIR=/path/to/CUSA02344 BB_EXE=u1 ./run.sh
```

`BB_GAME_DIR` is the folder with `eboot.bin`. `BB_EXE` is `u1`, `u2` or `u3` (default `u1`). The first start
prepares the game files and compiles shaders, so it is slow; the menus take about 75 s to reach the START screen.
Stop with Ctrl+C in the terminal or by closing the window.

On macOS `run.sh` sets these itself, and each can be overridden by putting your own value in front of the command:

| Variable | Default | Why |
|---|---|---|
| `VULKAN_SDK` | `~/VulkanSDK/1.4.363.0/macOS` | the Vulkan loader library; `run.sh` stops with a message if the folder is missing |
| `VK_DRIVER_FILES`, `VK_ICD_FILENAMES` | `$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json` | tells the loader where MoltenVK is (without it: `ErrorIncompatibleDriver`). Set only if you set neither yourself |
| `BB_GPU_ID` | `0` | MoltenVK; the SDK's other driver (KosmicKrisp) is arm64-only and cannot load under Rosetta |
| `ROSETTA_ADVERTISE_AVX` | `1` | without it Rosetta hides AVX and `bb-probe` stops |

It prints one line at the start, for example
`macOS: VULKAN_SDK=/Users/you/VulkanSDK/1.4.363.0/macOS, MoltenVK ICD found, BB_GPU_ID=0`.

Other variables (`BB_USER_DIR`, `BB_TIMEOUT`, `BB_CONTENT_SKU`, ...) work as in [LINUX_BUILD.md](LINUX_BUILD.md).

**Logs and saves.** Everything the game prints is in `user/last_run.log` (each `run.sh` start overwrites it).
Save data is in `user/savedata`. Both are under `BB_USER_DIR` (default `user/` in the repository folder).
Add `BB_PERF_LOG=1` for a `PERF:` line (fps, frame times, memory) every 5 s.

## 6. Controls

| | Keyboard + mouse | Controller (Xbox, DualSense, DualShock 4) |
|---|---|---|
| Move / camera | W A S D / mouse (I J K L also turn) | sticks |
| Jump / confirm (Cross) | Space | Cross / A |
| Circle | C | Circle / B |
| Square / Triangle | F / E | Square / X, Triangle / Y |
| Aim / fire | right mouse / left mouse | L2 / R2 |
| L1 / R1 | Q / G | L1 / R1 |
| L3 / R3 | Left Shift / V | stick clicks |
| Pause menu (Options) | Esc or Return | Options / Start |
| D-pad | arrow keys, mouse wheel | d-pad |
| Settings overlay | F10 (fn+F10 on a MacBook) or Insert | L3 + R3 together |
| Free the mouse pointer | hold Left Option (Alt) | |
| Re-centre the sticks | F9 (fn+F9 on a MacBook) | LB + RB + right-stick click, held 1 s |

- **Function keys on a MacBook:** a bare F9/F10 is a media key the game never sees. Hold fn, or enable System
  Settings → Keyboard → "Use F1, F2, etc. keys as standard function keys".
- **Mouse capture:** the pointer is not captured at launch. Your first left/right click in the game window (that click
  does not fire or aim) or first W/A/S/D press captures it. Hold Left Option to free it; it is also released when
  the window loses focus, and the next click captures it again. `BB_MOUSE_CAPTURE_AT_LAUNCH=1` captures at launch.
- **Focus:** the controller keeps working while another app is in front. Keyboard and mouse only work while the
  game window is in front (macOS delivers them to the frontmost app only).

## 7. Controller stick fix

Some controllers have a stick that does not rest at its centre (a worn or faulty stick), which makes the camera
creep. Every stick has a deadzone. On top of that, on macOS the **right stick** has a fix (`BB_STICK_FIX=right`, the
default): if a stick's raw value stays still outside its deadzone for 3 s, the port treats that spot as the stick's
centre ("stuck" detection). It remembers up to four such rest points per controller and snaps to one within 250 ms
when the stick settles there, and it adds a dead corridor between the true centre and the fault so the camera does
not drift while the stick slides back. Full deflection still reaches the full range.

The learned points are saved per controller in `user/pad_rest_points.txt` and loaded at the next start; delete
that file to forget them. F9 or the LB + RB + right-stick-click chord re-centres by hand. `BB_STICK_FIX=left`,
`both` select other sticks; **`BB_STICK_FIX=none` turns the fix off** (the deadzone stays). Healthy controllers
are not affected: with a healthy stick nothing is learned. `BB_PAD_LOG=1` prints the raw and corrected stick values
once a second in `user/last_run.log`.

## 8. Known limits

- **Hitches:** about 1–2 % of frames take 33 ms instead of 16.7 ms (otherwise a steady 60 fps was measured in
  gameplay on an M4 Max). The cause is MoltenVK's synchronous encoding of heavy frames; no fix is known.
- **Games:** Uncharted 1 is the only game tested through chapter 7. Uncharted 2 and 3 are untested.
- **Upscalers:** FSR 4, DLSS and XeSS are not available. FSR 3 is not part of the tested setup.
- **Memory:** about 13 GB in gameplay (7 GB at the START menu), on top of macOS itself.
- **Rosetta:** see section 1.

## 9. Troubleshooting

- **`GetInstanceLayers: Failed to query layer properties` followed by `CreateInstance: ... ErrorIncompatibleDriver`:**
  the Vulkan loader found no driver. Cause: `VK_DRIVER_FILES` is not set and MoltenVK is not where the loader looks.
  `run.sh` sets it; if you start `bb-probe` yourself, set
  `VK_DRIVER_FILES=$HOME/VulkanSDK/1.4.363.0/macOS/share/vulkan/icd.d/MoltenVK_icd.json`. With a working driver
  the first line can still appear in the log (it is harmless).
- **"missing AVX" (the CPU check lists AVX as missing) / `bb-probe` stops at start:** Rosetta hides AVX unless `ROSETTA_ADVERTISE_AVX=1` is set in the
  environment of the process. `run.sh` sets it; set it yourself when you run `bb-probe` or tests by hand.
- **Controller does not react:** the controller works while another app is in front, but check that the right
  one is connected (`BB_PAD_LOG=1`, then `grep -E 'gamepad backend|PAD:' user/last_run.log`). On a wired Xbox
  controller (USB-C) the LED may keep blinking on macOS; that is normal. Keyboard and mouse need the game window
  in front.
- **Camera creeps by itself:** see section 7; press F9 (or the chord) with the sticks at rest, or raise
  `BB_STICK_DEADZONE_RIGHT` (default 8689, up to 32767).

More on the history and design of the port: [macos/README.md](macos/README.md).

# M5 play session: Uncharted 1, chapter 1 (owner)

Goal: play chapter 1 end to end on the Mac and bring back one log per problem. The agent fixes things
from the logs; you only play and note where things happened.

## Start
From the repo folder, in Terminal:
```
cd /Users/akserg/Projects/UNCHARTED/uncharted_pc
BB_GPU_ID=0 BB_PERF_LOG=1 BB_GPU_CHECKPOINTS=1 BB_GAME_DIR=/Users/akserg/Projects/CUSA02344 BB_EXE=u1 ./run.sh
```
- Everything the game prints goes to `user/last_run.log`. **The next run overwrites it.**
- The log gets a `PERF:` line every 5 s (FPS, frame times, CPU use, memory). You don't need to watch it.
- Stop: close the window (or Ctrl+C in Terminal).
- Menus take about 75 s to the START screen. On START, pick **New Game** (not "Play": that continues the
  old "Ambushed" save).

## Controls
| | Keyboard + mouse | Controller (DualSense / DualShock 4 / Xbox) |
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
| Settings overlay | **F10** (fn+F10 on a MacBook) or Insert | **L3 + R3 together** |
| Free the mouse pointer | hold Left Option (Alt) | |
| **Mark this spot in the log** | **F8** (fn+F8 on a MacBook; if no `MARK` line shows up, see below) | |
| **Re-centre the sticks** (a stick that rests off centre) | **F9** (fn+F9 on a MacBook) | **View (Back) + right-stick click, held 1 s** |

**F8/F10 on a MacBook:** a bare F8/F10 is a media key (play/pause, mute) that the game never sees. Either hold fn,
or switch on System Settings → Keyboard → "Use F1, F2, etc. keys as standard function keys". To check which keys
reach the game, start with `BB_KEY_LOG=1` in front of the command above: every key press prints a `KEY:` line in
the log (`grep -E 'KEY:|MARK' user/last_run.log`).

**Stick drift / deadzone.** Sticks have a scaled radial deadzone (left 7849, right 8689 out of 32767), so a
resting Xbox stick sends exactly centre. Variables (put them in front of the command above):
- `BB_STICK_DEADZONE_LEFT=n`, `BB_STICK_DEADZONE_RIGHT=n`: 0-32767, 0 turns it off. Raise the right one
  (e.g. 12000) if the camera still creeps; lower it if the stick feels dead.
- `BB_PAD_LOG=1`: once a second, `PAD: raw ... | without deadzone ... | sent ...` in the log (sticks untouched:
  `sent` must be `128 128 128 128`). `grep PAD: user/last_run.log`.

## What to test
1. **Chapter 1 end to end.** Press **F8** whenever something odd happens and at each perf spot below.
2. **Perf spots** (stand still about 20 s at each; press F8 when you arrive, write down which mark is which):
   - M1: the START menu
   - M2: chapter 1, the beach (first outdoor scene with Elena after the boat)
   - M3: the first combat (first gunfight)
   Also tell me if anything stutters or feels slow anywhere. The first time a scene appears it may hitch
   once (shaders compiling); that's expected.
3. **Audio**: Mac speakers, then headphones (plug in or connect during play). Is sound present, in sync, no
   crackling, and does it switch to the headphones?
4. **Controller**: one over USB and the same (or another) over Bluetooth. Do all buttons, sticks, triggers
   and vibration work? Do the on-screen button pictures match the controller?
5. **Keyboard + mouse**: does mouse look turn the camera smoothly? Aiming with the right mouse button?

## When something goes wrong
**Crash, freeze, or the window closes**:
1. Immediately (before starting the game again) copy the log:
   ```
   cp user/last_run.log ~/Desktop/m5-crash-$(date +%H%M).log
   ```
2. Write one line: where you were (chapter / place / what you just did) and the time.
   The log already contains the crash details (`CRASH:` lines); nothing else is needed.
3. For a freeze, wait 30 s, then close the window or press Ctrl+C in Terminal, then copy the log as above.

**Visual bug** (missing textures, flicker, black or garbage areas, wrong colours):
1. Press **F8**, then take a screenshot (Cmd+Shift+4, then Space, then click the game window).
2. Copy the log as above when you stop playing (or immediately if you will restart).
3. Write where it happened and whether it stays or flickers.

**Sound or controller problem**: write what you tried and what happened; the log at the end of the session is
enough.

## What to send back
- Every copied log (`~/Desktop/m5-*.log`) and screenshot.
- The list of F8 marks with a few words each (M1 START menu, M2 beach, M3 first combat, plus the others).
- Whether chapter 1 was completed, and how long the session took.
- The log of the last (normal) session too: `cp user/last_run.log ~/Desktop/m5-session-$(date +%H%M).log`.

## Controller check (triage round 3)
Leave the sticks and triggers alone for 10 s at the START menu, then move both sticks, press every button, check
rumble and look at the LED. Run once per backend:
```
BB_PAD_LOG=1 BB_GPU_ID=0 BB_GAME_DIR=/Users/akserg/Projects/CUSA02344 BB_EXE=u1 ./run.sh                          # default (MFi)
SDL_JOYSTICK_MFI=0 BB_PAD_LOG=1 BB_GPU_ID=0 BB_GAME_DIR=/Users/akserg/Projects/CUSA02344 BB_EXE=u1 ./run.sh      # HIDAPI
grep -E 'gamepad backend|PAD:' user/last_run.log | head
```
Write down for each: the `gamepad backend` line, the `PAD:` line at rest (`sent` must be `128 128 128 128`), whether
the camera creeps, whether the LED stops blinking, whether vibration and every button work.


## Stick rest-centre calibration (triage round 4)
For a stick that rests off centre (your right stick sometimes rests about 72 % up). The port learns the rest
position and corrects for it; full deflection still reaches the full range.
- **Automatic**: `BB_STICK_AUTOCENTER=1` (the default on macOS, `0` switches it off). In the first 2 s after
  `gamepad connected`, **leave the sticks alone**: a stick holding one steady value outside its deadzone becomes its
  centre (log: `Runtime: stick centre recalibrated: left (x,y) right (x,y) (auto)`). If you touch a stick during
  those 2 s it is not calibrated.
- **On demand** (the rest point moves while you play): leave the sticks resting and press **F9** (fn+F9 on a MacBook),
  or hold **View + right-stick click** for 1 s (the two buttons do nothing in the game meanwhile). Log:
  `Runtime: stick centre recalibrated: left (x,y) right (x,y)`. A stick that is at rest at its true centre
  (inside the deadzone) goes back to centre (0,0). F9 is also the pad recorder's key: with `BB_PAD_RECORD` set it
  keeps that job and the recentre key is off (use the chord).
- **Automatic release**: if the stick later rests at the true centre for 3 s, the correction is dropped
  (`Runtime: stick centre reset (stick returned to centre)`).
- A rest point beyond 85 % of the range is refused (log says why).
- `BB_PAD_LOG=1` shows the centre in each `PAD:` line (`centre r=(x,y) l=(x,y)`).

Check: run with `BB_PAD_LOG=1`, leave the sticks alone 10 s, then `grep -E 'stick centre|PAD:' user/last_run.log`.
`sent` must be `128 128 128 128` with the camera still. With a healthy stick there is no `centre` log line.

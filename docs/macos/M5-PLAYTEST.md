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
| Settings overlay | Insert (not on a MacBook keyboard: use the controller) | **L3 + R3 together** |
| Free the mouse pointer | hold Left Option (Alt) | |
| **Mark this spot in the log** | **F8** (fn+F8 on a MacBook) | |

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

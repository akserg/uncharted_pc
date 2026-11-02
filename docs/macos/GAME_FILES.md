# Game files the loader expects

Derived from the code at `macos` (M3): `run.sh`, `scripts/prepare.py`, `scripts/link_modules.py`,
`scripts/content_profile.py`, `scripts/trophies.py`, `src/probe.c` (`main`), `src/runtime_file.c`.
No game files are part of this repository. Use a dump made from your own console and game.

## Required layout

`BB_GAME_DIR` (for `run.sh`) / `--app0` (for `bb-probe`) points at the **game package root**:

```
<BB_GAME_DIR>/                         e.g. CUSA02320, CUSA02343, CUSA02344, ... — title ID varies by region/edition
├── eboot.bin                          U1 executable + the collection menu      (BB_EXE=u1, default)
├── big2-ps4_Shipping.elf              U2 executable                            (BB_EXE=u2)
├── big3-ps4_Shipping.elf              U3 executable                            (BB_EXE=u3)
├── sce_module/
│   ├── libc.prx                       linked into the image (link_modules.py)  REQUIRED
│   └── libSceFios2.prx                linked into the image (link_modules.py)  REQUIRED
├── sce_sys/
│   ├── param.sfo                      title ID, version, title (prepare.py, content_profile.py,
│   │                                  probe.c save-data/network config)        REQUIRED
│   ├── npbind.dat                     NP communication IDs for trophies        optional
│   └── trophy/
│       ├── trophy01.trp               U1 trophies (NPWR09798_00)               optional
│       ├── trophy02.trp               U2 trophies (NPWR09799_00)               optional
│       └── trophy03.trp               U3 trophies (NPWR09801_00)               optional
├── u1data/build/main/...              U1 assets (.pak, .psarc)                  REQUIRED for U1
├── u2data/build/main/...              U2 assets                                 REQUIRED for U2
└── u3data/build/main/...              U3 assets                                 REQUIRED for U3
```

All three executables and all three data folders are needed only if you want all three games;
`run.sh` boots one executable at a time and switches (`loadexec.txt`, exit code 101) when the
collection menu launches another.

## Format requirements (checked by `scripts/prepare.py`)

- Executables must be **decrypted**: either a plain x86-64 ELF64 (`\x7fELF`, machine 62) or a PS4 SELF
  whose segments are plaintext. An encrypted/compressed SELF fails with
  `encrypted/compressed SELF segment is unsupported`.
- `sce_module/*.prx` must be decrypted the same way.
- `sce_sys/param.sfo` must start with the `\0PSF` magic.
- Loaded image must be under 512 MiB.

## How the game sees the folder

`runtime_file.c` mounts:
| Guest path | Host path |
|---|---|
| `/app0`, `/hostapp` | `<BB_GAME_DIR>` (read-only by convention) |
| `/temp0`, `/download0`, `/data`, save-data slots | under `--user` (`BB_USER_DIR`, default `./user`) |

Relative guest paths resolve under `/app0`. The guest's `argv[0]` is `/app0/eboot.bin`.

## Assets: `.psarc` extraction

`docs/LINUX_BUILD.md` recommends extracting the `.psarc` archives in each `uNdata/build/main`
(`bin.psarc`, `core.psarc`, `uiextra.psarc`, ...) with `psarcy` and moving the contents up one level.
Upstream marks it "optional but recommended"; whether the macOS build needs it will be recorded in M4.

## What the launcher generates (not part of the dump)

`run.sh` writes into `out/<exe>/`: `boot.bin`, `eboot.elf` (prepare.py), `boot-linked.bin`, `link.json`
(link_modules.py, eboot + libc.prx + libSceFios2.prx linked, guest `fs:[0]` loads rewritten to `gs:[0]`),
`content.bin`, `content-profile.json` (content_profile.py). `bb-probe` runs `boot-linked.bin`.

## Quick check

```bash
G=/path/to/game
for f in eboot.bin big2-ps4_Shipping.elf big3-ps4_Shipping.elf sce_module/libc.prx \
         sce_module/libSceFios2.prx sce_sys/param.sfo; do
  [ -f "$G/$f" ] && echo "ok   $f" || echo "MISS $f"; done
for d in u1data u2data u3data; do [ -d "$G/$d/build/main" ] && echo "ok   $d" || echo "MISS $d"; done
python3 scripts/prepare.py "$G" --out /tmp/bbcheck --exe eboot.bin   # fails clearly if not decrypted
```

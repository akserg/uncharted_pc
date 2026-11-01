# M6 — Ship

## Goal
A zip that a macOS 15.4+ user can unpack and run.

## Preconditions
- M5 done.

## Tasks
1. `run.sh`: make it work unchanged on Darwin (BSD `sed`/`readlink` differences, `arch -x86_64`
   not needed for a native x86_64 binary — Rosetta picks it up automatically).
2. Packaging: `packaging/macos/package.sh` producing `dist/uncharted-macos/` with `bb-probe`,
   `gpu/libbbgpu.dylib`, bundled `libMoltenVK.dylib`, `libSDL3.dylib`, `libvulkan.dylib` and the other
   `$BB_DEPS` dylibs (`otool -L` walk, `install_name_tool` to `@loader_path`), `licenses/`, `run.sh`.
   No DLSS/XeSS/FSR4 binaries.
3. `.app` bundle (optional for first release): `Info.plist`, `LSMinimumSystemVersion 15.4`,
   `LSArchitecturePriority x86_64`; entitlements `com.apple.security.cs.allow-jit`,
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
_(fill in: date, commit/tag, package contents, signing approach, clean-machine test notes,
upstream PR link or decision, open issues)_

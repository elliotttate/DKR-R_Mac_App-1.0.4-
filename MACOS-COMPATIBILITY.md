# macOS compatibility and first-run access, 2026-09-20

Package: **1.0.5-beta.10-macos.2**, Apple Silicon, macOS 12.0 deployment target.
Based on upstream main `8a8e927` with the existing models and terrain. The
unfinished PBR experiment remains separate.

## Start Game compatibility defect

The previous `macos.1` executable declared macOS 12.0, but its embedded RT64
Metal libraries targeted macOS **27.0.0**. The launcher and game initialize
different rendering paths, so reaching settings did not establish that the
game shaders could load on an older OS. This is a concrete compatibility defect;
the remote user's particular crash is still unconfirmed without their log.

RT64's custom Metal compile commands now receive both
`-mmacosx-version-min=12.0` (from the CMake deployment target) and
`-std=macos-metal2.4`. The Mac build applies the tracked patch without changing
the upstream dependency pin. Packaging fails if native code or shader targets
exceed the selected minimum, or if permission descriptions are absent.

The shipped `macOS-compatibility.json` records five native binaries targeting
12.0 and 57 checked compiled Metal libraries. Of those libraries, 55 occur
verbatim inside the game executable; its embedded shader targets are 12.0.0.
The validator also checks bundled dylib paths. A regression check against the
actual previous executable rejects its macOS 27 shaders.

## Access requests and failures

Browse for ROM now uses the native macOS Open panel. Choosing a file provides
explicit user consent for that file. The bundle includes explanations for
Documents, Downloads, Desktop, removable and network volumes, file-provider
storage, and local-network multiplayer. Requests occur when the relevant
feature needs access; a file selected through the native panel may need no
additional folder prompt. See Apple's
[file-access guidance](https://developer.apple.com/documentation/bundleresources/information-property-list/nsremovablevolumesusagedescription)
and [local-network privacy guidance](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy).

An unreadable ROM leaves an actionable launcher message. If access is lost
between selection and Start Game, the app presents a native error dialog with
the diagnostic-log path before exiting. Renderer-window and caught runtime
failures also receive a native dialog. These dialogs do not catch fatal signals.

The release is Developer ID signed with a stable bundle identifier and hardened
runtime. It is **not notarized**. Full Disk Access is not required. Cloud-only
ROMs must be downloaded locally before selection. The installation README
describes Files & Folders and Local Network settings for previously denied access.

## Verification and limits

Performed on an Apple M3 Max running macOS 27.0 (26A428):

- 83/83 runtime tests passed, plus package controller-pak recovery and mod-worker
  self-tests. All 40 packaged model files match source exactly; no PBR pack ships.
- Strict/deep signature validation passed on a fresh ZIP extraction and on the
  delivered copy. Native code and Metal deployment audits passed.
- Native Metal loading test passed: 81 UI frames during a three-second cache
  wait, six generated mip levels, automatic dismissal, unchanged game queues.
- A separate Developer ID signed QA app identity, launched through LaunchServices
  with fresh configuration, opened the native ROM picker. Cancel returned to the
  launcher. A test ROM in Downloads with POSIX read permission removed produced
  the expected error. A readable ROM enabled Start Game.
- Removing test-file read permission after selection produced the new native
  Start Game error. Restoring it and relaunching reached the v77 game on Metal
  with the fresh default Accurate profile.
- The unmodified extracted package booted v80 in Modern mode from `/tmp`,
  rendered its first frame in 1.10 seconds, and exited cleanly after 70 seconds.
  The delivered copy visibly rendered replacement palms and blueberries;
  F7/F8 switched terrain/models off and back on.

The denied-file checks exercise unreadable-file handling, **not an actual TCC
Deny selection**. Existing user privacy grants were not reset. Separate prompts
for every external drive, cloud provider, and LAN setup were not exercised.

No older macOS host or VM was available. Deployment-target validation does not
prove every runtime path on macOS 12 through 26. Before describing those systems
as tested, run the ZIP on at least a Monterey Apple Silicon Mac and the affected
user's OS: choose a local ROM through the picker, start both supported revisions,
and exercise Modern models/terrain. A full playthrough and multiplayer session
are outside these checks.

## Artifacts

Build instructions: [LOCAL-MAC-BUILD.md](LOCAL-MAC-BUILD.md).
Integration history: [UPSTREAM-UPDATE.md](UPSTREAM-UPDATE.md).

Delivered ZIP:
`/Users/briantate/Documents/GitHub/DKR-R_Mac_App-1.0.4-/dist/DKR-R-1.0.5-beta.10-macos.2-macOS-arm64.zip`

SHA256:
`81a8dd4b64f6c3333be8ea2d5ac14077ea81b715d7ba6589a7aa761e8583dce1`

Local evidence under the integration worktree's
`build/validation/macos-compatibility/`: `build-package.log`,
`metal-regression-check.log`, `metal-mipmap-test.log`, `denied-rom.png`,
`start-game-access-error.png`, `native-v77-accurate.png`,
`native-v80-modern.png`, `fresh-config/dkr-port/logs/`, and
`v80-modern-config/logs/`. Logs, ROM fixtures and screenshots remain untracked.

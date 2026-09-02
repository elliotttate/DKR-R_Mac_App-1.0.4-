# DKR-R architecture

## What kind of port this is

DKR-R is a static recompilation port, not a conventional source-to-source
decompilation port. The completed Diddy Kong Racing decompilation remains the
authoritative readable reference for symbols, structures, algorithms and patch
locations. The executable path is:

```text
User-owned DKR US v1.0 or US Rev A/v1.1 ROM
        +
Pinned matching DKR decomp ELF and symbols
        |
N64Recomp-generated native CPU functions
        |
Project Patch Pipeline hooks and host policies
        |
N64ModernRuntime scheduling, saves, audio, input and RSP dispatch
        |
Recompiled Rare audio/F3DDKR microcode
        |
One SDL2 launcher/game window + private headless SDL3 controller helper
```

This approach preserves the original program and timing while allowing focused,
readable host enhancements without manually rewriting the entire game.

## Protected boundaries

The following are generated or pinned dependency work areas and must never be
edited directly:

- `runtime-recomp/RecompiledFuncs`
- `runtime-recomp/RecompiledPatches`
- `extern/rt64`
- `extern/n64-modern-runtime`
- `extern/n64-modern-runtime/N64Recomp`

DKR instruction/function hooks belong in the matching versioned
`runtime-recomp/dkr.us.v*.recomp-policy.json`. The v1.1 policy is translated
from named v1.0 functions and checked intra-function offsets by
`scripts/generate_revision_policy.py`; it is not produced by a global address
delta. Dependency changes belong in `patches/manifest.json` and its referenced
patches.

## Runtime ownership

The project-owned `runtime-recomp/src/game` layer owns:

- exact, byte-order-independent ROM validation, revision dispatch and game
  registration;
- one-window SDL platform integration;
- stable hot-plug controller routing across four N64 ports, independent
  per-player bindings, exclusive keyboard ownership, layered SDL controller
  mappings, raw-device setup and angle-based gyro input;
- virtual EEPROM and four channel-specific Controller Pak stores, with
  atomic backup recovery and simultaneous platform rumble capability;
- audio mix/EQ policy without altering the original audio clock;
- Accurate/Modern presentation policy;
- widescreen, interpolation, FOV, visibility and detail controls;
- a presentation-only HUD policy covering the retail 59-entry `HudData` ABI,
  the direct minimap draw and generated timer glyphs across every 1-4 player,
  standard, wide and ultrawide layout variant;
- shared controller-first startup/in-game DKR-R UI and Save Manager;
- F3DDKR command translation and DKR-specific renderer policy.

## Online multiplayer boundary

The project-owned netplay layer provides a session boundary with
five-character WebRTC Quick Join, per-peer X25519 key agreement,
XChaCha20-Poly1305 authentication, explicit host admission, repeated input
history, host-authored hash-chained frame commits, portable authoritative
gameplay correction, ready/load/start barriers, subsystem state hashes and
replay logs. Quick Join uses a public PeerJS endpoint only for signaling and
then carries the existing session protocol over a DTLS-protected WebRTC data
channel. No DKR-R account service, public directory, gameplay backend or relay
is required or built.
The host is always N64 port 1; approved clients map to ports 2-4. All peers
execute the same original 30 Hz simulation.

Runtime input is resolved once at a Patch Pipeline-authored boundary before the
game starts its SI read. Physical polling updates only a private local sample;
it can never clear or overwrite the committed virtual N64 ports. Frontend
lockstep, gameplay lockstep and rollback all publish the same complete,
immutable four-port frame through the project-owned online input broker. The
local keyboard/controller profile is independent from the immutable network
slot, so the host remains virtual Player 1 and guests remain Players 2-4
without requiring a user's physical pad to occupy that local index.

Shared menu ownership is released by a revision-specific named hook at
character select. Gameplay scenes enter a synchronized baseline barrier before
simulation, and the rollback driver restores local deterministic checkpoints
at the authored `main_game_loop` boundary when late input differs from a
prediction. Historical replay suppresses graphics, audio, rumble, telemetry,
save writes and lifecycle barriers. Portable host checkpoints remain the
fail-safe recovery and finish-seal path; they exclude raw host pointers, audio,
OS queues and renderer memory.

## Preset boundary

Accurate is the regression baseline: original 4:3, original 30 FPS cadence,
original FOV/detail/visibility/audio mix and original HUD placement.

Modern leaves simulation, race timing, input polling and audio on that original
timeline. It changes only presentation and explicitly selected quality-of-life
policies. High-refresh output is interpolation, not a faster game clock. HUD
placement and size are host-only presentation data and never enter deterministic
simulation, replay or online-session compatibility state.

## ROM and save boundary

The selected Game Pak is canonicalised for identification and validated against
the exact v1.0/v77 or Rev A/v1.1/v80 hash. Byte-swapped and little-endian files
are materialised once as a hash-verified big-endian file in the user's private
`rom-cache`; the original file is never modified and no ROM is copied into a
release. Revision selection happens before game registration, RT64 creation or
audio startup. Windows exposes one `DKR-R.exe`; its verified Rev A engine module
is an embedded resource extracted into a versioned private cache only when
needed. The public executable loads that DLL with `LoadLibraryExW`, resolves the
narrow `DkrRunRevisionEngine` ABI and runs it synchronously inside the existing
process. The module creates only a borderless child render surface inside the
existing client area. Resize, fullscreen, restart and close retain the original
host window, and revision selection never starts a second game process.
Linux exposes one AppImage and macOS one application bundle, with the alternate
engine kept private inside that bundle. An internal-engine guard validates the
ROM again before either engine can register generated code. The revisions
intentionally share the existing compatible EEPROM/configuration namespace.
Releases are scanned for ROM extensions and N64 ROM headers. EEPROM and four
virtual Controller Pak files remain host files and can be managed through T.T.'s
Save Manager.

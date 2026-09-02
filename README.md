# DKR-R — Diddy Kong Racing Recompiled

<img width="1774" height="887" alt="DKR-R Logo" src="https://github.com/user-attachments/assets/1c1211fc-27b7-4a4c-af01-5c20067928c2" />

<p align="center">
  <a href="https://discord.com/invite/AMWfXdBjNP">
    <img src="https://img.shields.io/badge/JOIN%20OUR%20DISCORD-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join our Discord">
  </a>
</p>


DKR-R 1.0.1 is a cross-platform static recompilation of Diddy Kong Racing for
Windows and Linux. It runs the original game from a locally selected, legally
obtained US v1.0 or US Rev A/v1.1 Game Pak image; no ROM or extracted
Nintendo/Rare assets are included.

## Version 1 highlights

- Accurate presentation: original 4:3 composition, 30 FPS cadence, audio,
  object detail and game behaviour.
- Modern presentation: widescreen, interpolated high-refresh output, FOV,
  extended scenery distance, maximum vehicle detail and anisotropic filtering.
- Controller-first launcher and transparent in-game settings overlay.
- Four independent local-player controller assignments and binding profiles,
  keyboard ownership, quick race restart and two-axis gyro steering.
- Independent live master, music, effects, vehicle and ambience levels, plus EQ.
- Save import/export/backups, checksum-safe Save Builder and launch-time Magic Codes.
- Optional performance overlay, CRT masks and hot-swappable RT64/Rice texture packs.
- Compact HUD placement controls with original, safe-area and fit-to-viewport
  modes, plus a global HUD-size control for Modern presentation.
- Native virtual EEPROM and four independent Controller Paks, available
  alongside rumble on the same controllers.
- Encrypted two-to-four-player five-character Quick Join, host-approved
  admission, one-click invitations for authenticated friends, synchronized
  start barriers, connection telemetry and deterministic replay logs.
- A host-authoritative five-second online start countdown, plus native
  JOINTVENTURE routing that preserves DKR's shared Adventure hub while giving
  each online racer control when the game assigns them the lead.
- A privacy-safe support summary with exportable settings/system information,
  optional diagnostic logging and optional crash dumps.

Game simulation, race timers, AI, input polling and audio remain on the original
timeline when Modern interpolation is enabled.

## Playing

1. Download the package for your platform.
2. Start `DKR-R.exe` on Windows or the `.AppImage` on Linux.
3. Select your own supported Diddy Kong Racing US ROM when prompted. Big-endian
   `.z64`, 16-bit byte-swapped `.v64`, and 32-bit little-endian `.n64` byte
   orders are detected automatically, regardless of the filename extension.
4. Choose Accurate or Modern and select **START Diddy Kong Racing - Recompiled**.

The public download is one application per platform. DKR-R identifies the ROM
before renderer or audio startup and selects the matching revision
automatically; users never choose a revision executable. On Windows, Rev A is
loaded as a verified in-process module, so every supported revision runs in the
original DKR-R process and window without starting a background game process.

Supported ROM SHA-1 values after byte-order normalisation:

```text
0cb115d8716dbbc2922fda38e533b9fe63bb9670
6d96743d46f8c0cd0edb0ec5600b003c89b93755
```

Open the in-game overlay with Escape, F1, or controller Back/View. Alt+Enter and
F11 toggle fullscreen.

The Play page can export a privacy-safe support report and open the local log,
crash-dump and report folders. Diagnostic logging and crash dumps are opt-in;
support reports omit file paths, save data, account names, controller IDs,
friend codes and lobby codes.

The Controls page assigns connected gamepads to Players 1-4. Automatic mode
keeps first-connected order; Manual mode remembers each assignment across
restarts and reconnects without shifting the remaining players. A bundled
cross-platform mapping database covers modern N64 pads and mainstream
controllers; an in-app N64 setup wizard handles unmapped or incorrectly mapped
devices. Only friendly controller names and supported features are shown.
Hardware identifiers used for reliable reconnect matching remain private to
the runtime. See [docs/CONTROLLERS.md](docs/CONTROLLERS.md).

Online multiplayer is player-hosted: Player 1 is always the host. Remote
friends can use a five-character Quick Join code with no account or port
forwarding; the public rendezvous exchanges signaling metadata only and never
carries gameplay. Quick Join is the sole connection workflow, so players never
select an adapter, exchange an address, import an invitation file or configure
a UDP port. There is no DKR-R account, gameplay backend or relay. See
[docs/ONLINE_MULTIPLAYER.md](docs/ONLINE_MULTIPLAYER.md).

## Release files

```text
dist/DKR-R-1.0.1-Windows-x64.zip
dist/DKR-R-1.0.1-Linux-x86_64.AppImage
```

The Linux release is always distributed as an AppImage. macOS is supported by
the source/build kit and requires an Apple host for signing and final testing.

## Building

See [docs/BUILDING.md](docs/BUILDING.md). The short Windows path is:

```text
Build-DKR-Runtime.cmd
Build-Windows.cmd
```

The Linux build uses the already generated recompilation sources:

```bash
./Setup-Linux.sh
./Build-Linux.sh
```

Dependency revisions are pinned in `dependencies.lock.json`; all dependency
changes are reproducible patches listed in `patches/manifest.json`.

## Repository rules

Never edit dependency worktrees, `runtime-recomp/RecompiledFuncs`, or
`runtime-recomp/RecompiledPatches` directly. DKR hooks belong in
the versioned `runtime-recomp/dkr.us.v*.recomp-policy.json` files; dependency
changes belong in the Patch Pipeline.

Before distribution, run:

```bash
python scripts/scan_for_game_assets.py
```

See [docs/ASSET_POLICY.md](docs/ASSET_POLICY.md),
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/TEXTURE_PACKS.md](docs/TEXTURE_PACKS.md), and
[docs/ONLINE_MULTIPLAYER.md](docs/ONLINE_MULTIPLAYER.md), and
[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Credits and licensing

DKR-R was created by ThatGuyMcd with contributions from the DKR-R project.
The DKR-R application icon was created by
[POOTERMAN](https://www.deviantart.com/pooterman). The DKR-R HDR Texture Pack
Project re-imagines the original artwork in crisp HD while remaining faithful
to DKR; `sr.gu` leads that community project.
The runtime uses N64Recomp, N64ModernRuntime, RT64, SDL2, a private headless
SDL3 input helper, and Dear ImGui; exact credits and licences are in
[THIRD_PARTY.md](THIRD_PARTY.md).

DKR-R does not include or grant rights to Diddy Kong Racing, its ROM, or its
assets. Diddy Kong Racing and related properties belong to their respective
owners.

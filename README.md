
# DKR-R — Diddy Kong Racing Recompiled
<img width="1672" height="941" alt="bb1a4a71-c0c0-4803-a31e-377e8f4bbf35" src="https://github.com/user-attachments/assets/c913bee2-029c-4e29-9921-0f6abe7a3bf1" />


[![Discord](https://img.shields.io/badge/Discord-Join%20our%20server-informational?style=flat&logo=discord)](https://discord.gg/JnbKRBtHqC)

DKR-R 1.0.0 is a cross-platform static recompilation of Diddy Kong Racing for
Windows and Linux. It runs the original game from a locally selected, legally
obtained US 1.0 Game Pak image; no ROM or extracted Nintendo/Rare assets are
included.

## Version 1 highlights

- Accurate presentation: original 4:3 composition, 30 FPS cadence, audio,
  object detail and game behaviour.
- Modern presentation: widescreen, interpolated high-refresh output, FOV,
  extended scenery distance, maximum vehicle detail and anisotropic filtering.
- Controller-first launcher and transparent in-game settings overlay.
- Keyboard/controller remapping, quick race restart and two-axis gyro steering.
- Independent live master, music, effects, vehicle and ambience levels, plus EQ.
- Save import/export/backups, Save Builder and Magic Code switches.
- Optional performance overlay, CRT masks and hot-swappable RT64/Rice texture packs. (An official community-made texture pack can be found on our discord (*WIP*))
- Native virtual EEPROM and four Controller Paks.

Game simulation, race timers, AI, input polling and audio remain on the original
timeline when Modern interpolation is enabled.

## Playing

1. Download the package for your platform.
2. Start `DKR-R.exe` on Windows or the `.AppImage` on Linux.
3. Select your own Diddy Kong Racing US 1.0 ROM when prompted.
4. Choose Accurate or Modern and select **START Diddy Kong Racing - Recompiled**.

Supported ROM SHA-1 after byte-order normalisation:

```text
0cb115d8716dbbc2922fda38e533b9fe63bb9670
```

Open the in-game overlay with Escape, F1, or controller Back/View. Alt+Enter and
F11 toggle fullscreen.

## Release files

```text
dist/DKR-R-1.0.0-Windows-x64.zip
dist/DKR-R-1.0.0-Linux-x86_64.AppImage
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
`runtime-recomp/dkr.us.v77.recomp-policy.json`; dependency changes belong in the
Patch Pipeline.

Before distribution, run:

```bash
python scripts/scan_for_game_assets.py
```

See [docs/ASSET_POLICY.md](docs/ASSET_POLICY.md),
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/TEXTURE_PACKS.md](docs/TEXTURE_PACKS.md), and
[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Credits and licensing

DKR-R was created by ThatGuyMcd with contributions from the DKR-R project.
The runtime uses N64Recomp, N64ModernRuntime, RT64, SDL2 and Dear ImGui; exact
credits and licences are in [THIRD_PARTY.md](THIRD_PARTY.md).

DKR-R does not include or grant rights to Diddy Kong Racing, its ROM, or its
assets. Diddy Kong Racing and related properties belong to their respective
owners.

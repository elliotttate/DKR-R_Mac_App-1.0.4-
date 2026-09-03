# DKR-R for macOS — Apple Silicon

**Unofficial macOS build of [DKR-R](https://github.com/ThatGuyMcd/DKR-R) (Diddy
Kong Racing Recompiled).** Signed and notarized by Apple, so it installs like
any normal Mac app. This fork exists only to build DKR-R for macOS; all game
credit belongs to the upstream project. This build was put together by Claude and tested by jt87.

### [Download the latest release](../../releases/latest)

| | |
|---|---|
| Requires | Apple Silicon Mac (M1 or newer), macOS 12 Monterey or later |
| Also needs | Your own legally obtained Diddy Kong Racing US ROM |
| Does **not** need | Homebrew, Xcode, or command-line tools |
| ROM revisions | Both US v1.0 and US Rev A/v1.1 are supported |

Download the `.zip`, unzip, drag **DKR-R.app** to Applications, and open it. On
first launch macOS asks *"DKR-R is an app downloaded from the Internet"* — click
**Open**. Every notarized app asks this once. Full instructions are in the
`README.md` attached to the release.

Intel Macs cannot run this build.

## Why this fork exists

The DKR-R project releases Windows and Linux binaries. macOS ships as a source
kit that had not been built to completion, so building it required fixes that
are not in any upstream release:

- **Build tooling** — the dependency patch script tested `.git` with `is_dir()`,
  which is false for a git submodule, so N64Recomp's patches were silently
  skipped and recompilation failed. The macOS prep script also expanded an empty
  array under `set -u`, which the bash 3.2 that macOS ships rejects.
- **Missing platform support** — `runtime_platform.cpp` had no `__APPLE__`
  branch for the window handle. RT64 needs the `CAMetalLayer`, not the `NSView`,
  and the layer has to be rebuilt at launcher handoff or the launcher's last
  frame re-composites over the running game.
- **Retina** — RT64 forces `PLUME_APPLE_RETINA_ENABLED` off on Apple, which
  rendered the game at half resolution and drew the settings overlay at twice
  its size.
- **libc++ differences** — `__int128` file timestamps, and
  `std::atomic<std::shared_ptr<T>>`, which libc++ does not implement.
- **A latent bug on every platform** — a mutex locked after its own destruction
  at `exit()`. Undefined behavior on Windows and Linux too; libc++ is just the
  one that reports it.

Four of these are upstream bugs and have been reported to the relevant projects.

The full set is in [`macos-patches/`](macos-patches/) as a standalone series,
and applied to the source in this fork. `scripts/Bundle-macOS-Redistributable.sh`
vendors SDL into the app bundle so it runs without Homebrew.

## Support

**Please do not report problems with this build to the DKR-R project or their
Discord** unless you can reproduce the same issue on an official Windows or
Linux release. Bugs specific to macOS belong here.

## Building it yourself

```bash
./Setup-macOS.sh
./scripts/Prepare-DKR-Runtime-macOS.sh          # prompts for your ROM
# re-apply the RT64 Retina patch, which dependency prep resets:
(cd extern/rt64 && git apply ../../macos-patches/extern/rt64-enable-retina.patch)
DKR_MAC_V80_GENERATED_SOURCE=<v80-generated-dir> ./Build-macOS.sh
./scripts/Bundle-macOS-Redistributable.sh dist/*/DKR-R.app "Developer ID Application: ..."
```

`Build-macOS.sh` requires generated sources for **both** ROM revisions. The prep
script produces the v1.0 (v77) payload; the Rev A (v80) payload is generated
separately from a v1.1 ROM with `scripts/generate_recomp_config.py` and
N64Recomp.

## Licence

This fork is MIT, matching upstream. The runtime links **GPL-3.0**
N64ModernRuntime, so the distributed binary carries GPL-3.0 terms — the
corresponding source for each release is this repository at the matching commit.
See `LICENSE.md`, `THIRD_PARTY.md`, and `runtime-recomp/COPYING-NOTICE.md`.

Not affiliated with, endorsed by, or supported by Nintendo or Rare. No
copyrighted game data is included or distributed here.

---

# Upstream project README

Everything below is the original DKR-R README, unchanged. Note that it describes
the Windows and Linux releases; the download links and platform notes there do
not apply to this macOS fork.

# DKR-R — Diddy Kong Racing Recompiled

<img width="1672" height="941" alt="bb1a4a71-c0c0-4803-a31e-377e8f4bbf35" src="https://github.com/user-attachments/assets/c913bee2-029c-4e29-9921-0f6abe7a3bf1" />

<p align="center">
  <a href="https://discord.com/invite/AMWfXdBjNP">
    <img src="https://img.shields.io/badge/JOIN%20OUR%20DISCORD-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join our Discord">
  </a>
</p>


DKR-R 1.0.4 is a cross-platform static recompilation of Diddy Kong Racing for
Windows and Linux. It runs the original game from a locally selected, legally
obtained US v1.0 or US Rev A/v1.1 Game Pak image; no ROM or extracted
Nintendo/Rare assets are included.

## Version 1 highlights

- Accurate presentation: original 4:3 composition, 30 FPS cadence, audio,
  object detail and game behaviour.
- Modern presentation: widescreen, interpolated unlocked FPS, FOV,
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
- A privacy-safe support summary with exportable settings/system information,
  optional diagnostic logging and optional crash dumps.

Game simulation, race timers, AI, input polling and audio remain on the original
timeline when Modern interpolation is enabled.

## Version 1.0.1 highlights

- Shadows and vehicle props have been fixed and no longer pop in and out, flicker or shrink on random frames.
- Extended Controller compatibility! SDL2 and SDL3 compatible as well as native gyro support for the steamdeck. No longer need to use Steam Input for Gyro. 
- Overlay and Launcher Overhaul with loads of new settings added!
- 2 Player Online Multiplayer is now Included! 2P Adventure is also working as intended.
  (Highly dependent on a stable internet connection for the best experience. Use the built in
  Network Tester in the Lobby before starting your match to make sure the connection is good before playing)
- An issue with Custom Texture Alignment has been fixed.
- The Black bar that would appear at the bottom while using Expand to Window has been fixed.
- Performance has been fixed in many areas.
- You can now invert controls on a Per-vehicle basis. So if you want inverted controls for Planes only, you can!
- Fixed an issue where users would experience a crash or black screen at un-predictable intervals. This was due to an Audio Overflow and has been fixed.
- Encrypted two-player five-character Quick Join, host-approved
  admission, one-click invitations for authenticated friends, synchronized
  start barriers, connection telemetry and deterministic replay logs.
- A host-authoritative five-second online start countdown, plus native
  JOINTVENTURE routing that preserves DKR's shared Adventure hub while giving
  each online racer control when the game assigns them the lead.

## Version 1.0.2 highlights

- Stability and Performance fixes. 

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
forwarding; the public rendezvous exchanges signalling metadata only and never
carries gameplay. Quick Join is the sole connection workflow, so players never
select an adapter, exchange an address, import an invitation file or configure
a UDP port. There is no DKR-R account, gameplay backend or relay. See
[docs/ONLINE_MULTIPLAYER.md](docs/ONLINE_MULTIPLAYER.md).

## Release files

```text
dist/DKR-R-1.0.4-Windows-x64.zip
dist/DKR-R-1.0.4-Linux-x86_64.AppImage
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

## Texture Pack

For those of you looking for the DKR-R Community Texture pack. It can be found in our Discord Server here:

<p align="center">
  <a href="https://discord.com/invite/AMWfXdBjNP">
    <img src="https://img.shields.io/badge/JOIN%20OUR%20DISCORD-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join our Discord">
  </a>
</p>


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

# DKR-R 1.0.4 — Windows x64 with 3D models

Native Windows x64 Release build of this fork, published alongside the existing
Apple Silicon release. The release page identifies the Windows source commit
separately from the unchanged Mac source tag and archive.

## Included

- All six model families: palms and attached canopies, blueberries, rubber
  trees, beach trees, spinning bananas, and seven balloon finishes, including
  collectible balloons.
- All 40 model files: 16 meshes, 12 textures, and 12 manifests/renderer mappings.
  Every model file in the ZIP matches its source SHA-256.
- Procedural terrain detail and the fork's existing Modern presentation features.
- One executable supporting both US v1.0 and US Rev A/v1.1, bundled SDL2 and
  shader runtimes, and the private SDL3 input helper.

## Installation

Download `DKR-R-1.0.4-Windows-x64.zip` and its `.sha256` file from the release.
Compare the archive's hash with the checksum file:

```powershell
Get-FileHash -LiteralPath .\DKR-R-1.0.4-Windows-x64.zip -Algorithm SHA256
```

Extract the whole ZIP, run `DKR-R.exe`, and select your own supported ROM.
Select **Modern** for the new models. **F8** toggles 3D plants/items; **F7**
toggles terrain detail. Keep the complete `assets` folder beside the executable.
No ROMs, saves, external HD packs, or ROM-derived preparation data are included.

## Verification and limits

- Compiled with Visual Studio 2022 and Windows SDK 10.0.26100.0, using the pinned
  dependencies and checksummed patches in this repository.
- Both ROM-to-source preparation builds matched their expected checksums.
- Final full CTest suite: **63/63 passed**. An earlier direct-session test
  failed; its standalone rerun and the final complete suite passed.
- Packaged Controller Pak, SDL3 input-host, and live input-switch self-tests passed.
- The packaged executable ran for 30 seconds with each ROM revision in Modern
  mode, loaded all six model families, and exited cleanly. The v1.0 run completed
  853 graphics tasks; v1.1 completed 872.
- Startup traces show actual 3D draws for palms, attached canopies, and blueberries.
  This does not establish complete course, collectible, or live multiplayer coverage.
- Source and package asset scans passed.

Windows fixes select a compatible SDK, avoid a Windows macro collision in the
3D renderer, and provide COM declarations required by the texture loader.
Windows packaging now rejects missing or stale model files, verifies all model
hashes in the ZIP, and includes Windows installation instructions.

See [build instructions](docs/BUILDING.md), [model controls](packaging/WINDOWS-3D-MODELS.md),
and the separate [Mac release notes](RELEASE-NOTES-macos.1.md).

# DKR-R 1.0.5-beta.10-macos.4 — SR.GU HD textures and Mac diagnostics

This Apple Silicon release integrates upstream main at `8a8e927` (1.0.5-beta.10)
with this fork's six 3D model families and terrain detail. It includes
**DKR REMASTERED (SR.GU's)** by **SR.GU (sr.gu)**: 1,741 usable texture identities,
prepared from the supplied pack, including 129 merged RGB/alpha pairs.

Select **Modern** to use the bundled artwork, models and terrain. The pack is
active by default and can be deactivated or hidden in **Textures**; that choice
survives restarts. Accurate mode retains original textures. No external pack
import, Python installation or additional storage authorization is needed.
The built-in files stay in the signed app; preferences stay in the user's profile.
Existing user-imported packs retain their settings and can override bundled art.

The release also includes the compatibility, file access and diagnostics work:

- Native code and Metal shaders target macOS 12.0; the package audit rejects
  newer deployment requirements and external build-machine library paths.
- The native ROM picker authorizes access to a selected file. Feature-specific
  permission explanations are included; Full Disk Access is not required.
- **DKR-R Diagnostics.app** runs separately from the renderer, exports crash
  reports, captures early launch failures, and can launch a separate fresh
  profile while preserving normal settings and saves. Nothing uploads automatically.

Both apps are Developer ID signed, **not notarized**. The package targets
Apple Silicon M1 or newer; Intel Macs are not supported. Native runtime QA was
performed on macOS 27; older macOS runtime compatibility is not yet verified.
See [installation and troubleshooting](packaging/RELEASE-README.md),
[compatibility](MACOS-COMPATIBILITY.md), and [diagnostics](MACOS-DIAGNOSTICS.md).
The unfinished PBR experiment is excluded. No ROM, save files or extracted
retail resource archives are distributed. Supply a supported US v1.0 or Rev A ROM.

This is a **Mac-only prerelease**. The existing 1.0.4 Windows ZIP is unchanged;
its source fixes were merged and retained. This release does not claim Windows
runtime validation of the new upstream beta or bundled pack.

## Verification

- 86/86 CTest checks passed. The direct-session test fixture now services both
  peers during handoff and locks its queue access; three focused repeat runs
  passed after fixing those test-only timing/race failures.
- The prepared pack's 1,741 PNGs, dimensions, hashes and native Rice aliases
  were verified. All 134 RGB-derived outputs passed source pixel/alpha checks.
- All 40 files across six model families and all 1,744 pack files match source
  SHA-256 values in the staged app. The ZIP contains all 1,741 replacement PNGs.
- The native bundled-pack lifecycle test passed from a fresh extracted app,
  launched outside the source directory: default activation, persistent
  deactivation, hide/restore, reactivation and refusal to delete signed assets.
  The launcher displayed the pack and its 1,741-texture count; keyboard
  deactivation and the disabled removal control were verified visually.
- The extracted app ran with both US ROM revisions in separate test profiles
  on macOS 27. SR.GU artwork was inspected in the US v1.0 run; logs confirm
  seven accepted replacement directories (HD plus six model atlases), actual
  3D draws and terrain detail. Rev A completed 1,191 graphics tasks over a
  40-second run and exited cleanly. The user also confirmed the game appeared
  to work in their local visual check.
- Deep/strict signatures were checked for the game and standalone Diagnostics
  app, and rechecked after the native tests. The macOS 12 audit passed for six
  native binaries, 57 Metal libraries and 55 embedded shader libraries.
- Source and packaged asset scans exclude ROMs, saves and retail archives.

Older macOS devices, every course and live online multiplayer have not received
exhaustive runtime validation. Initial streaming may briefly use original
sprites before an atlas is ready; the models load afterward.

Archive: `DKR-R-1.0.5-beta.10-macos.4-macOS-arm64.zip` (230,647,052 bytes).
SHA-256: `6fb3af62e95cd41d08e8fd7c6c2dce661f8079fb9e1d677e803319e5aab08e4a`.


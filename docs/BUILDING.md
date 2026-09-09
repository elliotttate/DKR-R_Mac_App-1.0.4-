# Building DKR-R 1.0.4

## Requirements

Windows preparation requires Visual Studio 2022 with Desktop development with
C++, Windows SDK 10.0.26100.0 or newer, Git, Python 3, PowerShell and WSL2/Ubuntu.
Linux requires CMake 3.24+, Ninja,
Clang or GCC, Vulkan development files, Wayland/X11 development files and
AppImage packaging dependencies. The pinned SDL3 source used by the private
input helper is fetched by the preparation pipeline. You must supply supported
US v1.0 and US Rev A/v1.1 ROMs locally to produce both release engines.

## Prepare generated game code and dependencies

From a Windows terminal at the repository root:

```text
Build-DKR-Runtime.cmd
```

This validates the ROM, builds the matching decomp ELF, checks out the exact
dependency commits, applies `patches/manifest.json`, generates CPU/RSP sources
and runs the runtime probe. Generated and dependency worktrees are ignored and
must not be edited.

The release build consumes two internal engines but exposes one application.
Windows embeds the private Rev A engine in `DKR-R.exe`; Linux keeps it inside
the single AppImage and macOS inside `DKR-R.app`. The existing protected
`RecompiledFuncs` tree remains the v1.0/v77 output. Generate v1.1/v80 into a
separate build workspace, translate the versioned policy with
`scripts/generate_revision_policy.py`, create the N64Recomp configuration with
`scripts/generate_recomp_config.py`, and configure the second CMake tree with:

```text
-DDKR_GENERATED_SOURCE_V77=<v77-generated-directory>
-DDKR_GENERATED_SOURCE_V80=<v80-generated-directory>
```

CMake deliberately rejects attempts to compile v80 against the protected v77
generated directory.

Both revisions may instead be generated into an external workspace. Pass
`-GeneratedSource` to the Windows build, `DKR_LINUX_GENERATED_SOURCE` to the
Linux build, or `DKR_MAC_GENERATED_SOURCE` to the macOS build for the v77
output. The corresponding `*V80*GENERATED_SOURCE` option selects v80. This is
the recommended release workflow because generated and build output remains
outside the repository.

After a recomp policy change use:

```text
Diagnose-DKR-Recompile.cmd
```

## Windows release

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-DKR-R-Windows.ps1 -Clean -Package
```

This configures a native Visual Studio x64 Release build, compiles the runtime,
runs the complete DKR-R CTest suite, executes the Controller Pak self-test,
scans the staged package and creates:

```text
dist/DKR-R-1.0.4-Windows-x64.zip
```

The Windows script defaults to SDK `10.0.26100.0`, which supplies the GameInput
and D3D12 declarations used by the pinned dependencies. To select a newer
installed SDK, pass `-WindowsSdkVersion <version>`; use `-Clean` when changing
SDKs in an existing build tree.

The Windows package includes all six 3D model families, their near/far meshes,
attached palm canopies, collectible balloons, and texture mappings. Packaging
rejects missing or stale staged model files and verifies their SHA-256 hashes
inside the final ZIP. See `3D-MODELS.md` in the package for the F7/F8 controls.

## Linux and AppImage

On Ubuntu or another supported build host:

```bash
./Setup-Linux.sh
./Build-Linux.sh
```

The build uses Vulkan through RT64, runs the complete DKR-R CTest suite and the
packaged Controller Pak self-test, then creates:

```text
dist/DKR-R-1.0.4-Linux-x86_64.AppImage
```

An unpackaged Linux binary is not a complete release deliverable. Both Patch
Pipeline outputs are mandatory so the single runtime can link the US v1.0 and
US v1.1 recompiled CPU payloads. Set `DKR_LINUX_V80_GENERATED_SOURCE` to the
matching v1.1 Patch Pipeline output before running `Build-Linux.sh`. On Windows,
`scripts/Build-DKR-R-Windows.ps1 -Package` accepts
`-Revision80GeneratedSource` or the `DKR_WINDOWS_V80_GENERATED_SOURCE`
environment variable.

Every package contains one `DKR-R` executable. Exact ROM identification selects
the matching recompiled payload in-process; both revisions always share the same
frontend, renderer, input system, runtime state and application window. The
packagers reject a release containing more than one public executable.

## macOS

On an Apple host with Xcode command-line tools, CMake and Ninja:

```bash
./Setup-macOS.sh
./Build-macOS.sh
```

RT64 uses Metal on macOS. See `packaging/MACOS-BUILD-README.md` for the handoff
and validation checklist.

## Release safety

Run `python scripts/scan_for_game_assets.py` before packaging. No ROM, save,
Controller Pak, extracted asset, log, build cache or local configuration may be
included. Release archives are scanned again by their packaging scripts.

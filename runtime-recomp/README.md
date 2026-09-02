# DKR-R native recompilation runtime

This directory contains the project-owned integration layer for the playable
Diddy Kong Racing static recompilation.

The runtime combines N64Recomp-generated DKR CPU functions, recompiled Rare
audio/F3DDKR RSP microcode, N64ModernRuntime, the DKR-specific RT64 bridge and
the existing native SDL2 startup/in-game UI. A private, headless SDL3 helper
supplies optional controller and sensor snapshots without creating a window or
loading SDL3 into the launcher/game process.
It boots, renders, plays audio, accepts input, saves and supports both Accurate
and Modern presentation profiles.

Modern also provides independent live audio buses, two-axis angle-held gyro
control, configurable anisotropic filtering, real runtime telemetry, four
local-player controller assignments, independent binding profiles, a bundled
cross-platform controller database, a raw-device N64 mapping wizard, a guarded
Quick Restart chord and a controller-navigable Save Builder backed by a native
DKR EEPROM codec. The codec validates and regenerates all Adventure,
global-config and T.T.-record checksums while retaining unknown fields.

The Save Builder also exposes the 24 retail Magic Codes as a separate launch
configuration. Codes are applied once at the first authoritative game-loop
boundary after save initialization; they are not written into EEPROM data.
Mutually exclusive codes follow the original game's rules, and one-shot codes
are removed from their launch queue after injection.

## Reproducible patch boundary

Never edit `RecompiledFuncs`, `RecompiledPatches`, RT64, N64Recomp or
N64ModernRuntime manually.

- DKR hooks are declared in the versioned `dkr.us.v77.recomp-policy.json` and
  `dkr.us.v80.recomp-policy.json` files.
- dependency changes are declared by the repository Patch Pipeline.
- `Diagnose-DKR-Recompile.cmd` regenerates the CPU output from the prepared ELF.

## Build output

From the repository root run `Build-DKR-Runtime.cmd`, or run
`Diagnose-DKR-Recompile.cmd` after a policy-only change.

Windows output:

```text
build/dkr-runtime-rt64/bin/Release/DKR-R.exe
```

The single executable contains both Patch Pipeline CPU payloads. Exact ROM
identification selects the appropriate payload and address table in-process;
both revisions share the same frontend, renderer, input system and runtime.

Linux output:

```text
build/dkr-runtime-linux/bin/Release/DKR-R
```

Every Linux release must also be packaged as an AppImage with `Build-Linux.sh`
or `scripts/Package-Linux-AppImage.sh`.

macOS output is a native RT64/Metal `DKR-R.app`; `Build-macOS.sh` performs the
complete compile, test, self-test, bundle, scan and ZIP flow on an Apple host.

## ROM policy

Diddy Kong Racing US v1.0/v77 and US Rev A/v1.1/v80 are supported in all three
retail N64 byte orders. Exact canonical hashes are checked before selecting
the matching in-process CPU payload, before renderer/audio startup or game
registration. Both revisions share the compatible 512-byte EEPROM namespace;
ROMs, extracted assets, saves, logs and local configuration must never enter
source or release archives.

# Local Apple Silicon build

This source integrates ThatGuyMcd/DKR-R `main` at
`8a8e927e9ea14c5c07ca7ad74b44fdbe077f5ff6` (2026-09-19), version
`1.0.5-beta.10`, with the Mac port and the existing 3D plants/items and terrain.
The upstream `build/1.0.5-beta.11` branch changes release metadata only; this
integration follows the default branch. Unfinished PBR materials are separate.

## Prepare dependencies and game inputs

Use the dependency pins in `dependencies.lock.json` and apply
`scripts/apply-dependency-patches.sh`. Then apply the Mac Retina patch:

```sh
git -C extern/rt64 apply ../../macos-patches/extern/rt64-enable-retina.patch
```

Use a full Xcode installation for Metal. Build the pinned decomp with **gmake**,
not Apple's old make. Both supported ROM revisions are required. Keep a copy of
each matching ELF and Z64 before switching revisions because the decomp reuses
its build directory. No ROMs or generated game payloads belong in Git or releases.

The locally rebuilt v77/v80 ROMs must have these canonical SHA1 hashes:

- v77: `0cb115d8716dbbc2922fda38e533b9fe63bb9670`
- v80: `6d96743d46f8c0cd0edb0ec5600b003c89b93755`

The macOS v80 ELF is separately hash-pinned in
`scripts/legacy_character_presentation_policy.py`; all legacy hook instruction,
function, and delay-slot checks remain active.

## Generate both current beta payloads

The beta adds legacy track/character hooks. The old 1.0.4 generated C cannot be
reused. The generator composes the checked fragments into ignored build policies
and runs the pinned, patched N64Recomp tool for both revisions:

```sh
python3 scripts/prepare_macos_payloads.py \
  --v77-build /path/to/v77-elf-and-rom \
  --v80-build /path/to/v80-elf-and-rom \
  --recompiler /path/to/N64Recomp \
  --output build/payloads
```

Each input directory contains `dkr.us.v77.elf` / `dkr.us.v77.z64`, or the v80
counterparts. Generated files are never edited manually.

## Build, test and package

```sh
export DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer
export CMAKE_PREFIX_PATH=/path/to/sdl2-2.32.10/install
export DKR_MAC_SDL2_DYLIB=/path/to/sdl2-2.32.10/install/lib/libSDL2-2.0.0.dylib
export DKR_MAC_PAYLOAD_DIR="$PWD/build/payloads"
export DKR_RELEASE_VERSION=1.0.5-beta.10-macos.1
bash Build-macOS.sh
```

`DKR_MAC_PAYLOAD_DIR` supplies both generated source directories and the exact
composed policies used for the runtime fingerprint. The build runs all DKR tests,
controller-pak recovery and the mod-importer self-test; packages SDL2, SDL3, the
private mod worker and all six model families; checks dylib paths and scans the
release for game assets; signs ad hoc and produces the ZIP under `dist`.
It refuses to overwrite an existing staged release or ZIP.

The app requires Apple Silicon and macOS 12 or newer. It is ad-hoc signed and
not notarized. Modern mode enables the optional models (F8) and terrain (F7).
ROMs, saves and external HD texture packs remain outside the bundle.

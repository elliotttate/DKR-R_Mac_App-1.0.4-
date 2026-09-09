# Local Apple Silicon build

Base: jt87/DKR-R_Mac_App-1.0.4- at 6bb36b1, based on DKR-R 1.0.4
at b015686. The fork adds Retina launcher scaling, native Metal layer
handoff, and Apple compiler/runtime compatibility fixes.

The external patch `macos-patches/extern/rt64-enable-retina.patch` must
also be applied to the pinned RT64 checkout. Build-macOS.sh now rejects
a checkout with this setting disabled, since it clips/oversizes the
in-game settings overlay on Retina displays.

Local additions keep runtime assets and the SDL3 input helper discoverable
inside the app when launched from Finder, bundle SDL2 and SDL3 with correct
macOS load paths, and include their licenses. Packaging rejects external
non-system dylib paths and signs the resulting app ad hoc. This local build
is not the fork author's Developer ID-signed/notarized release.

The dependency lock file, patch manifest, and both revision policy files
were compared byte-for-byte with the previously prepared DKR-R checkout.
Its matching pinned dependency sources and generated v77/v80 CPU sources
were reused. Compilation and packaging run in this checkout, separately
from the older app. Original texture sources and saves are not modified.

Example build environment (adjust the Xcode and locally built SDL2 paths):

```sh
export DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer
export CMAKE_PREFIX_PATH=/path/to/sdl2-2.32.10/install
export DKR_MAC_SDL2_DYLIB=/path/to/sdl2-2.32.10/build/libSDL2-2.0.0.dylib
export DKR_MAC_V80_GENERATED_SOURCE="$PWD/build/recomp-v80/RecompiledFuncs"
export DKR_RELEASE_VERSION=1.0.4-macos.1
bash Build-macOS.sh
```

SDL2 2.32.10 was built locally instead of depending on a Homebrew-only
runtime. The build script refuses to overwrite existing dist artifacts;
retain/move an existing release before packaging another one.

HD textures use the game's native texture-pack system. The previously
converted DKR REMASTERED (incomplete) pack is compatible with this fork
and enabled in the existing `~/.config/dkr-port` configuration. It maps
1,815 source images to 1,597 replacement identities, including 129 merged
RGB/alpha pairs. One additional source image has no game texture hash
and therefore cannot be mapped. Missing textures retain original art.

# DKR-R 1.0.4-macos.1 — 3D items, plants and terrain

Native Apple Silicon / Metal build of this experimental fork. Requires macOS
12 or newer and your own supported Diddy Kong Racing US v1.0 or Rev A/v1.1 ROM.

## Included

- Spinning 3D bananas replace all eight world-sprite views. Rotation follows
  the original animation phase, including pause and phase wrap. HUD art is unchanged.
- All five weapon-balloon colors plus gold/silver collectibles use 3D meshes.
  Seven new 2K finishes feature large staggered stars, sharp rainbow waves,
  silver mirror tiles and the blue zigzag on gold, with repaired wrap seams.
- Repaired 3D palms and attached canopies, blueberry plants, round-leaf trees
  and beach trees, including material/brightness refinements and trunk joins.
- Material-aware terrain detail, cliff refinement and restrained decorations.
- Native Metal presentation, Retina scaling fixes, bundled SDL2/SDL3 libraries
  and Finder-safe asset discovery.

Select Modern to use the replacements. F8 toggles 3D plants/items; F7 toggles
terrain detail. Original simulation, collision, pickup and respawn timing are
retained. Missing optional model assets fall back to the original sprites.

## Installation and scope

Download the macOS-arm64 ZIP, verify it with `SHA256SUMS.txt`, unzip and move
`DKR-R.app` to Applications. No Homebrew or Xcode is required.

**Ad-hoc signed, not notarized.** This is not jt87's notarized release. If macOS
blocks it, only after verifying/trusting the download use Privacy & Security >
Open Anyway for this app; do not disable system-wide protections.

No ROM, saves, external HD texture pack or ROM-derived audit data is included.
The replacement model textures are included; an existing external HD pack may
be imported separately. Accurate mode retains original sprite presentation.

## Verification and limits

The preceding Preview 2 passed all 63 DKR tests, controller-pak recovery,
deep/strict signature checks and bundled dependency checks. All seven balloon
finishes were rendered through 360 degrees using the exported runtime meshes,
with eight cardinal/diagonal views inspected. Native USA 1.1 attract-mode
traces exercised all five weapon colors without sprite fallbacks.

The release is rebuilt and verified from the published commit. Gold/silver
collection scenes, all-course pickup/respawn behavior and multiplayer have
not received exhaustive visual testing. Highlights are baked material artwork,
not dynamic PBR lighting. The separate polygonal gold-balloon cinematic actor
is not replaced; HUD, balloon pop effects and string particles are unchanged.

See [balloon details](BALLOONS-3D-PREVIEW.md), [banana details](BANANA-3D-PREVIEW.md),
[terrain limits](TERRAIN-DETAIL.md) and [build instructions](LOCAL-MAC-BUILD.md).

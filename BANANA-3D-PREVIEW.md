# Spinning 3D bananas — Preview 1

Replaces collectible world bananas using the supplied
`Banana_-_Photorealistic_Fruit_Asset.usdz` (SHA256
`c2d9e989430b632f3f02c2a7808695dda32b2b3bea5cc82a0d85d184e6091287`).
The input file and supplied reference frames are unchanged.

## Identification and animation

Both supplied USA ROM revisions use sprite **156** for the eight banana
views. Sprite **161** is the separate HUD icon and is not replaced. The
runtime requires sprite 156, sprite model type 1, and banana behaviour 32
in both the object and header; scenery, spawners, particles and HUD draws
do not pass this gate.

The decompiled `obj_loop_banana` advances `animFrame` by `updateRate * 8`.
The original billboard uses `((animFrame & 255) * 8) >> 8` to select a view.
The replacement instead uses the full low-byte phase for one world-space
Y rotation: `yaw + phase * 256` in the game's 16-bit angle units. A revolution
therefore lasts 32 guest ticks, with 32 distinct guest poses at updateRate=1,
and rigid-transform interpolation between them. No wall-clock timer or
render-frame accumulator is involved. Pause, simulation speed and object
lifetimes retain their original timing. The interpolation identity does not
change with phase, sprite image, or LOD, including the 248→0 wrap.

The shared replacement draw path renders one mesh, then suppresses every
triangle/tile in that one sprite draw only after its atlas is GPU-ready.
Its private atlas is preloaded and kept resident, without matching or replacing
any of the eight retail texture hashes. Failed/missing assets retain sprites.

## Geometry, material and placement

- 2,762 triangles / 1,749 runtime vertices, retained identically at both LODs.
  A 900-triangle experiment caused UV artifacts and was not shipped.
- Upright, centered spin axis, Y-up, normalized height 1. The source model
  was rotated from its diagonal pose before export.
- Sprite anchor (36,66), tile y=6 and height=51 yield the authored y=9..59
  quad range. The mesh uses height 50 and bottom offset 9, multiplied by the
  original object scale. Position, pitch/roll and authored yaw remain intact.
- A private 1K atlas (`42414e414e413301`) bakes gentle wrap shading and source
  normal-map detail for the fixed-function material path. This is pre-baked
  illumination, not runtime PBR. No image-generation service or credits used.
- Original pickup collision, count, dropped-banana movement, respawn and
  disappearance code are untouched. All existing vegetation assets remain
  unchanged; this build also contains the current checkout's terrain work.

## Reproduce and checks

Run Blender 4.5:

```sh
Blender -b -t 6 --factory-startup --python-exit-code 1 \
  --python runtime-recomp/tools/prepare_banana_model.py -- SOURCE.usdz assets/models/banana
python3 runtime-recomp/tools/validate_static_plant.py assets/models/banana
```

Build using `LOCAL-MAC-BUILD.md`, version `1.0.4-banana3d-preview1`.
All **62 DKR tests passed** on the packaging run. These include mesh seams,
behaviour/HUD gating, full-phase rotation, negative yaw, cycle wrap, stable
identity and transform quantization. Both LODs have zero open edges. The app
passed controller-pak backup recovery, deep/strict signing and all 27 bundled
model-file comparisons.

The native USA 1.1 app launched using a separate configuration copy at the
task workspace's `work/banana1-qa/config`. In Coconut Canyon, the new bananas
were visible in the row ahead of the start. The runtime trace records the same
sprite-156 object identities at phases 16, 32, and 48, using the 2,762-triangle
mesh, plus stable far→near transitions. There were no replacement-fallback
entries at this checkpoint. The original HUD banana remained visible, and the
first row disappeared as AI racers passed. Direct player-pickup and dropped-
banana visual checks are not yet confirmed; unchanged gameplay code alone is
not a claim of exhaustive course/vehicle testing.

Task workspace outputs:

- `outputs/DKR-R-3D-Bananas-Preview1.app`
- `outputs/DKR-R-3D-Bananas-Preview1-macOS.zip`

Archive SHA256:
`36afe2e34e76144e551be219059a946876ba7bb90828dbea21f0dcf05f4b1577`.
No ROM or external HD pack is included. Changes are uncommitted.

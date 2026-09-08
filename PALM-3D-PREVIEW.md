# 3D palm replacement preview

The current model replaces this original model with the repaired Tropical
Palm Haven. See `HAVEN-PALM-3D-PREVIEW.md` for its current geometry, material,
attachment alignment and verification. The original Gold Palm notes below
document the earlier Preview 1–10 pipeline.

Local experimental Mac build using the user-supplied Meshy Tropical Gold Palm.
No ROM modifications. The existing Retina app is kept.

## Use

Use the Modern presentation profile. 3D palms are on by default in this preview.
Press **F8** in-game to compare the original sprites and 3D replacements instantly,
or use the checkbox in Settings → Graphics → Texture Packs. The switch is
session-only. `DKR_PALM_3D=0` starts with original sprites.

The existing HD pack remains enabled independently. A private 2048×2048 atlas
never overwrites a retail texture. Preview 10 adds offline wrap lighting, local
occlusion and gloss using the original GLB surface and tangent normal map.
Normal/roughness maps are not evaluated by the N64 material pipeline at runtime;
the highlights are baked, not camera-dependent PBR.

## Replacement scope

- Sprite 115, PalmPlant: complete tree, ground pivot, authored y=2..68 envelope.
- Sprite 114, PalmTreeTop: canopy attached to the existing level trunk.
- Sprite 116, PalmTreeTopChea: smaller canopy with its own dimensions.
- Other trees, static level geometry, collision, sounds and scenery updates are
  unchanged. The replacements follow the original object's yaw, tilt and scale.

The mesh is submitted in the original RT64 world pass with its camera, depth
target, fog, scissor, primitive color/opacity and object lifetime identity.
Object transforms are copied into the immutable graphics-task sidecar, not read
from live guest memory on the renderer thread. All sprite tiles are suppressed
only after the replacement texture is GPU-ready; missing/invalid assets fall back.

The full tree has 20,000 near / 5,000 far triangles. Canopies have 19,476 / 4,919.
The 3,077,476-triangle input is preserved. Coincident glTF seam vertices are welded
before decimation, retaining per-corner UVs. Both full LODs have zero open edges;
canopy openings are confined to the trunk cut below the fruit. No fruit seams
remain. The cut at normalized height 0.50 is the attachment pivot.

The bridge also fixes layout-marker dispatch: sprite/frame variants 26..31 must
not be treated as UI markers unless their mode is World/StaticAuto. The old
collision caused aspect-scope errors and prevented affected canopies from drawing.

## Reproduce assets

From the repo root (Blender 4.5 was used):

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
  --python runtime-recomp/tools/prepare_palm_model.py -- \
  '/path/to/Meshy_AI_Tropical_Gold_Palm_0908122657_texture.glb' assets/models/palm
python3 runtime-recomp/tools/prepare_palm_canopy.py assets/models/palm
python3 runtime-recomp/tools/validate_palm_mesh.py assets/models/palm/*.dkrmesh
```

For the Preview 10 material only, keep the prepared meshes and bake to a fresh
directory (the helper checks the source SHA and copies all four meshes exactly):

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 6 --factory-startup \
  --python-exit-code 1 --python runtime-recomp/tools/rebake_palm_material.py -- \
  '/path/to/Meshy_AI_Tropical_Gold_Palm_0908122657_texture.glb' \
  assets/models/palm '/path/to/fresh-palm-bake'
```

The `tropical-palm-gold-wrap-gloss-v1` profile follows the user-supplied
`exec-7646d356-b0c3-4cbe-88e9-8a5d0c9d672b.png` reference: yellow-green patterned
fronds, gold fruit highlights, and dark local overlaps. It rebakes no other
plant family, regenerates no LODs, and changes no attachment or motion code.
The reference's background is not inserted into the game. Source GLB/USDZ,
prior apps and the original unlit atlas are preserved.

Then use the documented Mac build with `DKR_RELEASE_VERSION=1.0.4-palm3d-preview1`.
Model binaries/atlas are optional; builds without them keep the original sprites.
`DKR_TRACE_PALM_3D=1` writes bounded draw/identity diagnostics to the runtime log.

## Validation boundaries

Mesh parser/limits, finite transforms, supported IDs, immutable sample copying,
LOD assets, toggle/fallback policy and layout-marker separation have automated
coverage. Mesh seam validation independently checks all four shipped LODs.
Live checks use copied configurations, not the user's saves. This is still a
preview: every course, split-screen layout, collision animation and dense-scene
performance combination has not been exhaustively checked. The tree-motion fix
uses fixed local mesh coordinates and one stable per-object transform identity
across both LODs. Vertex interpolation is disabled for these rigid meshes;
camera and object-transform interpolation stay enabled. See TREE-MOTION-FIX.md.

## Preview 10 package verification

All 60 DKR tests, controller-pak recovery and deep/strict signing passed on the
first package run. The four palm meshes are byte-identical to Preview 9;
near/far full-tree boundary counts remain zero, and the canopy cut has 46/17
boundary edges with zero fruit boundaries. All other plant assets are unchanged.
All 22 model files in the delivered bundle match the source asset directories.
Native unlit-versus-baked, back and side renders were inspected.
The USA 1.1 native launch recorded sprite 114/115 draws with no logged plant
sprite fallback and exited cleanly. A close-up gameplay capture of the updated
palm was not obtained before the session ended; that visual check remains open.

Historical deliverables in the local QA workspace's `outputs/` (not tracked):

- `DKR-R-3D-Plants-Preview10.app`
- `DKR-R-3D-Plants-Preview10-macOS.zip`
- `Original-Palm-Material-Before-After.png`

Archive SHA256:
`aa5d41043ff98027e630aa515328a8de65322aa844c0f155945e12d107b7ac2f`.

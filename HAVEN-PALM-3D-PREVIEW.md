# Tropical Palm Haven — canopy stem fix, Preview 13

## Current canopy-only fix

Sprites 114 and 116 retain the level's curved trunks. Their replacement crowns
now have the complete green basal stem removed, including the small collar
that remained after an experimental higher flat cut (Preview 12). Sprite 115's
full palm mesh and its green trunk are unchanged, as is the shared texture atlas.

`trim_haven_canopy.py` uses a narrow tapered Boolean recess around the basal
stem, ending at source y=0.61 inside the fruit junction. This avoids slicing
horizontally through the hanging pineapple lobes. Newly exposed recess faces
reuse nearby gold fruit UV patches; no texture image is edited. The resulting
canopies are closed surfaces, with no boundary, over-shared, or reversed edges.
Near/far canopy counts are 15,982 / 5,150 triangles.

The crown is translated to the existing (-0.010, 0.50, 0.0025) runtime attachment
pivot. Existing curved-trunk detection, overlap, rotation, scale, interpolation,
and collision are unchanged. Both full-tree LODs remain byte-identical to
Preview 11. Only the two canopy meshes and their manifest change in this fix.

The three checks in `test_trim_haven_canopy.py` verify closed consistent surfaces,
complete stem removal, the unchanged pivot, and preservation of 15,537 / 4,824
outer fruit/frond triangles and their UVs. Front and reverse schematic trunk-join
renders were reviewed; these are geometry previews, not in-game screenshots.

For this version, replace step 4 in the historical pipeline below with:

`Blender -b -t 6 --factory-startup --python-exit-code 1 --python runtime-recomp/tools/trim_haven_canopy.py -- RUNTIME_DIR`

Then run `python3 runtime-recomp/tools/test_trim_haven_canopy.py` after copying
the generated canopy meshes/manifest into `assets/models/palm`. Release version:
`1.0.4-plants3d-preview13`. Accepted intermediate assets: task workspace
`work/haven-runtime-v5`. Preview 12 was an intermediate flat-cut build, not the
final fix for the remaining collar.

## Preview 11 source and repair history

Replaces the original palm family, sprites 114/115/116, using the user's
`Meshy_AI_Tropical_Palm_Haven_0908164622_texture.usdz`. The original input is
unchanged. Source SHA256:
`37e0bcfefcf54c51c7659ea1a24e1f9d95197563daaf0e884945e072ea5fb635`.

## Repair evidence

The 5,197-triangle source had 30 boundary edges, 47 edges shared by more than
two faces, and inconsistent winding after welding coincident UV seams.
Underside renders showed sharp fins and distorted triangular pineapple faces.

The first pass removed 17 stray fin faces and smoothed 270 fruit vertices with
a maximum displacement of 1.2% of tree height. It retained the three fruit
lobes and source corner UVs. Direct patching experiments still produced
orientation conflicts at the tangled leaf roots and were not shipped.

The accepted second pass reconstructed the surface at 0.15%-height voxel
spacing, removed tiny disconnected remesh debris, and simplified to 16,000
triangles. The original albedo was reprojected onto a fresh 2K UV atlas in
Blender. This is a surface reconstruction, not a claim of unchanged topology
or pixel-identical texture detail. The reimported GLB has one connected
component, zero boundary edges, zero over-shared edges and consistent winding.

The `haven-palm-repaired-gloss-v1` material bakes bright yellow-green fronds,
gold fruit, wrap light and local occlusion into the atlas. Restrained albedo-
derived relief supplies scale detail on the reconstructed fruit. Lighting is
pre-baked, not camera-dependent PBR. No background or glow is added.

## Runtime mesh and attachment

Full-tree LODs: 16,000 / 5,000 triangles, both closed and consistently oriented.
Canopy LODs: 15,838 / 4,966 triangles, with 30 / 16 boundary edges confined to
the intended y=0.50 trunk cut. All four assets have zero fruit boundaries,
over-shared edges or inconsistent edge winding. `validate_palm_mesh.py` now
enforces these properties instead of checking only fruit holes.

The new canopy cut centers are translated to the existing runtime pivot
(-0.010, 0.50, 0.0025), preserving the earlier curved-trunk attachment fix.
Full-tree ground placement, original scale/rotations, collision and motion
interpolation code are unchanged. The other three plant families are unchanged.

## Reproduce

Use Blender 4.5 with fresh output directories at each step:

1. `repair_haven_palm.py -- SOURCE.usdz FIRST_PASS_DIR`
2. `reconstruct_haven_palm.py -- FIRST_PASS_DIR/repaired.blend CLEAN_DIR`
3. `prepare_static_plant.py -- CLEAN_DIR/repaired.glb RUNTIME_DIR 50414c4d33440001 --bake-lighting --lighting-profile haven-palm`
4. `python3 prepare_palm_canopy.py RUNTIME_DIR --align-cut`
5. `python3 validate_palm_mesh.py RUNTIME_DIR/near.dkrmesh RUNTIME_DIR/far.dkrmesh RUNTIME_DIR/canopy-near.dkrmesh RUNTIME_DIR/canopy-far.dkrmesh`

Blender scripts are under `runtime-recomp/tools`; invoke with
`Blender -b -t 6 --factory-startup --python-exit-code 1 --python SCRIPT -- ARGS`.
Build with `LOCAL-MAC-BUILD.md`, release version `1.0.4-plants3d-preview11`.

The accepted repair assets and audits are in the task workspace's
`work/haven-repair-v1`, `work/haven-reconstruct-v1`,
`work/haven-reconstruct-v1-audit` and `work/haven-runtime-v1` directories.
Front/back and underside render checks covered the repaired geometry; the
runtime atlas was inspected on the full-tree and canopy assets.

## Packaged verification

All 60 DKR tests passed on the full packaging rerun. The initial run hit the
previously observed DirectSession frame-ledger queue-overflow assertion; its
isolated retry and the subsequent full run both passed. Controller-pak backup
recovery, deep/strict app signing and all 22 bundled model-file comparisons
passed. Six palm files changed versus Preview 10; the other 16 files, including
the three other plant families, are unchanged.

The native USA 1.1 RT64 Metal intro was visually inspected. The new golden
pineapple clusters and glossy crowns were visible on the existing curved
trunks, with connected joins and no obvious spikes/holes in the inspected
view. The bounded draw trace confirmed sprites 114/115 and no plant sprite
fallbacks at that checkpoint. This does not claim every course/camera is tested.
The screenshot is `work/plants11-qa/haven-palm-intro.png` in the task workspace.
The QA session uses a separate configuration copy under `work/plants11-qa`.

Historical deliverables in the local QA workspace's `outputs/` (not tracked):

- `DKR-R-3D-Plants-Preview11.app`
- `DKR-R-3D-Plants-Preview11-macOS.zip`
- `Tropical-Palm-Haven-Repaired.glb` (clean geometry/reprojected albedo)
- `Tropical-Palm-Haven-Preview.png` (game-material full tree/canopy preview)

Archive SHA256:
`ce8825ac921dd7150f79b4f44f20cf5071f49b3b9b596d9c67d99485a394e75e`.
The app/archive contain no ROM or external HD pack. Original inputs and previous
apps remain available. No Meshy credits were used.

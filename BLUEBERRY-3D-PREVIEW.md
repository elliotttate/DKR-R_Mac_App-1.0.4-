# Blueberry plant replacement — material revision

Uses the smaller user-supplied `Meshy_AI_Blueberry_Verdant_Blo_0908131752_texture.usdz`.
The earlier 95 MB / 3,028,934-triangle file is not bundled or used. The selected
6.7 MB USDZ has 3,107 triangles, all retained at both near and far distances.
Only coincident seam vertices are welded; face-corner UVs are preserved.
The original USDZ and unlit 2048×2048 atlas remain available. No Meshy API jobs
or credits are used.

## Glossy material pass

After approving the Preview 7 round tree, the user requested the same treatment
for this plant, with their side-by-side screenshot as the appearance reference.
The new offline Blender profile is `blueberry-gloss-wrap-v2`. It separates blue
berries/stems from green foliage by source albedo, keeps the dark leaf markings,
adds local overlap shading, and bakes four-sided wrap lighting and highlights
using the supplied normal map. Berries receive a tighter white highlight and
broad sheen; leaf highlights are softer and subdued over the dark markings.
Only the bake's lighting normals are smoothed; no vertices are subdivided or
moved. The approved rubber-tree atlas and the palm assets are not rebaked.

These highlights are fixed texture lighting, not camera-dependent PBR. The
source model's angular berry silhouette and leaf arrangement remain; a material
pass is not an exact geometry reconstruction of the sprite.

Material validation preserved byte-identical near/far mesh binaries relative to
the unlit preview: 3,107 triangles, 4,836 runtime vertices and eight source open
edges. The approved Preview 7 rubber-tree and palm files were hash-checked as
unchanged. Native unlit renders of the exported atlas were checked from the
front, back and side, including a before/after comparison on the same mesh.
The new profile keeps the berries cobalt blue with round white highlights;
dark leaf markings have much lower gloss than the surrounding bright blade.

The Preview 8 package also includes the separate task's motion-fix code; see
`TREE-MOTION-FIX.md`. That task passed its own bounded testing, but route-specific
user confirmation of the intermittent flick remains pending. This material pass
does not claim to diagnose or universally resolve flicker.

Preview 8 passed all 59 DKR tests on the full package rerun, plus the controller-
pak self-test, deep/strict signing verification and all 17 bundled model-file
hash comparisons. The initial run hit the pre-existing intermittent DirectSession
frame-ledger overflow assertion; its isolated retry and full rerun passed.
The exact Preview 8 app was launched with a copied configuration and the user's
USA 1.1 ROM. The user approved the result ("Looks good!"). The bounded runtime
trace confirmed the 3,107-triangle blueberry draw path and detail-level
transitions with no sprite-fallback entries during the observed intro period.

## Runtime mapping

Both supplied USA ROM revisions identify BlueBerryBush as object header 43,
scenery behavior 2, sprite 108. The six original sprite tiles span y=0..113
relative to their anchor; the replacement uses a normalized ground pivot and
113-unit height before the original object's scale and rotation. This replaces
only that plant. It does not change scenery behavior, collision, or ROM data.

The existing palm replacements and HD texture pack remain independent. Each
model family has its own private atlas hash and optional asset loading; missing
blueberry assets cannot disable working palms, and missing palm assets cannot
disable working blueberries. Original sprites remain visible until their own
replacement atlas is GPU-ready. F8 toggles all supported 3D plants for comparison.

The renderer continues to use immutable submitted object transforms and the
original world/depth/fog pass. The N64 material path reads the baked color atlas;
it does not evaluate normal, metallic or roughness maps at runtime. This is a
experimental Mac preview build.

## Reproduce

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 8 --factory-startup \
  --python-exit-code 1 --python runtime-recomp/tools/prepare_static_plant.py -- \
  '/path/to/Meshy_AI_Blueberry_Verdant_Blo_0908131752_texture.usdz' \
  assets/models/blueberry 424c554542330001 --preserve-topology \
  --bake-lighting --lighting-profile blueberry
python3 runtime-recomp/tools/validate_static_plant.py assets/models/blueberry
```

Build with the environment in `LOCAL-MAC-BUILD.md` and a fresh release version.

## Original preview 2 verification

58 DKR tests and the controller-pak round-trip/backup-recovery self-test passed.
Tests cover separate texture identities, family-specific fallback, ground-pivot
and rotation calculations, parser bounds, stable snapshots, and palm regression.
Both blueberry meshes retain exactly 3,107 triangles and are byte-identical.
Their eight open edges match the supplied low-poly mesh's imported baseline;
conversion did not add open seams. These are below the berries in the central
leaf/stem region. The model is not claimed to be fully watertight.

Blueberry objects occur in the character-select scene, Central Area, Fossil
Canyon, Ancient Lake, RC Car Sequence, and several Tricky/cutscene maps. This
list comes from the ROM object maps; it is not a claim of live QA in every scene.

The packaged USA 1.1 app was launched with a copied QA configuration and the HD
pack enabled. Live checks showed the new plants in the intro/RC Car Sequence
and Central Area fly-through. F8 visibly restored the original HD blueberry
sprites and then the 3D models. Ground attachment and the supplied model's
silhouette were checked; the session was left running with 3D plants on. This
does not certify every course, collision interaction, or split-screen layout.

The original USDZ files, earlier app bundles, ROMs, and user saves are preserved.

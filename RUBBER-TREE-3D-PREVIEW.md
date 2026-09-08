# Round-leaf tree and palm attachment update

Local experimental preview 7, building on the existing Mac/HD/palm/blueberry work.
No ROMs or user saves are modified. These notes document the local preview.

## Current tree: supplied Emerald Umbrella USDZ

The user preferred `Meshy_AI_Emerald_Umbrella_Tree_0908134447_texture.usdz` to
the first generated model. Preview 6 uses that supplied file, not the earlier
2,869-triangle GLB. Source SHA256:
`0d5a71277f969a21efb8c73ad3386677908d2bea9a3ed3e59e167a381bd9248f`.

All 5,101 triangles are kept at both distances. Near/far mesh binaries are
identical. Welding coincident geometry preserves the eight source boundary
edges and face-corner UVs; the supplied file is not modified. The lit and unlit
conversions are byte-identical geometrically.

The original game path consumes only a base-color atlas, not the USDZ's normal
and roughness maps. That made the first replacement look flat and neon green.
Preview 6's first offline bake overcorrected: its directional lighting left the
back-facing foliage and bark too dark and matte. The user's side-by-side with
`download (1).png` is the appearance reference, not a request to darken the
entire model. Preview 7 uses the same source albedo and normal map with brighter
four-sided wrap lighting, local ambient occlusion, stronger foliage-only gloss,
and separately warmed/lifted bark. The 2048-square atlas is tagged
`bright-sprite-wrap-gloss-v2` in the manifest. Linear-light gains are
(1.20, 1.55, 1.12) for green foliage and (2.05, 1.85, 1.40) for bark;
local AO contributes 0.40 + 0.60*AO, diffuse wrap 0.65 + 0.35*key,
and the AO-masked foliage highlight uses exponent 24 and strength 0.85.

Front, back, and side native-material previews were inspected against white.
This restores bright green faces and white highlights without changing mesh
positions, UVs, object placement, collision, or other plant families. The
user's HD pack is untouched. These are fixed baked highlights, not
camera-dependent PBR, and there is no added runtime lighting cost. The supplied
mesh still differs from the sprite in canopy density and leaf silhouette;
this material pass does not claim an exact geometric match. No further Meshy
API credits were used for this user-supplied revision.

Preview 7 passed all 59 DKR tests (including DirectSession on this run), the
controller-pak recovery self-test, deep/strict signing verification, and bundle
asset hash verification. The exact packaged app was launched with an isolated
copy of the prior QA configuration. Native Central Area inspection confirmed
the brighter glossy crown and warm trunk. The intro-to-hub trace showed the
5,101-triangle replacement drawing with no sprite-fallback entries during this
bounded check. Screenshot: `work/plants7-qa/bright-tree-in-game.png` in the task
workspace. This is not a claim that all routes or all camera transitions were
tested.

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 8 --factory-startup \
  --python-exit-code 1 --python runtime-recomp/tools/prepare_static_plant.py -- \
  '/path/to/Meshy_AI_Emerald_Umbrella_Tree_0908134447_texture.usdz' \
  assets/models/rubber-tree 5255424252330001 --preserve-topology --bake-lighting
python3 runtime-recomp/tools/validate_static_plant.py assets/models/rubber-tree
```

## Palm attachment correction

The original canopy sprite anchors are not the actual static trunk tips. In the
Central Area, the two small doorway palms have sprite anchors (1865,417,2197)
and (1770,414,1904), but their bark tip centers are approximately
(1869.33,385,2192.67) and (1753,387.33,1908.33). The intro uses different sprite
anchors again, while keeping those same tips. A uniform vertical offset would
not correctly join both trees in both scenes.

The guest-side presentation capture now resolves a nearby compact triangular
bark ring from the current level model, filtering the original palm-bark texture
IDs through the loaded texture cache. Pointer/count/scan bounds are checked.
The result is cached for the object lifetime and copied into the immutable
submitted-frame sample. The renderer never dereferences live guest geometry.
Both canopy LODs use their measured common cut center and a three-world-unit
overlap into the original trunk. Full palms and blueberries retain their old
transforms. A missing/invalid attachment leaves the previous sprite-anchor
placement, rather than snapping to unrelated terrain.

The separate preview 3 app was inspected in the running USA 1.1 game. The two
doorway crowns visibly joined the curved white trunks, and runtime traces
confirmed matching tip coordinates in both intro and playable Central Area.

The earlier brief original-sprite appearance coincided with our recorded F8
off/on comparison; an independent automatic switch has not been established.
F8 remains a deliberate session-only comparison toggle. Preview 4's bounded
reason-coded fallback logging under `DKR_TRACE_PALM_3D=1` identified a separate
issue: private virtual texture tiles could age out while a plant was off-camera,
even though its replacement atlas was preloaded. Their next appearance required
an asynchronous upload, causing brief task-local sprite fallbacks.

Preview 5 warms the three private virtual tiles during boot/menu rendering and
refreshes their cache access record every submitted task while 3D plants are
enabled. This prevents visibility changes from aging them out. It does not pin
or modify any retail/HD textures. Original sprites remain the safe fallback for
missing assets, disabled models, or a replacement pack that has not loaded.

## Round-leaf identity and earlier generation

The screenshot's plant is RubberTree: object header 48, scenery behavior 2,
sprite 113 in both supported USA revisions. It is not SmartieTree (106) or
BeachTree (107). Only sprite 113 is added by this update.

The following generation is preserved for provenance, but superseded by the
user's Emerald Umbrella USDZ in preview 6.
Meshy image-to-3D task: `01a0813c-bf7b-72d6-97b6-a3e8aaed2c87`.
Model: `meshy-t2`, `smart-topology`, target 3000 triangles, textured 2K, GLB,
bottom origin, no PBR maps. The successful task consumed 15 credits.

Source: `DKR-Rubber-Tree-Meshy.glb`, SHA256
`499fcb852661bf61a461c99ec8dc98aa71dda0a3ac41af202095abb7269410bb`.
The actual output has 2,869 triangles and a 2048-square base-color texture.
Both runtime LODs retain every triangle and are byte-identical. The imported
source has 37 boundary edges; conversion preserves that count and introduces
no additional open seams. This is not a claim that the generated mesh is
watertight. Spaces between separate leaves are part of the generated foliage.

The model uses a ground pivot and 115-unit height before authored object scale
and rotation. Collision, object behavior, and level placement are unchanged.
Its private atlas hash is `5255424252330001`. Palm, blueberry and rubber-tree
assets load and fall back independently of one another and the user's HD pack.

## Generation reference and prompt

The built-in image-generation skill was used only to isolate the indicated
tree from the supplied screenshot. That clean reference is saved at
`outputs/rubber-tree-reference.png` in the local QA workspace (not tracked).
The original screenshot and generated source GLB are preserved.

Reference prompt (built-in image tool):

> Use case: background-extraction. Asset type: clean single-tree reference for a low-poly 3D game model. Isolate ONLY the large round-leaf tree indicated by the red arrow in this screenshot. Center that entire tree, from its thin brown trunk base to crown top, on a plain white background. Preserve exactly its distinctive rounded glossy green leaves in radial rosette clusters, roughly spherical crown, slender straight brown bark trunk and branching under the crown, original relative crown/trunk proportions, saturated lime and dark green palette, and stylized game-art appearance. Remove all other scene objects, ground, flowers, sky, palm trees, smaller trees, characters, and the arrow. No text, no pedestal, no shadows on the background, no fruit, no flowers, no new ornaments. Show only this complete tree, upright, fully visible with a small margin; do not crop its trunk or redesign it.

Meshy texture prompt:

> Faithfully match this stylized Diddy Kong Racing rubber tree: glossy rounded bright green leaves arranged in radial rosette clusters, a spherical crown, slim straight brown bark trunk with branches under the foliage. Green leaves, brown wood only. No fruit, flowers, ground or extra objects.

## Reproduce

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
  --python runtime-recomp/tools/prepare_static_plant.py -- \
  /path/to/DKR-Rubber-Tree-Meshy.glb assets/models/rubber-tree 5255424252330001
python3 runtime-recomp/tools/validate_static_plant.py assets/models/rubber-tree
```

Use the build environment in `LOCAL-MAC-BUILD.md` with a fresh release version.
Restrict CTest to `-R '^DKR'`; dependency benchmark targets are not game tests.

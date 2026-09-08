# BeachTree / Neon Palm — Preview 9

Replaces the ribbed-leaf BeachTree with the supplied
`Meshy_AI_Neon_Palm_0908154715_texture.usdz`, using
`exec-33353d86-8baa-4497-968f-e9cf7837b8aa.png` as the appearance reference.
No source USDZ, ROM, save, or external HD-pack files are modified. No Meshy
generation or credits are used. These notes document the local preview.

## Identity and placement

Both supported USA ROMs identify BeachTree as object header 49, scenery behavior
2, sprite 107. It is not the Tropical Gold Palm family (114/115/116), RubberTree
(113), or SmartieTree (106). The reference matches the three BeachTree texture
tiles with Rice hashes A2BF10CA, 6C9CAB68 and 368A3F45.

The sprite anchor is y=122. Its first tile begins at y=5; its last tile begins
at y=67 and is 61 pixels tall. The original sprite builder uses top=anchor-posY-1
and bottom=anchor-posY-height, giving a -6..116 vertex envelope. The new mesh
uses height 122 and foot offset -6 before the original object scale and rotation.
Collision, level data, object behavior, authored yaw/tilt and lifetime identity
are unchanged. The two hub doorway examples are around (-2623,517,1402) and
(-2413,531,1832); the intro uses the same tree family.

The family has its own optional assets and private atlas hash 4245414348330001.
It loads/falls back independently of palms, blueberries and round trees. Its
virtual texture tile receives the same warm-up/residency handling, and its
rigid mesh uses the existing stable object-transform interpolation with vertex
interpolation disabled. F8 intentionally toggles all supported plant models.

## Model and material

Source SHA256: `9c03c3b08cadd9646a04ca571ad4e5697536e1d2fb16a781e28a98333ca345cd`.
All 8,090 triangles are retained at both near and far distances. Welding
coincident seam vertices preserves face-corner UVs and the 12 source open edges;
the converter introduces no additional open seams. This is not a watertightness
claim. The runtime mesh has 14,758 UV/normal-split vertices.

The `beach-tree-gold-gloss-v2` offline Blender profile makes the sculpted ribs
glossy green and the crown/trunk warm gold. It uses four-sided wrap lighting,
local occlusion and smooth lighting normals during the bake. This USDZ has no
normal map; restrained albedo-derived bump adds local relief to the painted
golden scales/bark without moving mesh vertices. The output is a pre-lit 2K
atlas, not view-dependent PBR or an actual glowing light. The reference's black
background and halo are not inserted into the game. Existing approved plant
assets are not rebaked.

## Reproduce

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b -t 6 --factory-startup \
  --python-exit-code 1 --python runtime-recomp/tools/prepare_static_plant.py -- \
  '/path/to/Meshy_AI_Neon_Palm_0908154715_texture.usdz' \
  assets/models/beach-tree 4245414348330001 --preserve-topology \
  --bake-lighting --lighting-profile beach-tree
python3 runtime-recomp/tools/validate_static_plant.py assets/models/beach-tree
```

Build using `LOCAL-MAC-BUILD.md` and a fresh release version. New tests cover
the independent family, texture identity, authored foot/top placement, rigid
transforms, parser/mesh limits and preserved source boundaries. A source map
audit also finds this tree in Fossil Canyon, Pirate Lagoon, Ancient Lake,
Crescent Island and other maps; those are asset matches, not live QA claims.

## Packaged verification

The Preview 9 full package run passed all 60 DKR tests on its first attempt,
including BeachTree placement/identity and preserved-boundary validation.
Controller-pak round-trip/backup recovery and deep/strict app signature checks
passed. All 22 bundled model-family files match their source assets; the 17
previously approved files also match Preview 8 byte-for-byte. Original near/far
BeachTree geometry matches the unlit conversion. Native Blender material
inspection covered front, back and side views.

Historical deliverables in the local QA workspace's `outputs/` (not tracked):

- `DKR-R-3D-Plants-Preview9.app`
- `DKR-R-3D-Plants-Preview9-macOS.zip`
- `Beach-Tree-Material-Before-After.png` (unlit left, baked right)

Archive SHA256:
`7edaa6a894f64b181c2531aa456b6b7feab522f910a3dbc135ac301a5989335c`.

The live QA session uses a separate copy of the Preview 8 configuration at
the local QA workspace's `work/plants9-qa/config`.
Normal app launch still uses the regular DKR-R configuration. Neither the app
nor archive includes a ROM or the external HD texture pack.

Native RT64 Metal QA with the supplied USA 1.1 ROM confirmed the new 8,090-
triangle sprite-107 draws during the intro and the golden pair beside the hub
bridge. The observed hub view showed seated trunks and connected crowns;
existing round trees, blueberries and curved palms remained present. The
runtime trace recorded both directions of BeachTree near/far transitions with
vertex interpolation disabled and zero plant sprite fallbacks at the QA
checkpoint. This is bounded intro/hub evidence, not coverage of every map or
camera route. Screenshots are `work/plants9-qa/beach-intro.png` and
`work/plants9-qa/beach-hub.png` beneath the task workspace.

# Balloon mesh preview 2

Uses the supplied `Meshy_AI_Starry_Red_Balloon_0908130421_texture (2).usdz`.
SHA-256: `adaa2d690ae752f8eb92b0acd58b09114ec89021c1202d33b6734294ebd3c720`.
The source file is unchanged. Its old image maps are not used.

## Replacement artwork

One welded, smooth-normal mesh, 3,148 triangles, with seven freshly baked 2K
atlases. Preview 2 rebuilds the material against the supplied
`balloons-combined.png` (SHA-256
`74d31ab60f214cc48d66a8550c60aaaf3a391c90ce9cae85f4cf385396e18242`).
Large staggered stars, sharply edged rainbow waves, blue-white mirrored silver
tiles and the broad blue zigzag/triangles on gold replace Preview 1's simplified
procedural patterns. These are reference-guided reconstructions, not an exact
pixel copy of the sprite.

The built-in image-generation tool produced seven flat surface maps. Photoshop
MCP offset them by half their width, the image tool repaired the center seams,
and Photoshop composited those repairs with the already-matching outer edges
locked to the originals. Full-circumference latitude/longitude mapping bakes
them to the existing non-overlapping UVs with a 12-pixel gutter. Knots/strings
receive their own finish. This is glossy baked base color for the unlit RT64
replacement path, not dynamic PBR. No mirrored-front projection or blurred
longitudinal crossfade is used in the final material.

All four mesh files are byte-identical to Preview 1. No placement, gameplay,
object mapping, animation, vegetation, or banana code changed in this pass.

Gold/silver reuse the body and knot with a closed cut above the dangling
string, 2,842 triangles. Both near/far files are identical for each geometry,
preventing a small round item's silhouette or texture seams from changing
at the LOD boundary. All four files are closed, without nonmanifold edges.

## Exact scope

Verified against both user-supplied USA 1.0 and USA 1.1 ROM asset tables:

| Sprite | Use | Fresh finish | Behavior |
| --- | --- | --- | --- |
| 147 | Boost | Royal blue / yellow stars | 17 |
| 148 | Missile | Vivid red / yellow stars | 17 |
| 149 | Trap | Emerald green / yellow stars | 17 |
| 150 | Shield (called yellow in the game) | Yellow / purple stars, per supplied HD reference | 17 |
| 151 | Magnet | Green-blue-red-yellow-green wavy bands | 17 |
| 154 | Collectible | Gold / blue zigzag and separate triangles | 77, gold sprite cutscene actor 50 |
| 155 | Adventure Two collectible | Blue-white mirrored tile grid | 77 |

Header 113 owns weapon models 147–151; header 283 owns 154/155. Adventure Two
reassigns the collectible model in the original game code. Header 260 is the
separate gold cutscene **sprite**. HUD 81/82, string particles 99, unused gift
sprites 152/153 and burst effect 176 are explicitly excluded. This is a
sprite-to-mesh replacement: the separate already-polygonal `PolyGoldBaloon`
cinematic actor (header 179, model IDs 204/205) is not rerouted by this preview.

No game behavior code was changed. Collection, power-up upgrades, cheats,
invisibility, opacity, respawn scaling and path-following retain guest timing.
Balloons do not inherit banana rotation. Their original object transforms,
size and vertical offsets are retained. Private per-color hashes prevent any
HUD/texture-pack replacement collision. Missing models/atlases fall back to
the original sprite before any authored triangles are suppressed.

## Reproduce

Run Blender in background mode with `runtime-recomp/tools/prepare_balloon_model.py`:

```text
blender -b --python runtime-recomp/tools/prepare_balloon_model.py -- SOURCE.usdz assets/models/balloons REVIEW.blend balloons-combined.png art-source/balloons/wraps
```

Build/package with the existing `Build-macOS.sh` workflow and
`DKR_RELEASE_VERSION=1.0.4-balloons3d-preview2`. Packaging includes the new
`assets/models/balloons` directory but no ROM or user HD texture pack.

The final wraps, layered Photoshop seam composites, and exact prompts are in
[`art-source/balloons/`](art-source/balloons/README.md). The original reference
and supplied USDZ remain external inputs; neither is required to run the game.
`manifest.json` records the source model, reference and seven source-wrap hashes.

## Preview 2 verification

- Release packaging and all 63 DKR tests passed; packaged app signature verifies.
- Packaged model assets match the checkout; all four balloon meshes are
  byte-identical to Preview 1, with closed topology and identical LOD bytes.
- Rendered 72 views through a full 360-degree turn with the exported runtime
  meshes/atlases. Inspected eight cardinal/diagonal views of all seven colors.
- Verified the outer 64-pixel strips of each final source wrap are exactly
  equal to adjacent original source columns, preserving the repaired wrap join.
- Launched the exact Preview 2 app under native Metal with the USA 1.1 ROM.
  Attract-mode traces confirm all five weapon colors (147–151) draw with no
  sprite-fallback messages in the observed session, including near/far draws.
  The saved native screenshot visibly shows the new blue/yellow star finish.
- Gold/silver were inspected in model review, not their live collection scenes.
  Pickup/respawn behavior was not re-tested in this texture-only pass.
- The earlier Preview 1 gameplay session was left open; Preview 2 uses its own
  copied test profile. No source model/reference files, commits or publications
  were changed.

## Preview 1 verification (prior build)

- All 63 DKR tests passed in the release packaging run.
- New tests cover all seven exact behavior gates, HUD/pop exclusions,
  unique texture hashes, trace slots, optional family loading, original
  placements, respawn scales and no invented spin.
- Geometry validation checks every file, normalized bounds, atlas coverage,
  closed topology, triangle budget and identical LOD bytes.
- Front/back renders use the actual exported `.dkrmesh` files and atlases,
  not a separate physically lit material preview.
- Packaged app signature verifies; bundled model assets match the repo.
- Native USA 1.1 attract-mode races visibly draw the new balloons. Runtime
  traces confirm all five weapon-color IDs and near/far transitions, with
  no sprite-fallback reports in the observed session.

Direct player pickup/respawn and the gold/silver collection sequences were
not yet visually exercised. The running preview is left open for that check.
Existing terrain, vegetation and spinning-banana work is preserved. No commit
or publication was made.

# Terrain detail

Optional Modern-profile terrain refinement for the native RT64 Mac build.
The implementation is in `runtime-recomp/src/game/terrain_detail.{hpp,cpp}`;
the F3DDKR bridge submits its cached triangles alongside the original game
materials. Existing HD texture packs and the 3D plant replacements remain
compatible.

## Controls

- **F7:** Original → Surface detail → Surface + geometry.
- **Shift+F7:** material and boundary diagnostics.
- **Mods / Hacks → Terrain Detail:** mode, strength, quality, and grass/stone
  decoration controls. Settings are saved with the normal application settings.
- Material colors: grass green, rock orange, water blue, special paths yellow,
  protected geometry purple. Boundary diagnostics mark fixed joins red.

## Geometry and material contract

The level decoder reads the immutable graphics-task RDRAM snapshot. It uses
the revision-selected level/texture addresses, original surface types, texture
identities, segment and triangle addresses, and render flags. Grass, sand, snow,
earth, ice, selected hard track surfaces, and audited natural rock are refined. Water,
animated textures, decals, cutouts, translucent surfaces, hidden geometry,
special pads, and unrecognized materials are excluded from displacement.
The Central Area cliff textures are 306/307 and its main grass is 214.
The complete audited rock family is listed in `terrain_detail.cpp::classify`.
The Wizpig statue, entrance trim, yellow paths, and existing tree trunks have
separate materials and are not included in that rock family.

All selected triangles use the same subdivision level (4/8/16 per edge), so
shared edges remain conforming across source segments and texture UV seams.
One shared displacement direction is constructed per source position. Open
edges and joins to excluded surfaces remain fixed, with displacement fading
out near those joins. Ground height remains on the original surface; horizontal
movement shared with a cliff can add contour to a flat grassy rim. Sloping
ground joins and traversable rock shelves are pinned. Ice and hard track
surfaces are pinned in all axes. Fractional positions are
submitted through RT64's extended position stream, avoiding integer rounding
steps on sloped ground. The game collision mesh and simulation are untouched.

Fixed world-coordinate noise supplies broad color variation, rock bulges,
shallow strata, and fine shade variation. Derived surface slopes add local
lighting relative to the authored vertex colors. The original UV values use
DKR's exact 10.5 coordinate convention, independent of the generic RSP texture
scale. This is procedural mesh/vertex shading, not a PBR material replacement.
Sand adds low-contrast wind ripples; snow uses softer drift variation; earth
uses broader mottling. Ice and hard track surfaces receive restrained shading
only, without displacement. Lava, special pads and painted route markers stay
excluded. Texture 96's soil treatment is restricted to forest levels because
the same texture is used on the hub's authored Wizpig statue.

Sparse opaque grass blades and stones are placed inside grass faces adjoining
natural rock, with deterministic placement. Decorations shrink away smoothly
at distance. Terrain topology stays fixed while the camera moves; it inherits
the existing world-matrix interpolation without cross-frame vertex morphing.
The source game still controls segment visibility, fog, depth, and viewport.

## Development checks

`DKRTerrainDetail` covers material exclusions, deterministic noise, shared
segment/UV seams, protected joins, grass height, and malformed snapshot input.
The test executable additionally accepts a local captured scene:

```sh
build/dkr-runtime-macos/DKRTerrainDetailTests SNAPSHOT.bin MODEL_HEX CACHE_HEX TEXTURE_COUNT [QUALITY] [LEVEL_ID]
```

This validates the decoded scene's coincident samples and grass heights, and
reports source/detail counts, decorations, and cache-build time. Pass LEVEL_ID
for scene-specific material exceptions (notably the forest soil/statue texture).

`DKR_TRACE_TERRAIN=1` logs selected materials, cache builds, and submission
statistics. `DKR_TERRAIN_DUMP=/absolute/path/prefix` additionally saves a local
submission snapshot; snapshots contain game data and must never be distributed.
`runtime-recomp/tools/inspect_terrain_snapshot.py` produces a local material
contact sheet from such a capture.

For repeatable offline driving checks, set `DKR_TERRAIN_QA_ROUTE` to a local text
file and press F6. Each row is `simulation_ticks N64_button_bits analog_x analog_y`.
For example `30 0x8000 0 0` holds acceleration for 30 simulation ticks. A subsequent
`30 0 0 0` releases it. The driver is inactive without this explicit environment
variable and never overrides online input. It exercises the normal game input
boundary rather than editing the game state or synthesizing OS input.
For split-screen checks, `DKR_TERRAIN_QA_PLAYERS=2` exposes two offline test
ports. A route can start with `player 1` to address the second port; omission
addresses player zero. Neither setting changes controller routing online.

## Preview 4: whole-game coverage

Current package: **1.0.4-terrain-preview4**, native arm64 Metal. It applies
material-specific detail across every playable course, hub, boss track and
battle arena, including the title-screen demonstrations and space courses.
This does not mean every polygon is displaced: artificial structures,
unrecognized materials, water, lava and protected joins remain unchanged.

The owner-ROM audit covers **65 level entries / 51 distinct level models** in
each of US v1.0 and v1.1. All three quality settings pass for every entry:
**390 mesh/quality cases, zero failures**. Every racing and battle entry has
nonzero coverage. Menu backdrops and the trophy podium legitimately have no
eligible natural terrain. Both full 61-test package validation and strict
ad-hoc signature verification pass.

The audit fixtures are generated by `export_terrain_audit.mjs` from a verified
local ROM. They relocate the level model and load its texture metadata using
the game's layout and vertex-alpha convention. They are **synthetic decoder
fixtures, not gameplay captures**. Their source/selected/shared-sample counts
match the live hub captures from both revisions and all 27 live title-demo
scene/quality results, including decoration counts.
`audit_all_terrain.mjs` checks every fixture for finite coordinates, conforming
coincident samples and preserved ground heights at Low/Balanced/High.
`audit_startup_terrain.mjs` also passes all 27 checks on the nine actually
captured title-demo scenes. This is not a claim that every course has been
driven end-to-end in the new app.

The final packaged app's opening sand/cave montage, Greenwood Village and
Frosty Village were visually inspected with moving cameras. Original, Surface
and Geometry switching was exercised and Geometry restored. Ground/kart
contact, snow slopes, cliff joins, buildings and painted markers remained
intact in these inspected views. The user's selected High quality / 150%
strength was retained. Only one instance was used for the final inspection.
The final one-tick neutral QA route completed and normal input was restored;
the packaged app was left running in the startup sequence. Normal launches
do not enable QA routing. The user's original saves remain untouched.

Examples from the US v1.1 audit (whole scene, not per-frame visibility):

| Scene | Selected source triangles | Balanced detail triangles |
|---|---:|---:|
| Central Area hub | 2,083 | 133,312 |
| Opening montage | 1,215 | 77,760 |
| Frosty Village | 1,382 | 88,448 |
| Hot Top Volcano | 718 | 45,952 |
| Spaceport Alpha | 180 | 11,520 |
| Spacedust Alley | 2,289 | 146,496 |
| Wizpig 2 | 1,913 | 122,432 |

The largest High-quality case contains 586,240 refined triangles, below the
1.5-million cache cap. High generates four times as many refined triangles as
Balanced; the earlier Preview 3 timing observations below are not Preview 4
performance guarantees.

Local audit data and logs are under `build/terrain-qa/all-levels-v77/`,
`all-levels-v80/`, their `*-results.jsonl` / `*-results.log` siblings, and
`startup-final-audit.jsonl`. The full package log is `build/terrain-qa/package4.log`.
These fixtures and contact sheets contain game data and are never distributed.
The scripts themselves contain no ROM or texture assets.

Artifacts: `dist/DKR-R-1.0.4-terrain-preview4-macOS-arm64/DKR-R.app` and the
adjacent ZIP. SHA-256:
`739d0083382f55f1ae8484a2ea2c49b0ae8a6b005ce4a6f25d7080ac2a23580c`.
Preview artifacts are local validation outputs, not tracked repository files.

The later source-publication check passed 60 of 61 DKR tests, including all
terrain and plant checks, plus the three standalone Haven canopy regressions.
The already documented `DKRDirectSession` frame-ledger timing assertion failed
again, including its isolated retry. Its netplay implementation/test sources
are unchanged by these features. The earlier 61/61 package result above is
historical validation, not a claim that this intermittent failure is resolved.

## Prior Preview 3 validation (historical)

Verified package: **1.0.4-terrain-preview3**, native arm64 Metal, built and
ad-hoc signed by `Build-macOS.sh`. All 61 DKR tests pass in the package run,
including the terrain suite and both ROM-revision determinism tests. The
standalone `DKRDirectSession` test intermittently failed its existing handoff
timing assertion during earlier runs with the game active; it passed with the
game closed and in the final package run. Terrain is not linked into that test.

Captured US v1.0 and v1.1 hub meshes pass all three quality settings:

| Quality | Refined terrain triangles | Coincident samples checked |
|---|---:|---:|
| Low | 18,912 | 7,348 |
| Balanced | 75,648 | 13,528 |
| High | 302,592 | 25,888 |

Each hub capture contains 3,425 source triangles, of which 1,182 are selected,
plus 545 generated decoration triangles. Balanced cache construction measured
about 16 ms in standalone checks; this happens on scene/settings changes,
not every frame. Captured title/coastal terrain also passes seam checks.

Native checks so far cover the hub intro, lawn-to-shore driving, entrance trim,
waterfalls, the statue, kart contact, and Original/Surface/Geometry switching.
Typical observed terrain CPU submission was 0.7–2.5 ms depending on visibility.
The M3 Max Metal HUD showed roughly 4.5–6 ms GPU time in inspected hub views at
3456×2168, with presentation generally around 110–120 FPS. These are live
observations, not a controlled GPU benchmark or a guarantee for other Macs.

The packaged material and boundary diagnostics were inspected in the hub.
Ancient Lake was tested in two-player split screen, first stationary and then
with player one driving while player two remained at the start. Both cameras
kept terrain, road edges and kart contact intact. The inspected race views
reported about 119–122 presented FPS; terrain submission was about 0.4–0.5 ms
for 158 visible patches / 10,112 refined triangles across the two viewports.
The Ancient Lake capture additionally passes 1,547 coincident-sample checks.

The offline QA driver now uses a process-monotonic simulation counter so menu
input cannot remain held when a scene-local presentation counter resets.
Its bounded input/port isolation is covered by the terrain test executable.

Artifacts are under `dist/DKR-R-1.0.4-terrain-preview3-macOS-arm64/`, with the
corresponding `.zip` beside that directory. The archive SHA-256 is
`2d3d387a24c312df683c125e09447f84e93dcb6a6878307764ddb2daf1d4a56c`.
The package contains no ROM, save file, HD game texture pack, or RAM capture.
The material allowlist is deliberately conservative; this is a verified hub
terrain pass, not an all-course art audit.

For the final handoff, one instance of the packaged preview was left in the
playable hub with Geometry / Balanced / 100% enabled. The one-tick neutral QA
route completed, releasing the input override for normal controls. This run
uses an isolated QA configuration and save, leaving the user's normal saves
unchanged. Earlier preview packages were moved to
`build/terrain-qa/obsolete-releases/` to avoid launching the wrong candidate.

Local full-resolution inspection capture names (not tracked or included in the
release ZIP; retained here only to identify the historical QA evidence):

- Original terrain, fixed hub view: `codex-shot-2026-09-08_14-32-35.png`
- Surface shading, same view: `codex-shot-2026-09-08_14-33-03.png`
- Geometry, same view: `codex-shot-2026-09-08_14-33-12.png`
- Protected-boundary diagnostic: `codex-shot-2026-09-08_14-42-29.png`
- Packaged split-screen movement check: `codex-shot-2026-09-08_14-48-46.png`
- Final packaged preview, playable hub: `codex-shot-2026-09-08_14-56-46.png`

# 3D tree motion fix - 2026-09-08

Candidate: `1.0.4-tree-motion-fix1`, based on the approved Preview 7 assets.
User report: short sideways flicks still occurred after the private-texture
residency fix. That fix remains unchanged.

## Code-level cause addressed

DrawPalmReplacement previously baked object placement into integer world-space
vertices and changed its interpolation identity at clip-distance 1800. RT64
matches transforms by identity, so the first frame after an LOD change had no
previous camera transform to match. This can briefly snap a tree ahead of the
interpolated scene. Even the identical near/far blueberry and rubber-tree meshes
changed identity. Reusing that identity with vertex interpolation still enabled
would instead risk morphing different mesh topologies.

The replacement now submits static local-space vertices at 8192 units per source
unit and composes a model-to-world transform with the captured camera matrix.
One lifetime/camera/family identity is retained across both LODs. Only the combined
transform is interpolated, never mesh vertices or UVs. This also removes whole
world-unit vertex rounding during object motion. Original attachment offsets,
scale, yaw/pitch/roll, depth, fog, textures, and sprite fallback stay intact.

This addresses a demonstrable frame-matching discontinuity, not a claim that
every possible source of flicker has been ruled out.

## Verification

- All 59 DKR tests passed in the package run; controller-pak self-test passed.
- New tests cover all five supported sprite IDs, attached canopy placement,
  local-coordinate quantization bounds, changing object/camera identities,
  distance crossings, and transform interpolation through signed-angle wrap.
- Native arm64 app was packaged and its ad-hoc signature verified.
- Live QA uses a copy of the user's config at `work/tree-motion-qa/config`,
  leaving the original app/session and saves intact.
- `DKR_TRACE_PALM_3D=1` now records bounded LOD switches with the stable transform
  key and `vertexInterpolation=0` for live transition checks.
- Live moving-intro/hub checks exercised near/far transitions for the palm
  canopies and blueberry plants, with no sprite fallback entries. The hub's
  round trees were visually inspected at near and far distances. This is a
  bounded visual check, not a frame-by-frame proof across every driving route.

No model regeneration or material edits were made for this motion fix.
A route-specific user check remains necessary because the
reported intermittent flick was not captured frame-by-frame before this change.

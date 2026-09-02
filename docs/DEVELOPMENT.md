# DKR-R development guide

Project-owned code lives in `runtime-recomp/src/game`, tests in
`runtime-recomp/tests`, release assets in `assets` and `packaging`, and build
automation in `scripts`.

Never edit these generated or dependency paths directly:

- `runtime-recomp/RecompiledFuncs`
- `runtime-recomp/RecompiledPatches`
- `extern/n64-modern-runtime`
- `extern/n64-modern-runtime/N64Recomp`
- `extern/rt64`

Game hooks and instruction changes are declared in the versioned
`runtime-recomp/dkr.us.v77.recomp-policy.json` and
`runtime-recomp/dkr.us.v80.recomp-policy.json` files. Revision policies are
mapped by named function and checked intra-function offset, never by a global
VRAM delta. Dependency changes are patch files with SHA-256 entries in
`patches/manifest.json`. Regenerate after policy changes and validate every
dependency patch from its pinned clean commit.

Keep Accurate as the regression baseline. Modern features must not change the
simulation clock, race results, AI, audio cadence, save data or original input
semantics. A presentation identity discontinuity must fall back to the newest
authored endpoint rather than extrapolate.

Every behaviour policy should have a zero-dependency test. A release change is
complete only after CTest, runtime self-tests, asset scanning, package scanning
and visible playtesting of the affected scenes.

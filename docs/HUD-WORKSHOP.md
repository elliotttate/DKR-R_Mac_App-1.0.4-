# HUD layouts — v1.0.5 Beta 5 reference-alignment rebuild

The HUD editor is temporarily deactivated following launcher crash reports.
Graphics → HUD now offers only **4:3** and **Fit to Window**, at 100% HUD size.
These controls apply to **Modern one/two-player
presentation only**. Three/four-player HUDs and Accurate presentation retain
their existing behavior. These settings never change the world's aspect ratio.

## Suspended editor

Custom layouts, the Workshop, sizing controls, preset editing and live review
cannot be opened in this build. The editor implementation is excluded from the
production executable rather than merely hiding its buttons.

Previously saved custom placements and named presets are retained in the
configuration file, but do not affect rendering. A saved Custom/Safe Area mode
falls back to 4:3; a saved Fill mode stays Fit to Window. Saved custom sizes do
not apply. No manual configuration deletion or save restoration is necessary.

Selecting either supported option saves that selection immediately. A failed
write leaves the previous option active and displays an error. Restarting the
application retains the selected option.

## Scope

This rebuild does not alter the accepted three/four-player HUD, world-rendering
or shadow fixes, online gameplay, game saves, or launcher window lifecycle.

HUD settings remain in `hud-layouts-v2.json` under the existing configuration
directory, with a recovery backup. Legacy mode settings are still recognized.

## Validation boundary

Automated checks exercise the actual two-option ImGui settings entry, repeated
selection changes, save failures, disabled editor callbacks, and the real
runtime's persistence/publication path with isolated test profiles. Native
DX12/Vulkan, Steam Deck and in-race appearance still need your playtesting.

## Reference-alignment correction

The active presets no longer use the suspended editor's estimated bounding
boxes to choose anchors. All 59 retail HUD slots are explicitly classified by
race, time-trial, boss, challenge and hub mode, matching Golden Balloon's
`sHudWidescreenAnchor` table at revision
`83a847ccbd3e6334c9cc9c697122d5b3cddb91c1`.

Fit to Window translates complete groups; it does not widen their geometry.
The minimap background and markers share a translation. Temporary timer
glyphs and stopwatch/finish text use their verified caller's group. DKR's
two-player shared challenge strip remains right-aligned rather than adopting
Golden Balloon's single-player centred strip. Entry slides cover the expanded
canvas without changing the authoritative countdown or bounce animation.

Both presets explicitly preserve HUD projection and rectangle aspect. The
scoped projection is popped before later world/menu/dialogue draws. HUD
overscan clipping is not allowed to redefine the native framebuffer width.

The reference comparison checks all 295 element/mode entries. Headless tests
execute RT64's actual projection processor and rectangle conversion at 4:3,
16:10, 16:9, 21:9 and 32:9, with full/top/bottom HUD clips and restoration
checks. A negative control reproduces the old model-only aspect flag failure.
These tests do not replace native in-race visual verification.

Golden Balloon is credited in About DKR-R. See THIRD_PARTY.md and the packaged
GOLDEN-BALLOON-NOTICE.txt for the adaptations and upstream provenance.

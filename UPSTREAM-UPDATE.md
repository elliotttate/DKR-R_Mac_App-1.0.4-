# DKR-R upstream update, 2026-09-20

Upstream: [ThatGuyMcd/DKR-R](https://github.com/ThatGuyMcd/DKR-R), default branch
`main`, commit `8a8e927e9ea14c5c07ca7ad74b44fdbe077f5ff6` (2026-09-19).
Source version: `1.0.5-beta.10`. Mac package: `1.0.5-beta.10-macos.1`.
The separate upstream beta.11 branch contains release metadata changes only.

The update carries the existing Mac port, all six model families (palms,
blueberries, rubber trees, beach trees, bananas, balloons), their textures, and
procedural terrain. The unfinished PBR experiment remains in the original
checkout and is not included in this integration or package.

## Integration changes

- Keep upstream's typed presentation markers, HUD transforms and split-screen
  handling while attaching our replacement-model samples to geometry markers.
- Adapt replacement texture lookups to upstream's generated-mipmap API.
- Preserve the current launcher and its Track Lab/mod pages, with our F7/F8
  controls on the Textures page.
- Replace unsupported atomic shared-pointer specializations with standard
  shared-pointer atomic operations in the HUD, social, session and mod code.
- Use Cocoa button-event coordinates for launcher hit tests when no mouse
  motion event accompanies the click. Native traces confirmed that stale
  pointer state caused missed clicks.
- Use POSIX recvmsg truncation flags so oversized UDP packets are discarded
  correctly on macOS without dropping valid 2048-byte packets.
- Add Darwin importer limits and parent/memory monitoring; bundle and locate
  the private importer beside the SDL input helper.
- Make both GPU mipmap test harnesses use the native Metal backend on macOS.
- Generate v77 and v80 payloads from checked, composed beta policies with
  `scripts/prepare_macos_payloads.py`. Record the composed policy fingerprints
  in the executable. Keep upstream dependency pins and all 44 checked patches.
- Admit the exact Mac-built v80 ELF hash after rebuilding the pinned decomp to
  the canonical retail ROM. All hook/function/instruction checks still apply.

## Verification

- 83/83 runtime CTest cases passed, including the model/terrain tests and the
  atomic publication and UDP packet-boundary regression checks.
- 17/17 standalone legacy-mod tests passed, including ROM-backed asset checks.
- Both rebuilt ROMs match the supported canonical hashes; both generated
  payloads pass the finish-camera/viewport hook checks against their ELF words.
- Native Metal loading test passed: 77 UI frames during a real three-second
  texture-cache wait, six generated mip levels, automatic modal dismissal,
  and unchanged game queue cursors.
- Package controller-pak recovery and mod-importer self-tests passed.
- Fresh ZIP extraction passes strict/deep signature validation. All 40 model
  files match the source assets exactly; no PBR material pack is present.

- The freshly extracted app booted both v1.0 (v77) and Rev A (v80) on Metal,
  reaching a first rendered game frame in 883 ms and 849 ms respectively.
- v77 visibly rendered replacement palms and blueberries; F8 restored the
  original sprites and switched back. F7 disabled terrain detail and logged
  zero terrain patches. v80 rendered its attract sequence with detailed
  terrain (up to 436 patches / 27,904 triangles in the observed frame).
- Native launcher pointer navigation passed after the Cocoa click fix, including
  Graphics, Textures, Mods / Hacks and Track Lab. Terrain labels sit above their
  fields so their labels fit the settings panel.
- Both timed native runs exited cleanly. Checks cover startup, attract scenes
  and character selection; a complete playthrough, real imported custom tracks
  and multiplayer sessions were not exercised.

Native gameplay verification is recorded in the local `build/validation`
folder. Logs, ROM inputs, screenshots and generated code remain local and are
excluded from Git. See [LOCAL-MAC-BUILD.md](LOCAL-MAC-BUILD.md) for rebuilding.

Archive SHA256:
`bd953dab04329bb28b94e60a3bada419b7ea7a134c9f6d2936f66f79c8206fcb`.

Final app and ZIP: `/Users/briantate/Documents/GitHub/DKR-R_Mac_App-1.0.4-/dist/DKR-R-1.0.5-beta.10-macos.1-macOS-arm64` and the adjacent `.zip`.
Integration branch: `codex/update-dkr-3d-1.0.5`.

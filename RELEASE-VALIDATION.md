# DKR-R 1.0.4 release validation

Validated on 3 September 2026 from `C:\DKRPort`.

## Reproducibility and repository safety

- `VERSION`, Windows resources and Linux AppStream metadata resolve to `1.0.4`.
- N64ModernRuntime, N64Recomp, RT64 and the matching DKR decomp are locked to
  exact commits in `dependencies.lock.json`.
- Release metadata validation verified all 42 dependency-patch SHA-256 values
  declared by `patches/manifest.json` against the pinned dependency revisions.
- The repository asset scan passed across 1,117 files.
- Final documentation contains no development-stage roadmap language or
  research-project references.
- ROMs, saves, Controller Pak files, local configuration, logs and build caches
  are excluded from source and binary releases.

## Windows x64

- Native Visual Studio 2022 Release build completed.
- All 55 DKR-R regression tests passed, including the two-map gameplay handoff,
  single-viewport Adventure boss policy, dual-control Adventure race policy and
  conflicting-scene rejection cases. The regular-race policy also verifies the
  retail six-racer topology and both four-racer challenge variants without
  weakening the boss or shared-hub gates. Accessory-policy coverage verifies
  that an enabled virtual Mem Pak takes storage-probe priority, a disabled Mem
  Pak falls through to the retail accessory probe, and disabling Rumble Pak
  clears the game-visible rumble mask.
- The built and staged executables passed the virtual Controller Pak round-trip
  and backup-recovery self-test. The staged SDL3 input host and two live
  SDL2-to-SDL3-to-SDL2 switching round trips also passed.
- The Launcher and Overlay share the same `Enable Mem Pak` and `Enable Rumble
  Pak` controls under Controls > Device; both settings retain their existing
  persistent `memory_pak` and `rumble` configuration keys.
- The 40-file staged package and ZIP passed prohibited-extension and N64-header
  scans.
- Executable file and product versions report `1.0.4`.

Deliverable:

```text
dist/DKR-R-1.0.4-Windows-x64.zip
SHA-256 93E3569E13DF6C225B7DDE99D486F98C29A4E1DE9A655F8E778E8C4A44722A25
```

## Linux x86_64

- Native Ubuntu 24.04/GCC 13 Release build completed with RT64/Vulkan.
- All 55 DKR-R regression tests passed, including the same Mem Pak priority and
  Rumble Pak enablement policy exercised by the Windows build.
- The native runtime and packaged AppImage passed the virtual Controller Pak
  round-trip and backup-recovery self-test. The private SDL3 input host and two
  live input-backend switching round trips also passed.
- AppImage staging passed a 189-file prohibited-data scan before SquashFS
  creation.

Deliverable:

```text
dist/DKR-R-1.0.4-Linux-x86_64.AppImage
SHA-256 53AB95E4B067109DA477F256BBF7ADEC33BBB57DBF3CD9318E5C15EDC14DDDF4
```

## Source archive

The deterministic source package passed a 353-file archive scan for unsafe
paths, prohibited game-data extensions and N64 ROM headers:

```text
dist/DKR-R-1.0.4-Source.zip
```

## Scope

These checks are non-interactive release gates. The confirmed gameplay,
graphics, interpolation and audio baseline remains the user-visible acceptance
test for DKR-R 1.0.4.

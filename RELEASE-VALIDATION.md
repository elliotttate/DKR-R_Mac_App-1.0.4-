# DKR-R 1.0.2 release validation

Validated on 10 August 2026 from the clean `C:\DKRPort` release tree.

## Reproducibility and repository safety

- `VERSION`, Windows resources and Linux AppStream metadata resolve to `1.0.0`.
- N64ModernRuntime, N64Recomp, RT64 and the matching DKR decomp are locked to
  exact commits in `dependencies.lock.json`.
- The Patch Pipeline reproduced all 25 declared patches from clean dependency
  checkouts and verified every SHA-256 in `patches/manifest.json`.
- The repository asset scan passed across 225 source files.
- Final documentation contains no development-stage roadmap language or
  research-project references.
- ROMs, saves, Controller Pak files, local configuration, logs and build caches
  are excluded from source and binary releases.

## Windows x64

- Native Visual Studio 2022 Release build completed.
- All 18 DKR-R regression tests passed.
- The built executable and staged release executable passed the virtual
  Controller Pak round-trip and backup-recovery self-test.
- The staged package and ZIP passed prohibited-extension and N64-header scans.
- Executable file and product versions report `1.0.0`.

Deliverable:

```text
dist/DKR-R-1.0.2-Windows-x64.zip
SHA-256 D7A16D53A93C5B695588804617D23CD37B279E849EB4DEEF3E8B3C4D925329FD
```

## Linux x86_64

- Native Ubuntu 24.04/GCC 13 Release build completed with RT64/Vulkan.
- All 18 DKR-R regression tests passed.
- The native runtime and packaged AppImage passed the virtual Controller Pak
  round-trip and backup-recovery self-test.
- AppImage staging passed a 172-file prohibited-data scan before SquashFS
  creation.

Deliverable:

```text
dist/DKR-R-1.0.2-Linux-x86_64.AppImage
SHA-256 A228CC7FF5D3A84074E01CFBDE54BAE1A1EE0A93C3B94D278F59E917506E3D2F
```

## Scope

These checks are non-interactive release gates. The confirmed gameplay,
graphics, interpolation and audio baseline remains the user-visible acceptance
test for DKR-R 1.0.2.

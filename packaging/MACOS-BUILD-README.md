# DKR-R 1.0.1 macOS build handoff

The source/build kit contains no ROM or extracted game assets. On an Apple host
with Xcode command-line tools, CMake and Ninja, run:

```bash
./Setup-macOS.sh
./Build-macOS.sh
```

RT64 selects Metal on macOS. The script builds the native `.app`, runs CTest and
runtime self-tests, stages licences and assets, scans for prohibited game data,
applies an ad-hoc local signature and creates the architecture-specific ZIP in
`dist`.

Before publishing, test startup, ROM selection, Accurate and Modern gameplay,
audio, controller navigation, fullscreen, save writes, texture pack switching
and application exit on physical Apple hardware. Distribution signing and
notarization require the publisher's Apple credentials and are not performed by
the repository script.

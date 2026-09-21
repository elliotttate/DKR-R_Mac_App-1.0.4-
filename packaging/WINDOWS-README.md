# DKR-R 1.0.4 — Windows x64

This is the experimental Windows build from
https://github.com/elliotttate/DKR-R_Mac_App-1.0.4-, based on jt87's macOS port
and ThatGuyMcd's DKR-R. It includes 3D plants, spinning bananas, seven balloon
finishes, and material-aware terrain detail.

## Install and play

1. Compare the ZIP's SHA-256 with the accompanying `.sha256` file:

   ```powershell
   Get-FileHash -LiteralPath .\DKR-R-1.0.4-Windows-x64.zip -Algorithm SHA256
   ```

2. Extract the entire ZIP into a folder on your Windows x64 PC.
3. Run `DKR-R.exe` and select your own supported Diddy Kong Racing US v1.0 or
   US Rev A/v1.1 ROM when prompted. The application detects `.z64`, `.v64`, and
   `.n64` byte orders automatically and selects the correct revision.
4. Choose **Modern** to enable the fork's 3D models and terrain detail.

Keep `assets`, `libexec`, and the bundled DLLs beside `DKR-R.exe`. No separate
build tools are required. No ROM, saves, or external HD texture pack is included.
External texture packs can be imported separately through the application.

## Controls and included models

- **F8:** toggle 3D plants and items. All six model families are bundled: palms
  and attached canopies, blueberries, rubber trees, beach trees, bananas, and
  balloons, including collectibles. See `3D-MODELS.md`.
- **F7:** toggle procedural terrain detail.
- **Escape**, **F1**, or controller **Back/View:** open settings.
- **Alt+Enter** or **F11:** toggle fullscreen.

Accurate mode retains the original sprite presentation. The replacements
preserve game simulation, collision, and pickup timing. Local settings, saves,
Controller Paks, and imported packs are stored outside the package.
See `ONLINE_MULTIPLAYER.md` for connection and lobby instructions.

## Verification and support

The native Windows build passed all 63 DKR tests and packaged Controller Pak,
SDL3 helper, and live input-switch checks. All 40 bundled model files were
verified against the source. The packaged executable completed 30-second Modern
startup runs with both supported ROM revisions and all six model families loaded.
Full-course, collectible, and live multiplayer coverage is unverified.

Report fork-specific problems to this repository. The Play page can export a
support report; diagnostic logging and crash dumps are opt-in. See `LICENSE.md`,
`THIRD_PARTY.md`, and the bundled license files for terms and credits. The
release page links the corresponding Windows source commit.

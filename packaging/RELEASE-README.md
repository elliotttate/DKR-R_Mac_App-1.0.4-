# DKR-R 1.0.5-beta.10-macos.3 — Apple Silicon

This is the experimental macOS build from
https://github.com/elliotttate/DKR-R_Mac_App-1.0.4-, based on jt87's macOS port
and ThatGuyMcd's DKR-R. It includes native Metal rendering, bundled SDL2/SDL3,
3D vegetation, spinning 3D bananas, seven wraparound balloon finishes and
material-aware terrain detail. This is not jt87's notarized original release.

## Install on macOS

Requires Apple Silicon (M1 or newer) and macOS 12 Monterey or newer. Intel Macs
are not supported. No Homebrew, Xcode or command-line build tools are required.

1. Verify the ZIP against the supplied SHA256 checksum, then unzip it.
2. Drag `DKR-R.app` and `DKR-R Diagnostics.app` to Applications, then open DKR-R.
3. Supply your own supported US ROM when prompted and select Modern to use
   the new 3D replacements. Existing external HD packs can be imported separately.

This release is **Developer ID signed, but not notarized**. macOS may
block the first launch. Only if you trust this release and have verified its
checksum, use System Settings > Privacy & Security > Open Anyway for this app.
Do not disable Gatekeeper globally.

F8 toggles the optional 3D plants/items; F7 toggles terrain detail. Original
gameplay timing and collision are retained. Saves, ROMs and texture packs stay
outside the app. No ROM, saves or external HD texture pack is included.

## macOS compatibility and permissions

This revision fixes the Start Game compatibility issue caused by game shaders
compiled for the build Mac's newer OS. Native code and Metal shaders now target
macOS 12.0; packaging rejects binaries or shaders that exceed that target.
Native launch checks were performed on macOS 27. An older-macOS device test is
still needed to confirm the complete runtime on each supported OS release.

Browse for ROM opens the native macOS file picker. Select your own ROM there
to authorize access, including files in Documents, Downloads, iCloud or external
drives. Download cloud-only files in Finder before selecting them. If access
was previously denied, select the file again or check System Settings > Privacy
& Security > Files & Folders. macOS 12 uses System Preferences > Security &
Privacy > Privacy. LAN multiplayer may request Local Network access on macOS 15
or newer. DKR-R includes explanations for these requests; it does not require
Full Disk Access. Permission is requested when the corresponding feature needs it.

For a launch failure, collect `runtime.log` from `~/.config/dkr-port/logs/`
immediately afterward. Reopening the app moves the prior log to
`runtime-previous.log`.

## If the game crashes or will not open

Open **DKR-R Diagnostics.app**, which runs separately from the game renderer.
If the game opens, the same tool is available under **About DKR-R**, in the
support card: **Save Crash Logs / Mac Diagnostics**.

1. Choose **Save Diagnostic Report** before repeatedly relaunching. Select where
   to save the text file, then review it before sharing it with support.
2. Choose **Launch with Logging**, reproduce the problem, quit the game if it is
   still open, then save another report. The tool records errors that occur
   before the game creates its normal runtime log, plus the exit code or signal.
3. Try **Launch with Fresh Settings** to rule out a bad setting or imported pack.
   Select your ROM again. This creates a separate profile; your normal settings
   and saves are preserved. Fresh-session saves remain in the diagnostic profile.
4. If the game was moved, use **Choose DKR-R.app** to locate it.

Reports contain macOS/GPU details, release and binary UUID, signature status,
current and recent runtime logs, fatal-signal records and selected fields from
recent Apple crash reports. Common personal paths and connection data are
redacted. ROMs, saves, full settings files and full Apple reports are excluded.
Review the text before sharing; nothing is uploaded automatically. A report can
still be saved when the game is missing or cannot launch.

The game retains eight archived runtime logs and up to eight fatal-signal records.
Native Apple reports can also be found in `~/Library/Logs/DiagnosticReports/`.
Diagnostic launch output and fresh profiles are stored in
`~/Library/Application Support/DKR-R/Diagnostics/`. Both apps target macOS 12+;
this tool does not bypass Gatekeeper or grant Full Disk Access. If macOS blocks
both apps, use Console > Crash Reports or collect the existing runtime log for
support. No Xcode, Homebrew or Python installation is required.

## Upstream beta changes

This playtest build repairs failed model-load cache accounting in US v1.0,
safely rejects null model instances, and gives offline custom-mod sessions
the native expansion-memory heap extent within the existing 8 MiB renderer
boundary. Stock and online sessions keep their original heap extent.
HUD asset-load failures now produce a bounded diagnostic instead of being silent.
Custom-character selection facing, portraits, voices, HUD placement and Blender
Track Lab additions are retained. Visual HUD acceptance remains a playtest check.
Track Lab is available in **Mods/Hacks**, directly below **Magic Codes**, with
its Blender working-folder, import and testing controls intact.
Custom legacy mods remain offline-only, with at most two active custom characters.
Characters can only use replacement portraits and sounds actually supplied by
their mod; unchanged donor assets remain unchanged. Saved ghost identities still
use the original game's character IDs.

DKR-R is a native static recompilation of Diddy Kong Racing. This package does
not include the game ROM or extracted game assets. Supply your own legally
obtained Diddy Kong Racing US v1.0 or US Rev A/v1.1 Game Pak image when
prompted. DKR-R identifies `.z64`, `.v64`, and `.n64` byte orders automatically
and selects the matching revision. Windows embeds Rev A as an in-process DLL in
the single `DKR-R.exe`; Linux contains both revisions in the single AppImage.
No manual engine selection is required, and Windows never launches a second
background game process.

Start `DKR-R.exe` on Windows or the DKR-R AppImage on Linux. Select Accurate for
the original 4:3/30 FPS presentation, or Modern for optional widescreen,
high-refresh interpolation and quality-of-life controls.

Open settings with Escape, F1 or controller Back/View. Alt+Enter and F11 toggle
fullscreen. Local settings, saves, Controller Paks, imported filters and texture
packs are stored outside this package.

Modern mode includes HUD controls for original 4:3 or fit-to-window placement.
HUD artwork retains its original proportions; these presentation settings do
not alter gameplay or online determinism.

The Controls page supports four explicitly assigned local controllers. Known
devices use the bundled cross-platform mapping database. Select any controller
marked **Setup required** to run the N64 mapping wizard; custom mappings are
saved locally and can be imported or exported from the same page.
Automatic input mode uses the private native SDL3 controller and gyro helper on
Steam Deck and the established SDL2 controller path elsewhere. The helper is
headless: the launcher and game remain one SDL2 window. SDL2 compatibility can
be selected explicitly for raw controller remapping. Backend changes apply live
at a neutral input boundary, and SDL2 is restored automatically if the helper
is unavailable.
Enabling **Virtual Controller Paks** creates one independent Pak for every
connected local racer while preserving rumble on supported controllers.

If this package was not downloaded from ThatGuyMcd's official GitHub release,
it may have been modified. Verify the published SHA-256 before running it.

## Online multiplayer

Open **ONLINE MP** to create an encrypted two-to-four-player room or join one
with a five-character Quick Join code. The host
approves every joining device, is always Player 1, and starts only after every
racer is ready. Quick Join needs no account, adapter selection, invitation
file, IP address or port forwarding.
Each machine chooses its own local controller or keyboard profile before
readying; DKR-R routes that source to the host-assigned online player number
automatically.
When `JOINTVENTURE` is enabled, the original shared Adventure hub remains a
single-view, single-racer scene. DKR-R routes control to whichever online racer
the game currently assigns as the native lead; races, bosses and minigames
continue to expose both racers normally.
Hosts can also select **INVITE FRIENDS** in an open lobby. Online friends
receive an in-game notification and a persistent invitation under **OPEN
LOBBIES**. Friend invitations expire after five minutes and use a single-use
authenticated admission; sharing the normal five-character code still uses
the standard host-approval flow.
Quick Join uses a public rendezvous for WebRTC signaling only; it does not carry
gameplay, inputs or save data. DKR-R has no account service, public lobby
directory, gameplay server or relay. Some restrictive networks may be unable
to establish the required direct peer route.

After every racer is ready, Player 1 starts a synchronized five-second
countdown. All peers show the same centred countdown and tones before the
cold-boot synchronization barrier releases gameplay.

## Support information

The Play page can export a privacy-safe settings and system summary. Optional
diagnostic logging and crash dumps can be enabled from the same card, and the
corresponding local folders can be opened directly. Reports exclude ROM/save
contents, personal paths, account names, hardware identifiers and online
invitation data.

The DKR-R application icon was created by POOTERMAN. The DKR-R HDR Texture Pack
Project is led by `sr.gu` and re-imagines DKR's artwork in faithful high
definition.

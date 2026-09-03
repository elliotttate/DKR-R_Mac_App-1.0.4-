# DKR-R 1.0.4

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

Modern mode includes compact HUD controls for original placement, a protected
safe area or fit-to-viewport positioning, plus a global HUD-size adjustment.
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

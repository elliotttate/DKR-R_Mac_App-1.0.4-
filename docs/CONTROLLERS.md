# Controller support

DKR-R supports four independently assigned local controllers. It loads SDL's
built-in mappings, then the pinned DKR-R controller database, and finally the
player's local mappings. The later source wins, so an in-app mapping can safely
correct a device without changing the installation.

The bundled database contains 2,256 platform-specific mapping records: 866 for
Windows, 732 for Linux, 317 for macOS, plus mobile records retained from the
upstream database. A database entry means the device has a known SDL mapping;
it is not a claim that the DKR-R team physically certified every hardware and
firmware revision.

## N64-focused controllers and adapters

The bundled mappings explicitly include these named families and revisions:

- 8BitDo 64 and 8BitDo N64;
- Nintendo Switch Online N64 Controller;
- Hyperkin Admiral N64 Controller and Hyperkin N64 adapters;
- Mayflash N64 adapters;
- Raphnet N64 adapters and Raphnet GC/N64 adapters;
- N64 Adaptoid;
- RetroUSB N64 RetroPort.

Other N64 USB and Bluetooth controllers are supported through the setup wizard
when the operating system exposes their buttons, axes and D-pad through SDL.
This covers devices absent from the snapshot, unusual firmware modes and many
generic USB N64 adapters. The wizard requests the original N64 controls in
game-native order and activates the result immediately.

## Other controller families

The database and SDL platform backends include mappings for major Xbox 360,
Xbox One, Xbox Series, Elite and Adaptive controllers; Sony PlayStation 3/4/5
controllers; Nintendo Switch Pro and Joy-Con devices; GameCube adapters;
Steam Controller and Steam Deck input; Google Stadia and Amazon Luna pads; and
controllers from 8BitDo, Logitech, GameSir, PowerA, PDP, HORI, Razer,
SteelSeries, SCUF, Flydigi, GuliKit, Mayflash, Hyperkin and Raphnet.

The optional SDL3 native backend runs in a private headless input helper. It
enables the Steam Deck HIDAPI controller and sensor path before discovery, but
never owns or replaces DKR-R's SDL2 launcher/game window. Automatic mode selects
this backend on Steam Deck and retains SDL2 compatibility elsewhere. If the
helper cannot start or disconnects, DKR-R activates its SDL2 controller path
without restarting the application.

Backend selections apply live from the Controls page. The handover releases the
old backend's controller, sensor and haptic handles before the new backend takes
ownership; the SDL2 launcher/game window, renderer and audio remain running. A
failed activation restores the previous controller backend automatically.

SDL3 native mode supports the Deck controls and gyro without requiring a Steam
Input gyro mapping. In SteamOS Gaming Mode, Steam normally exposes buttons as
the sensorless Steam Virtual Gamepad. DKR-R keeps that SDL3 gamepad for normal
controls and, only when SDL reports no gamepad gyro, reads the built-in Deck IMU
through SDL3's private HID interface. The fallback is limited to Valve's Deck
VID/PID and controller interface, preserves SDL's Deck axis conversion, and
does not replace the launcher/game window or the selected button device.

Steam Input can still be used in SDL2 compatibility mode when a virtual
Xbox-style device is preferred. Do not map the Deck gyro to mouse or joystick
while DKR-R native gyro is enabled, because that would add a second motion path.

Raw mapping creation and map import remain on the SDL2 compatibility backend;
SDL3 native mode consumes SDL3's standardized gamepad mappings directly.

## Assignment and remapping

- **Automatic** fills the first available player slots while retaining stable
  reconnect claims.
- **Manual** assigns a named controller to a specific player and prevents one
  device from driving two players.
- **Press a button to assign** identifies the physical controller without
  exposing its GUID, serial number or operating-system path in the UI.
- **Remap this controller** rebuilds a known or incorrect mapping.
- **Import/Export custom maps** transfers only the player's SDL mapping text;
  it does not export saves, settings or ROM data.
- The keyboard can be owned by exactly one selected player. Its binding column
  is shown only while editing that player.
- Online MP asks each machine which local player profile supplies its racer.
  That choice selects the assigned controller, bindings, keyboard ownership
  and gyro source; it does not change the host-assigned network player number.

Custom mappings are stored under the user's DKR-R configuration directory in
`controllers/gamecontrollerdb.txt`. Disconnecting a manually assigned device
leaves that player neutral; reconnecting it reclaims the same player without
shifting the other assignments.

The exact bundled mapping list and its source hash are documented in
[`assets/controllers/README.md`](../assets/controllers/README.md).

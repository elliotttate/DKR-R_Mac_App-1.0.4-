# Troubleshooting DKR-R 1.0.4

## ROM rejected

Diddy Kong Racing US v1.0/v77 and US Rev A/v1.1/v80 are supported. Normalized
SHA-1 values are `0cb115d8716dbbc2922fda38e533b9fe63bb9670` and
`6d96743d46f8c0cd0edb0ec5600b003c89b93755`. The file extension does not decide
byte order.

Byte-swapped and little-endian dumps are converted once into a verified local
big-endian cache. If that cache is interrupted or damaged, close DKR-R and
remove only the `rom-cache` directory from the active configuration folder;
the launcher recreates it from the selected ROM on the next start.

Players always start the single public DKR-R application; engine selection is
automatic. On Windows, a damaged private Rev A cache can be repaired by closing
DKR-R and removing only `%LOCALAPPDATA%\DKR-R\engines`. The verified engine is
recreated from `DKR-R.exe` on the next Rev A launch. If verification still
fails, restore the official package and compare its published SHA-256.

## Saves or settings appear missing

Compatibility paths are retained:

- Windows: `%APPDATA%\DKRPort`
- Linux: `$XDG_CONFIG_HOME/dkr-port` or `~/.config/dkr-port`

Portable Windows stores data in `dkr-runtime-data` beside the executable.

## Linux/SteamOS closes as gameplay starts

Run the AppImage from a terminal and verify Vulkan with `vulkaninfo`. Steam Deck
must expose its native Vulkan driver rather than a software or remote desktop
device. The launcher may appear before RT64 creates the Vulkan device, so a
driver failure can occur only when gameplay begins.

## Modern presentation is uneven

Start at 60 FPS with Match Display disabled. Verify Accurate remains steady at
4:3/30 FPS. High-refresh output interpolates presentation; it must not speed up
simulation or audio. Disable custom texture packs and CRT filters while
isolating third-party content.

## Overlay input

Open the overlay with Escape, F1 or controller Back/View. Navigate with D-pad or
left stick, select with A/Cross, cancel with B/Circle, and change pages with
LB/RB. Mouse input remains active while the overlay is open.

## Gyro drift

Place the controller still, calibrate, then recenter at the desired neutral
angle. On Steam Deck, select SDL3 native input and leave the Steam Input gyro
action set to None; DKR-R reads the built-in IMU directly when Steam's virtual
gamepad does not advertise a sensor. Do not map gyro to mouse or joystick at the
same time. DKR-R uses hardware sensor timestamps when available, so duplicate
high-rate polls do not integrate the same gyro sample more than once.

If the Controls page still reports no gyro, run the packaged private helper
from a terminal with `--self-test`. A Deck should list either a gamepad with
`gyro=yes` or at least one `Steam Deck physical gyro interface`. Zero physical
interfaces means SteamOS did not expose the Deck's `28de:1205` HID interface to
the application; update SteamOS and verify the standard Steam device permission
rules are installed.

## Graphics API recovery

If a selected backend cannot initialize, remove only the local graphics setting
or choose Automatic on the next launch. Do not delete your ROM or save files.

## Recomp policy change has no effect

Run `Diagnose-DKR-Recompile.cmd`. Never edit generated functions or dependency
worktrees directly.

## Creating a support report

Open **PLAY**, expand the support summary, then select **EXPORT SUPPORT REPORT**.
The report records the DKR-R release, presentation and graphics settings,
enabled texture-pack count, operating system, CPU, memory, graphics adapter and
storage type. It deliberately excludes paths, ROM data, saves, account names,
controller identifiers, Friend Codes and lobby codes.

Diagnostic logging and crash dumps are separate opt-in switches in the same
card. Enable them only while reproducing a problem, restart if requested, and
use **OPEN LOGS** or **OPEN CRASH DUMPS** to reach the resulting files. Turning
either switch off does not delete existing reports. Review any file before
sharing it publicly.

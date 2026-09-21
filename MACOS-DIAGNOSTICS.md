# Mac crash logs and failed-launch diagnostics

Available in **1.0.5-beta.10-macos.3**, based on upstream main `8a8e927` with
our existing models and terrain. The unfinished PBR experiment remains separate.

## For a player

Open **DKR-R Diagnostics.app** from the download, or choose **About DKR-R**,
scroll to the support controls, and click **Save Crash Logs / Mac Diagnostics**.
The Diagnostics app runs independently of the game's renderer.

- **Save Diagnostic Report** opens a native Save dialog. Save the text file,
  review it, and share it with support. Nothing is uploaded automatically.
- **Launch with Logging** starts the game with logging enabled for that session,
  captures early stdout/stderr, and records its exit code or fatal signal.
- **Launch with Fresh Settings** starts a separate profile without imported
  packs, settings or existing saves. Select your ROM again. Normal user data is
  preserved; saves made during this test remain in the separate diagnostic profile.
- **Choose DKR-R.app** selects the game if it has been moved.

Save a report before repeatedly relaunching, then another after reproducing the
problem. Apple may take several seconds to create its crash report; save again
if the first report says none was found. Both applications target Apple Silicon
and macOS 12+. No Xcode, Homebrew or Python installation is needed. This release
is Developer ID signed but not notarized. If macOS blocks both applications,
Console's Crash Reports and the existing runtime logs remain available; the
diagnostics tool does not bypass Gatekeeper or request Full Disk Access.

## What is recorded

The game preserves the current and previous runtime logs plus eight archived
logs under `~/.config/dkr-port/logs/` (or its selected configuration directory).
Fatal-signal records go in `crash-dumps/`, with bounded retention. Crash recording
is enabled by default and its preference applies at the next launch. Empty
reserved crash files from successful runs are removed on later launches.

The Mac signal handler uses pre-opened files and async-signal-safe writes for
the signal, fault address, program counter, link register and executable base.
It re-raises the original signal instead of exiting normally, allowing macOS to
create its usual crash report. It covers SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL
and SIGTRAP; SIGKILL, power loss and failures before handler installation cannot
produce an in-process record. The independent launcher still records launch
errors/exit status when available, and the exporter can run without the game.

Reports contain release/OS/GPU information, executable UUID, signature status,
recent bounded log excerpts, fatal records, and selected exception/termination
and crashed-thread frame fields from up to three Apple reports. Full Apple
reports, ROMs, saves, settings files, personal paths and connection details are
not copied. Common personal-path and connection-data lines are omitted from log
excerpts; review reports before sharing because third-party log text can vary.
Original Apple reports stay in `~/Library/Logs/DiagnosticReports/`.

Diagnostic launch logs and fresh profiles are stored in
`~/Library/Application Support/DKR-R/Diagnostics/`. The tool refuses to start a
second game session while one is running. It never deletes or resets the normal
profile. The launcher also now uses the macOS `open` utility for support folders,
replacing the inherited Linux-only `xdg-open` call.

## Verification, 2026-09-20

- **85/85 tests passed**, with the two focused Mac tests rerun after the final
  signal-thread correction. Checks include log-retention, fatal-signal
  termination/recording, disabled-recording preference, report redaction, Apple
  report field selection and malformed-frame regression checks.
- A missing-game fixture still exported a report. An unwritable destination
  returned failure. ROM/save contents, personal paths and connection-data
  sentinels were excluded while a useful renderer-error message remained.
- A fresh extracted ZIP passed strict/deep signature checks for both apps.
  The compatibility audit checked six native binaries and 57 Metal libraries;
  their deployment targets remain at or below macOS 12. The standalone helper
  is an exact copy of the audited embedded helper.
- Native Diagnostics opened a fresh profile; the game accepted v77 through the
  native picker and rendered on Metal. The in-game support button opened the
  embedded helper with the correct configuration directory.
- A controlled SIGABRT sent only to that QA game produced a fatal record and
  a native Apple `.ips` report. Diagnostics displayed signal 6. Saving through
  the native Save dialog succeeded; a later export included Apple's delayed
  report with EXC_CRASH/SIGABRT and stack frames. Personal paths and raw Apple
  crash identifiers were absent. Hashes of normal-profile preferences remained
  unchanged throughout the fresh-profile test.

Native tests ran on macOS 27 with an M3 Max. Actual execution on macOS 12-26
remains unverified. This change adds diagnostic evidence and a recovery path;
reports are still needed to identify another user's particular crash.

Local evidence: `build/validation/macos-diagnostics/`, including
`build-package-final.log`, `missing-game-report.txt`, `Controlled-Crash-Report.txt`,
`Controlled-Crash-With-Apple-Report.txt`, `fresh-session-game.png`, and
`report-saved.png`, and `Final-Signal-Report.txt`. The final executable symbol map
is retained as `macos.3-executable-symbols.txt` for matching program counters to
the exact shipped binary. These logs, symbols and fixtures remain untracked.

Delivered ZIP: `dist/DKR-R-1.0.5-beta.10-macos.3-macOS-arm64.zip`.
SHA256: `39f40f3a937c95196450e5dd66fd7d2a4c86756a02f0af4c696b5ef3e5f5f21a`.

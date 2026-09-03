#!/usr/bin/env bash
# Make a built DKR-R.app self-contained so it runs on a Mac without Homebrew.
#
# The runtime links Homebrew's sdl2-compat by absolute path, and sdl2-compat
# dlopens SDL3 at runtime. Both are copied into Contents/Frameworks and the
# load paths rewritten, so the bundle carries its own copies.
#
# Usage: scripts/Bundle-macOS-Redistributable.sh <path-to-DKR-R.app> [signing-identity]
#   signing-identity defaults to "-" (ad-hoc). Pass a Developer ID Application
#   identity to produce a bundle that can then be notarised.
set -euo pipefail

app="${1:?usage: $0 <path-to-DKR-R.app> [signing-identity]}"
identity="${2:--}"
binary="${app}/Contents/MacOS/DKR-R"
frameworks="${app}/Contents/Frameworks"

[[ -x "${binary}" ]] || { echo "Not a DKR-R bundle: ${app}" >&2; exit 1; }
command -v install_name_tool >/dev/null || { echo "install_name_tool missing" >&2; exit 1; }

# Resolve the real dylibs behind Homebrew's symlinks.
sdl2_link="$(otool -L "${binary}" | awk '/libSDL2-2\.0\.0\.dylib/ {print $1; exit}')"
[[ -n "${sdl2_link}" ]] || { echo "No SDL2 dependency found; nothing to bundle." >&2; exit 0; }
case "${sdl2_link}" in
  @*) echo "SDL2 is already bundled (${sdl2_link})."; exit 0 ;;
esac
sdl2_real="$(readlink -f "${sdl2_link}")"
sdl3_real="$(readlink -f "$(brew --prefix sdl3)/lib/libSDL3.dylib")"
[[ -f "${sdl2_real}" ]] || { echo "Missing SDL2: ${sdl2_real}" >&2; exit 1; }
[[ -f "${sdl3_real}" ]] || { echo "Missing SDL3: ${sdl3_real}" >&2; exit 1; }

mkdir -p "${frameworks}"
install -m 0755 "${sdl2_real}" "${frameworks}/libSDL2-2.0.0.dylib"
# sdl2-compat dlopens "@loader_path/libSDL3.dylib", so the name matters here.
install -m 0755 "${sdl3_real}" "${frameworks}/libSDL3.dylib"

install_name_tool -id "@executable_path/../Frameworks/libSDL2-2.0.0.dylib" \
  "${frameworks}/libSDL2-2.0.0.dylib"
install_name_tool -id "@executable_path/../Frameworks/libSDL3.dylib" \
  "${frameworks}/libSDL3.dylib"
install_name_tool -change "${sdl2_link}" \
  "@executable_path/../Frameworks/libSDL2-2.0.0.dylib" "${binary}"

# Signatures are invalidated by the rewrites; sign inside-out.
codesign --force --timestamp --sign "${identity}" "${frameworks}/libSDL3.dylib"
codesign --force --timestamp --sign "${identity}" "${frameworks}/libSDL2-2.0.0.dylib"
if [[ "${identity}" == "-" ]]; then
  codesign --force --deep --sign - "${app}"
else
  codesign --force --timestamp --options runtime --sign "${identity}" "${app}"
fi

remaining="$(otool -L "${binary}" | tail -n +2 | grep -c '/opt/homebrew' || true)"
if [[ "${remaining}" -ne 0 ]]; then
  echo "Homebrew paths still present in ${binary}:" >&2
  otool -L "${binary}" | grep '/opt/homebrew' >&2
  exit 1
fi

echo "Bundled SDL2 and SDL3 into ${frameworks}"
echo "Signed with identity: ${identity}"
codesign -dv "${app}" 2>&1 | sed -n '1,5p'

#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIRECTORY="${DKR_LINUX_BUILD_DIR:-${PROJECT_ROOT}/build/dkr-runtime-linux}"
BINARY="${BUILD_DIRECTORY}/bin/Release/DKR-R"
INPUT_HOST_DIRECTORY="${BUILD_DIRECTORY}/bin/Release/libexec/dkr-r"
INPUT_HOST="${INPUT_HOST_DIRECTORY}/DKR-R-InputHost"
SDL3_LIBRARY="${INPUT_HOST_DIRECTORY}/libSDL3.so.0"
VERSION_FILE_VALUE="$(tr -d '\r\n' < "${PROJECT_ROOT}/VERSION")"
VERSION="${DKR_RELEASE_VERSION:-${VERSION_FILE_VALUE}}"
APPDIR="${DKR_APPDIR:-${PROJECT_ROOT}/dist/DKR-R-${VERSION}-Linux-x86_64.AppDir}"
OUTPUT="${DKR_APPIMAGE_OUTPUT:-${PROJECT_ROOT}/dist/DKR-R-${VERSION}-Linux-x86_64.AppImage}"
LINUXDEPLOY="${LINUXDEPLOY:-${PROJECT_ROOT}/.deps/tools/linuxdeploy-x86_64.AppImage}"
APPIMAGE_PLUGIN="${LINUXDEPLOY_PLUGIN_APPIMAGE:-${PROJECT_ROOT}/.deps/tools/linuxdeploy-plugin-appimage}"
ICON_FILE="${DKR_LINUX_ICON_FILE:-${PROJECT_ROOT}/assets/ui/Icons/256x256.png}"
ICON_STAGE="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-icon.XXXXXX")"
BINARY_STAGE="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-binary.XXXXXX")"
PAK_TEST="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-appimage-pak.XXXXXX")"
INPUT_SWITCH_TEST="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-appimage-input-switch.XXXXXX")"
trap 'rm -rf -- "${ICON_STAGE}" "${BINARY_STAGE}" "${PAK_TEST}" "${INPUT_SWITCH_TEST}"' EXIT

validate_release_tree() {
  local root="$1"
  local file extension magic inspected=0
  while IFS= read -r -d '' file; do
    inspected=$((inspected + 1))
    extension="${file##*.}"
    extension="${extension,,}"
    case "${extension}" in
      z64|v64|n64|eep|mpk|sra|fla|o2r|otr)
        echo "Release staging contains prohibited game data: ${file}" >&2
        return 1
        ;;
    esac
    magic="$(od -An -tx1 -N4 "${file}" 2>/dev/null | tr -d '[:space:]')"
    case "${magic}" in
      80371240|37804012|40123780)
        echo "Release staging contains an N64 ROM header: ${file}" >&2
        return 1
        ;;
    esac
  done < <(find "${root}" -type f -print0)
  [[ "${inspected}" -gt 0 ]] || {
    echo "Release staging is empty: ${root}" >&2
    return 1
  }
  echo "Release staging scan passed: ${inspected} files inspected"
}

collect_linux_dependency_notices() {
  local root="$1"
  local copyright_file package_directory
  local notice_root="${root}/usr/share/doc/dkr-port/third-party/linux-packages"
  local manifest="${notice_root}/PACKAGE-MANIFEST.txt"
  local library_count copyright_count

  mkdir -p "${notice_root}/common-licenses"
  : > "${manifest}"
  printf '%s\n' \
    'Debian package notice directories deployed with the Linux dependencies:' \
    >> "${manifest}"

  while IFS= read -r -d '' copyright_file; do
    package_directory="$(basename "$(dirname "${copyright_file}")")"
    printf '%s\n' "${package_directory}" >> "${manifest}"
  done < <(find "${root}/usr/share/doc" -mindepth 2 -maxdepth 2 \
    -type f -name copyright -print0 | sort -z)

  while IFS= read -r -d '' copyright_file; do
    install -m 0644 "${copyright_file}" \
      "${notice_root}/common-licenses/$(basename "${copyright_file}")"
  done < <(find /usr/share/common-licenses -maxdepth 1 -type f -print0)

  library_count="$(find "${root}/usr/lib" -maxdepth 1 -type f | wc -l)"
  copyright_count="$(find "${root}/usr/share/doc" -mindepth 2 -maxdepth 2 \
    -type f -name copyright | wc -l)"
  if [[ "${library_count}" -eq 0 || "${copyright_count}" -eq 0 ]]; then
    echo "Linux dependency deployment did not include libraries and copyright records." >&2
    return 1
  fi
  echo "Collected ${copyright_count} dependency copyright records for ${library_count} bundled libraries"
}

[[ -x "${BINARY}" ]] || { echo "Missing Linux release binary: ${BINARY}" >&2; exit 1; }
[[ -x "${INPUT_HOST}" ]] || { echo "Missing private SDL3 input host: ${INPUT_HOST}" >&2; exit 1; }
[[ -f "${SDL3_LIBRARY}" ]] || { echo "Missing private SDL3 runtime: ${SDL3_LIBRARY}" >&2; exit 1; }
[[ -x "${LINUXDEPLOY}" ]] || { echo "Missing linuxdeploy: ${LINUXDEPLOY}" >&2; exit 1; }
[[ -x "${APPIMAGE_PLUGIN}" ]] || { echo "Missing linuxdeploy AppImage plugin: ${APPIMAGE_PLUGIN}" >&2; exit 1; }
[[ -f "${ICON_FILE}" ]] || { echo "Missing Linux application icon: ${ICON_FILE}" >&2; exit 1; }
command -v patchelf >/dev/null || { echo "Missing required command: patchelf" >&2; exit 1; }
[[ ! -e "${APPDIR}" ]] || { echo "AppDir already exists; choose a fresh DKR_APPDIR: ${APPDIR}" >&2; exit 1; }
[[ ! -e "${OUTPUT}" ]] || { echo "Output already exists; choose a fresh DKR_APPIMAGE_OUTPUT: ${OUTPUT}" >&2; exit 1; }

mkdir -p "${APPDIR}/usr/share/doc/dkr-port/licenses" "$(dirname "${OUTPUT}")"
mkdir -p "${APPDIR}/usr/share/metainfo"
install -m 0644 "${ICON_FILE}" "${ICON_STAGE}/dkr-r.png"
install -m 0755 "${BINARY}" "${BINARY_STAGE}/DKR-R"
install -m 0644 "${PROJECT_ROOT}/LICENSE.md" "${APPDIR}/usr/share/doc/dkr-port/LICENSE.md"
install -m 0644 "${PROJECT_ROOT}/THIRD_PARTY.md" "${APPDIR}/usr/share/doc/dkr-port/THIRD_PARTY.md"
install -m 0644 "${PROJECT_ROOT}/docs/ONLINE_MULTIPLAYER.md" "${APPDIR}/usr/share/doc/dkr-port/ONLINE_MULTIPLAYER.md"
install -m 0644 "${PROJECT_ROOT}/runtime-recomp/COPYING-NOTICE.md" "${APPDIR}/usr/share/doc/dkr-port/COPYING-NOTICE.md"
install -m 0644 "${PROJECT_ROOT}/extern/rt64/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/RT64-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/rt64/src/contrib/imgui/LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/Dear-ImGui-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/SDL2-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/sdl3/LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/SDL3-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/n64-modern-runtime/COPYING" "${APPDIR}/usr/share/doc/dkr-port/licenses/N64ModernRuntime-COPYING.txt"
install -m 0644 "${PROJECT_ROOT}/extern/n64-modern-runtime/N64Recomp/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/N64Recomp-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/Jumpman-LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/Jumpman-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/CRT-FILTERS-NOTICE.md" "${APPDIR}/usr/share/doc/dkr-port/licenses/CRT-FILTERS-NOTICE.md"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/SDL-GAMECONTROLLERDB-LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/SDL-GAMECONTROLLERDB-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/GEKKONET-LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/GEKKONET-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/MONOCYPHER-LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/MONOCYPHER-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/LIBDATACHANNEL-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/mbedtls/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/MBEDTLS-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/deps/libjuice/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/LIBJUICE-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/deps/usrsctp/LICENSE.md" "${APPDIR}/usr/share/doc/dkr-port/licenses/USRSCTP-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/deps/json/LICENSE.MIT" "${APPDIR}/usr/share/doc/dkr-port/licenses/NLOHMANN-JSON-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/deps/plog/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/PLOG-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/linux/dkr-port.appdata.xml" "${APPDIR}/usr/share/metainfo/dkr-port.appdata.xml"

export PATH="$(dirname "${APPIMAGE_PLUGIN}"):${PATH}"
export OUTPUT
export VERSION
export LDAI_OUTPUT="${OUTPUT}"
export LINUXDEPLOY_OUTPUT_VERSION="${VERSION}"
export APPIMAGE_EXTRACT_AND_RUN=1
# This local release has no public project homepage yet. appimagetool treats
# that optional AppStream field as a fatal warning, so package the supplied
# metadata without the network-facing catalogue validation step.
export LDAI_NO_APPSTREAM=1

"${LINUXDEPLOY}" --appimage-extract-and-run \
  --appdir "${APPDIR}" \
  --executable "${BINARY_STAGE}/DKR-R" \
  --desktop-file "${PROJECT_ROOT}/packaging/linux/dkr-port.desktop" \
  --icon-file "${ICON_STAGE}/dkr-r.png"

mkdir -p "${APPDIR}/usr/bin/assets/ui/Icons"
install -m 0644 "${PROJECT_ROOT}/assets/ui/Icons/DKR-R-Logo.bmp" \
  "${APPDIR}/usr/bin/assets/ui/Icons/DKR-R-Logo.bmp"
install -m 0644 "${PROJECT_ROOT}/assets/ui/Icons/DKR-R-Spinning-Icon.png" \
  "${APPDIR}/usr/bin/assets/ui/Icons/DKR-R-Spinning-Icon.png"
install -m 0644 "${PROJECT_ROOT}/assets/ui/Icons/DKR-R-Short-Logo.png" \
  "${APPDIR}/usr/bin/assets/ui/Icons/DKR-R-Short-Logo.png"
mkdir -p "${APPDIR}/usr/bin/assets/ui/Backgrounds"
install -m 0644 \
  "${PROJECT_ROOT}/assets/ui/Backgrounds/DKR-R-Launcher-Background.png" \
  "${APPDIR}/usr/bin/assets/ui/Backgrounds/DKR-R-Launcher-Background.png"
mkdir -p "${APPDIR}/usr/bin/assets/filters"
install -m 0644 "${PROJECT_ROOT}"/assets/filters/*.png \
  "${APPDIR}/usr/bin/assets/filters/"
mkdir -p "${APPDIR}/usr/bin/assets/controllers"
install -m 0644 "${PROJECT_ROOT}/assets/controllers/gamecontrollerdb.txt" \
  "${APPDIR}/usr/bin/assets/controllers/gamecontrollerdb.txt"

# Keep SDL3 outside usr/bin so it cannot replace or interpose on the SDL2 ABI
# used by the single launcher/game window. The helper has an $ORIGIN rpath and
# therefore resolves only the private copy installed beside it.
mkdir -p "${APPDIR}/usr/libexec/dkr-r"
install -m 0755 "${INPUT_HOST}" \
  "${APPDIR}/usr/libexec/dkr-r/DKR-R-InputHost"
install -m 0755 "${SDL3_LIBRARY}" \
  "${APPDIR}/usr/libexec/dkr-r/libSDL3.so.0"
patchelf --set-rpath '$ORIGIN' \
  "${APPDIR}/usr/libexec/dkr-r/DKR-R-InputHost"
LD_LIBRARY_PATH="${APPDIR}/usr/libexec/dkr-r${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  "${APPDIR}/usr/libexec/dkr-r/DKR-R-InputHost" --self-test --mappings \
  "${APPDIR}/usr/bin/assets/controllers/gamecontrollerdb.txt"

collect_linux_dependency_notices "${APPDIR}"
validate_release_tree "${APPDIR}"
"${APPIMAGE_PLUGIN}" --appdir "${APPDIR}"
[[ -s "${OUTPUT}" ]] || { echo "AppImage output is missing or empty: ${OUTPUT}" >&2; exit 1; }
APPIMAGE_EXTRACT_AND_RUN=1 "${OUTPUT}" --self-test-pak "${PAK_TEST}"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy APPIMAGE_EXTRACT_AND_RUN=1 \
  "${OUTPUT}" --self-test-input-switch "${INPUT_SWITCH_TEST}"
echo "Created ${OUTPUT}"
sha256sum "${OUTPUT}"

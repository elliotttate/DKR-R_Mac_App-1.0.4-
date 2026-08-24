#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ "$(uname -s)" == "Darwin" ]] || {
  echo 'Build-macOS.sh must run on macOS with Xcode command-line tools installed.' >&2
  exit 1
}

for tool in cmake ninja ditto codesign; do
  command -v "${tool}" >/dev/null 2>&1 || {
    echo "Missing required macOS build tool: ${tool}" >&2
    exit 1
  }
done

[[ -d "${project_root}/runtime-recomp/RecompiledFuncs" ]] || {
  echo 'Generated DKR functions are missing. Prepare them on macOS with Prepare-DKR-Runtime-macOS.sh first' >&2
  exit 1
}
[[ -d "${project_root}/extern/n64-modern-runtime" ]] || {
  echo 'N64ModernRuntime is missing. Prepare the pinned dependencies first.' >&2
  exit 1
}
[[ -d "${project_root}/extern/rt64" ]] || {
  echo 'RT64 is missing. Prepare the pinned renderer dependency first.' >&2
  exit 1
}

version="${DKR_RELEASE_VERSION:-$(tr -d '\r\n' < "${project_root}/VERSION")}"
host_arch="$(uname -m)"
architectures="${DKR_MAC_ARCHITECTURES:-${host_arch}}"
archive_arch="$(printf '%s' "${architectures}" | tr ';' '-')"
build_dir="${DKR_MAC_BUILD_DIR:-${project_root}/build/dkr-runtime-macos}"
dist_dir="${project_root}/dist"
stage="${dist_dir}/DKR-R-${version}-macOS-${archive_arch}"
output="${stage}.zip"

[[ ! -e "${stage}" ]] || {
  echo "Release staging already exists: ${stage}" >&2
  exit 1
}
[[ ! -e "${output}" ]] || {
  echo "Release archive already exists: ${output}" >&2
  exit 1
}

cmake -S "${project_root}/runtime-recomp" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${DKR_MACOS_DEPLOYMENT_TARGET:-12.0}" \
  -DCMAKE_OSX_ARCHITECTURES="${architectures}" \
  -DDKRPORT_ROOT="${project_root}" \
  -DDKR_RELEASE_VERSION="${version}" \
  -DDKR_RUNTIME_BUILD_GENERATED=ON \
  -DDKR_RUNTIME_BUILD_RT64=ON
cmake --build "${build_dir}" --parallel "$(sysctl -n hw.logicalcpu)"
ctest --test-dir "${build_dir}" --output-on-failure -R '^DKR'

built_app=""
while IFS= read -r candidate; do
  built_app="${candidate}"
  break
done < <(find "${build_dir}/bin" -type d -name 'DKR-R.app' -print)
[[ -n "${built_app}" && -x "${built_app}/Contents/MacOS/DKR-R" ]] || {
  echo "The macOS DKR-R.app bundle was not produced under ${build_dir}/bin." >&2
  exit 1
}

pak_test="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-pak-test.XXXXXX")"
cleanup() { rm -rf -- "${pak_test}"; }
trap cleanup EXIT
"${built_app}/Contents/MacOS/DKR-R" --self-test-pak "${pak_test}"

mkdir -p "${stage}"
ditto "${built_app}" "${stage}/DKR-R.app"
mkdir -p "${stage}/DKR-R.app/Contents/Resources/ThirdPartyLicenses"
install -m 0644 "${project_root}/assets/ui/Icons/DKR-R8.bmp" \
  "${stage}/DKR-R.app/Contents/MacOS/assets/ui/Icons/DKR-R8.bmp"
install -m 0644 "${project_root}/packaging/RELEASE-README.md" "${stage}/README.md"
install -m 0644 "${project_root}/LICENSE.md" "${stage}/LICENSE.md"
install -m 0644 "${project_root}/THIRD_PARTY.md" "${stage}/THIRD_PARTY.md"
install -m 0644 "${project_root}/runtime-recomp/COPYING-NOTICE.md" \
  "${stage}/COPYING-NOTICE.md"

notices="${stage}/DKR-R.app/Contents/Resources/ThirdPartyLicenses"
install -m 0644 "${project_root}/extern/rt64/LICENSE" "${notices}/RT64-LICENSE.txt"
install -m 0644 "${project_root}/extern/rt64/src/contrib/imgui/LICENSE.txt" \
  "${notices}/Dear-ImGui-LICENSE.txt"
install -m 0644 "${project_root}/extern/n64-modern-runtime/COPYING" \
  "${notices}/N64ModernRuntime-COPYING.txt"
install -m 0644 "${project_root}/extern/n64-modern-runtime/N64Recomp/LICENSE" \
  "${notices}/N64Recomp-LICENSE.txt"
install -m 0644 "${project_root}/packaging/licenses/Jumpman-LICENSE.txt" \
  "${notices}/Jumpman-LICENSE.txt"
install -m 0644 "${project_root}/packaging/licenses/CRT-FILTERS-NOTICE.md" \
  "${notices}/CRT-FILTERS-NOTICE.md"

# Generate a native icon from the approved DKR-R artwork when the standard
# macOS image utilities are available. The app remains valid without it.
if command -v sips >/dev/null 2>&1 && command -v iconutil >/dev/null 2>&1 && \
   [[ -f "${project_root}/assets/ui/Icons/DKR-R8.png" ]]; then
  iconset="${stage}/DKR-R.iconset"
  mkdir -p "${iconset}"
  for size in 16 32 128 256 512; do
    sips -z "${size}" "${size}" "${project_root}/assets/ui/Icons/DKR-R8.png" \
      --out "${iconset}/icon_${size}x${size}.png" >/dev/null
    doubled=$((size * 2))
    sips -z "${doubled}" "${doubled}" "${project_root}/assets/ui/Icons/DKR-R8.png" \
      --out "${iconset}/icon_${size}x${size}@2x.png" >/dev/null
  done
  iconutil -c icns "${iconset}" -o \
    "${stage}/DKR-R.app/Contents/Resources/DKR-R.icns"
  rm -rf -- "${iconset}"
  /usr/libexec/PlistBuddy -c 'Delete :CFBundleIconFile' \
    "${stage}/DKR-R.app/Contents/Info.plist" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string DKR-R' \
    "${stage}/DKR-R.app/Contents/Info.plist"
fi

# Ad-hoc signing keeps the local bundle internally consistent. Public notarised
# releases can replace this signature in CI using an Apple Developer identity.
codesign --force --deep --sign - "${stage}/DKR-R.app"

while IFS= read -r -d '' file; do
  extension="${file##*.}"
  extension="$(printf '%s' "${extension}" | tr '[:upper:]' '[:lower:]')"
  case "${extension}" in
    z64|v64|n64|eep|mpk|o2r|otr)
      echo "macOS release staging contains prohibited game data: ${file}" >&2
      exit 1
      ;;
  esac
  magic="$(od -An -tx1 -N4 "${file}" 2>/dev/null | tr -d '[:space:]')"
  case "${magic}" in
    80371240|37804012|40123780)
      echo "macOS release staging contains an N64 ROM header: ${file}" >&2
      exit 1
      ;;
  esac
done < <(find "${stage}" -type f -print0)

ditto -c -k --sequesterRsrc --keepParent "${stage}" "${output}"
printf '\nmacOS runtime, tests and app packaging passed.\n'
printf 'Graphics backend: RT64 Metal (native Apple path)\n'
printf 'Application: %s\n' "${stage}/DKR-R.app"
printf 'Archive: %s\n' "${output}"
shasum -a 256 "${output}"

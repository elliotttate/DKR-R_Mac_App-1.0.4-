#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/build-logs"
TIMESTAMP="$(date '+%Y%m%d-%H%M%S')"
LOG_PATH="$LOG_DIR/prepare-dkr-runtime-$TIMESTAMP.log"
mkdir -p "$LOG_DIR"
exec > >(tee -a "$LOG_PATH") 2>&1

SKIP_DEPS=0
FORCE=0
SKIP_RECOMPILE=0
SKIP_PROBE=0
BUILD_RENDERER=0
JOBS=0

fail() { echo "[ERROR] $*" >&2; echo "Full log: $LOG_PATH" >&2; exit 1; }
step() { echo; echo "==> $*"; }
usage() {
  cat <<EOF
Usage: $(basename "$0") [options]
  --skip-deps       Skip Homebrew dependency installation
  --force           Discard local changes in managed Git checkouts
  --skip-recompile  Skip N64Recomp translation
  --skip-probe      Skip the generated-code compilation probe
  --build-renderer  Prepare/build RT64
  --jobs N          Parallel build jobs
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-deps) SKIP_DEPS=1 ;;
    --force) FORCE=1 ;;
    --skip-recompile) SKIP_RECOMPILE=1 ;;
    --skip-probe) SKIP_PROBE=1 ;;
    --build-renderer) BUILD_RENDERER=1 ;;
    --jobs) [[ $# -ge 2 ]] || fail "--jobs requires a value"; JOBS="$2"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) fail "Unknown option: $1" ;;
  esac
  shift
done

command -v git >/dev/null || fail "Git was not found."
command -v python3 >/dev/null || fail "python3 was not found."

if [[ "$JOBS" == 0 ]]; then
  CPU_COUNT="$(sysctl -n hw.ncpu)"
  JOBS=$((CPU_COUNT > 1 ? CPU_COUNT - 1 : 1))
fi
[[ "$JOBS" =~ ^[0-9]+$ ]] || fail "--jobs must be a non-negative integer."

step "Preparing macOS dependencies"
if [[ "$SKIP_DEPS" -eq 0 ]]; then
  command -v brew >/dev/null || fail "Homebrew was not found. Install it from https://brew.sh/ and rerun."
  brew install git cmake ninja pkg-config pcre2 python llvm lld binutils make sdl2
  export PATH="$(brew --prefix llvm)/bin:$(brew --prefix binutils)/bin:$PATH"
fi
for cmd in git cmake ninja python3; do command -v "$cmd" >/dev/null || fail "Required command '$cmd' was not found."; done

if [[ "$BUILD_RENDERER" -eq 1 ]] && ! xcrun metal -v >/dev/null 2>&1; then
  echo "[INFO] Metal Toolchain not found. Installing via xcodebuild..."
  xcodebuild -downloadComponent MetalToolchain || fail "Could not install the Metal Toolchain. Run: xcodebuild -downloadComponent MetalToolchain"
fi

# macOS ships GNU Make 3.81 which lacks features the DKR decomp Makefile
# relies on (the != shell-assignment operator, shortest-stem pattern rule
# precedence). Homebrew's make (installed as gmake) is GNU Make 4+.
DECOMP_MAKE="make"
if command -v gmake >/dev/null 2>&1; then
  DECOMP_MAKE="gmake"
  echo "[OK] Using $(gmake --version | head -1) for the decomp build."
elif brew_make="$(brew --prefix make 2>/dev/null)/bin/gmake" && [[ -x "$brew_make" ]]; then
  DECOMP_MAKE="$brew_make"
  echo "[OK] Using $("$brew_make" --version | head -1) for the decomp build."
fi

step "Preparing the pinned DKR decomp checkout"
SOURCE_SCRIPT="$SCRIPT_DIR/Prepare-Game-Source-macOS.sh"
[[ -x "$SOURCE_SCRIPT" ]] || fail "Missing executable: $SOURCE_SCRIPT"
args=()
[[ "$FORCE" -eq 1 ]] && args+=(--force)
"$SOURCE_SCRIPT" ${args[@]+"${args[@]}"}

DKR_SOURCE="$PROJECT_ROOT/extern/dkr-decomp"
[[ -d "$DKR_SOURCE/.git" ]] || fail "DKR source checkout was not prepared."

step "Locating and normalising the user-owned ROM"
EXPECTED_SHA1='0cb115d8716dbbc2922fda38e533b9fe63bb9670'
resolve_rom() {
  local candidates=(
    "$PROJECT_ROOT/runtime/config/rom-source.json"
    "$PROJECT_ROOT/dist/DKRPort-macOS-x64/runtime/config/rom-source.json"
    "$PROJECT_ROOT/build/macos-x64/bin/Release/runtime/config/rom-source.json"
    "$PROJECT_ROOT/build/macos-x64/bin/Debug/runtime/config/rom-source.json"
  )
  local c p
  for c in "${candidates[@]}"; do
    [[ -f "$c" ]] || continue
    p="$(python3 - "$c" <<'PY'
import json, pathlib, sys
try:
    p = json.loads(pathlib.Path(sys.argv[1]).read_text()).get("path")
    if p and pathlib.Path(p).expanduser().is_file():
        print(pathlib.Path(p).expanduser().resolve())
except Exception:
    pass
PY
)"
    if [[ -n "$p" ]]; then
      echo "[OK] Using ROM from launcher configuration: $p" >&2
      printf '%s\n' "$p"
      return
    fi
  done
  if command -v osascript >/dev/null; then
    p="$(osascript <<'APPLESCRIPT'
set f to choose file with prompt "Select your validated Diddy Kong Racing US 1.0 ROM" of type {"z64", "v64", "n64"}
POSIX path of f
APPLESCRIPT
)" || true
    [[ -n "${p:-}" && -f "$p" ]] && { printf '%s\n' "$p"; return; }
  fi
  read -r -p "Path to your validated Diddy Kong Racing US 1.0 ROM: " p
  [[ -f "$p" ]] || fail "ROM file does not exist."
  printf '%s\n' "$(cd "$(dirname "$p")" && pwd)/$(basename "$p")"
}

SOURCE_ROM="$(resolve_rom)"
CANONICAL_ROM="$DKR_SOURCE/baseroms/dkr.us.v77.z64"

# Do ROM conversion separately so errors remain readable.
python3 - "$SOURCE_ROM" "$CANONICAL_ROM" "$EXPECTED_SHA1" <<'PY'
import hashlib, pathlib, struct, sys
src = pathlib.Path(sys.argv[1]).expanduser()
dst = pathlib.Path(sys.argv[2])
expected = sys.argv[3]
b = bytearray(src.read_bytes())
if len(b) < 12:
    raise SystemExit("The selected ROM is too small to contain an N64 header.")
magic = bytes(b[:4])
if magic == bytes.fromhex("80371240"):
    print("[OK] ROM byte order: z64 / big-endian")
elif magic == bytes.fromhex("37804012"):
    print("[OK] ROM byte order: v64 / byte-swapped; normalising locally")
    for i in range(0, len(b)-1, 2): b[i], b[i+1] = b[i+1], b[i]
elif magic == bytes.fromhex("40123780"):
    print("[OK] ROM byte order: n64 / little-endian; normalising locally")
    for i in range(0, len(b)-3, 4):
        a, c = b[i], b[i+1]
        b[i], b[i+1], b[i+2], b[i+3] = b[i+3], b[i+2], c, a
else:
    raise SystemExit(f"Unrecognised N64 ROM byte order (magic {magic.hex().upper()}).")
sha = hashlib.sha1(b).hexdigest()
if sha != expected:
    raise SystemExit(f"The normalised ROM SHA-1 is {sha}, but DKR US 1.0 requires {expected}.")
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_bytes(b)
print(f"[OK] Canonical local build ROM written to: {dst}")
print(f"ROM_ENTRYPOINT=0x{struct.unpack('>I', b[8:12])[0]:08X}")
PY

ROM_HEADER_ENTRYPOINT="$(python3 - "$CANONICAL_ROM" <<'PY'
import pathlib, struct, sys
b = pathlib.Path(sys.argv[1]).read_bytes()
print(f"0x{struct.unpack('>I', b[8:12])[0]:08X}")
PY
)"

step "Ensuring a MIPS cross-toolchain is available"
# The decomp's get-binutils.sh uses nproc (Linux-only) and will fail on
# macOS. Build the MIPS binutils from source ourselves if no cross-tools
# exist. This must happen before gmake parses the Makefile.
(
  cd "$DKR_SOURCE"
  _have_mips_cross=0
  for _pfx in mips64-elf mips-n64 mips64 mips-linux-gnu mips64-linux-gnu mips64-none-elf mips mips-suse-linux; do
    if command -v "${_pfx}-as" >/dev/null 2>&1 && command -v "${_pfx}-ld" >/dev/null 2>&1; then
      _have_mips_cross=1; break
    fi
  done
  if [[ "$_have_mips_cross" -eq 0 && ! -x tools/binutils/mips64-elf-as ]]; then
    echo "[INFO] No MIPS cross-toolchain found; building binutils from source."
    _bu_ver="binutils-2.46.0"
    _bu_tmp="$(mktemp -d "${TMPDIR:-/tmp}/dkr-binutils.XXXXXX")"
    _bu_dest="$(pwd)/tools/binutils"
    mkdir -p "$_bu_dest"
    _llvm_prefix="$(brew --prefix llvm 2>/dev/null)"
    _ar_flags=()
    if [[ -n "$_llvm_prefix" && -x "${_llvm_prefix}/bin/llvm-ar" ]]; then
      _ar_flags=(AR="${_llvm_prefix}/bin/llvm-ar" RANLIB="${_llvm_prefix}/bin/llvm-ranlib")
    fi
    (
      cd "$_bu_tmp"
      curl -fSL "https://ftp.gnu.org/gnu/binutils/${_bu_ver}.tar.xz" -o "${_bu_ver}.tar.xz"
      tar -xf "${_bu_ver}.tar.xz"
      cd "$_bu_ver"
      ./configure --target=mips64-elf --prefix=/usr \
        --with-sysroot=/usr/mips64-elf --with-gnu-as --with-gnu-ld \
        --enable-64-bit-bfd --enable-multilib --enable-plugins \
        --disable-gold --disable-nls --disable-shared --disable-werror \
        ${_ar_flags[@]+"${_ar_flags[@]}"}
      make -j"$JOBS" ${_ar_flags[@]+"${_ar_flags[@]}"}
      cp binutils/ar          "$_bu_dest/mips64-elf-ar"
      cp binutils/objcopy     "$_bu_dest/mips64-elf-objcopy"
      cp binutils/objdump     "$_bu_dest/mips64-elf-objdump"
      cp binutils/strip-new   "$_bu_dest/mips64-elf-strip"
      cp gas/as-new           "$_bu_dest/mips64-elf-as"
      cp ld/ld-new            "$_bu_dest/mips64-elf-ld"
    ) || { rm -rf "$_bu_tmp"; fail "Failed to build MIPS binutils."; }
    rm -rf "$_bu_tmp"
    echo "[OK] MIPS binutils installed to: $_bu_dest"
  else
    echo "[OK] MIPS cross-toolchain is available."
  fi
) || exit 1

step "Building the completed DKR decomp ELF"
(
  cd "$DKR_SOURCE"
  needs_setup=0
  [[ -x .venv/bin/python3 ]] || needs_setup=1
  if [[ "$needs_setup" -eq 0 ]] && ! .venv/bin/python3 -c 'import splat' >/dev/null 2>&1; then needs_setup=1; fi
  if [[ "$needs_setup" -eq 1 ]]; then
    echo "[INFO] DKR Python/tool environment is incomplete; running setup."
    $DECOMP_MAKE setup
  fi
  .venv/bin/python3 -c 'import splat' >/dev/null 2>&1 ||
    { echo "[ERROR] splat is still unavailable in .venv." >&2; exit 92; }
  $DECOMP_MAKE extract
  $DECOMP_MAKE -j"$JOBS"
) || fail "The matching DKR decomp build failed."

ELF_PATH="$DKR_SOURCE/build/dkr.us.v77.elf"
BUILT_ROM_PATH="$DKR_SOURCE/build/dkr.us.v77.z64"
[[ -f "$ELF_PATH" ]] || fail "Expected ELF was not produced: $ELF_PATH"
[[ -f "$BUILT_ROM_PATH" ]] || fail "Expected ROM image was not produced: $BUILT_ROM_PATH"
echo "[OK] Matching DKR ELF: $ELF_PATH"

step "Resolving the DKR static-recompilation entry function"
find_tool() {
  local c
  for c in "$@"; do command -v "$c" >/dev/null 2>&1 && { echo "$c"; return; }; done
}
NM="$(find_tool mips-linux-gnu-nm mips64-linux-gnu-nm llvm-nm nm)"
READELF="$(find_tool mips-linux-gnu-readelf mips64-linux-gnu-readelf llvm-readelf readelf)"
[[ -n "$NM" ]] || fail "No usable nm was found. A MIPS binutils toolchain is required."
[[ -n "$READELF" ]] || fail "No usable readelf was found. A MIPS binutils toolchain is required."
SYMBOLS_LOG="$LOG_DIR/dkr-elf-symbols-$TIMESTAMP.log"
SECTIONS_LOG="$LOG_DIR/dkr-elf-sections-$TIMESTAMP.log"
"$NM" -n --defined-only "$ELF_PATH" >"$SYMBOLS_LOG" 2>&1 || fail "Could not inspect ELF symbols."
"$READELF" -S -W "$ELF_PATH" >"$SECTIONS_LOG" 2>&1 || fail "Could not inspect ELF sections."

ENTRY_DATA="$(python3 - "$SYMBOLS_LOG" "$SECTIONS_LOG" "$ROM_HEADER_ENTRYPOINT" <<'PY'
import pathlib, re, sys
symbols = pathlib.Path(sys.argv[1]).read_text(errors="replace").splitlines()
sections = pathlib.Path(sys.argv[2]).read_text(errors="replace")
header = int(sys.argv[3], 16)
mainproc = None
header_symbol = None
for line in symbols:
    m = re.match(r'^\s*([0-9A-Fa-f]+)\s+([A-Za-z])\s+(\S+)\s*$', line)
    if not m: continue
    addr, typ, name = int(m.group(1),16), m.group(2), m.group(3)
    if name == "mainproc": mainproc = (addr, name)
    if addr == header and typ in "TtWw": header_symbol = (addr, name)
selected = mainproc or header_symbol
if not selected:
    raise SystemExit(f"The ELF contains neither mainproc nor a text symbol at {header:#x}. Symbols: {sys.argv[1]}")
addr, name = selected
if addr & 3: raise SystemExit(f"Resolved entrypoint {name} at {addr:#x} is not word-aligned.")
print(f"0x{addr:08X}")
print(name)
print("true" if re.search(r'(?m)^\s*\[\s*\d+\s+\.mdebug(?:\s|$)', sections) else "false")
PY
)"
ENTRYPOINT_TEXT="$(printf '%s\n' "$ENTRY_DATA" | sed -n '1p')"
ENTRYPOINT_SYMBOL="$(printf '%s\n' "$ENTRY_DATA" | sed -n '2p')"
USE_MDEBUG="$(printf '%s\n' "$ENTRY_DATA" | sed -n '3p')"
echo "[OK] Static recompilation entry function: $ENTRYPOINT_SYMBOL at $ENTRYPOINT_TEXT"
echo "[OK] ELF .mdebug metadata available: $USE_MDEBUG"
echo "[OK] ELF symbol report: $SYMBOLS_LOG"

step "Preparing the static recompilation tool and runtime sources"
RUNTIME_ROOT="$PROJECT_ROOT/runtime-recomp"
MODERN_RUNTIME="$PROJECT_ROOT/extern/n64-modern-runtime"
N64RECOMP="$MODERN_RUNTIME/N64Recomp"
RT64="$PROJECT_ROOT/extern/rt64"
PATCH_MANIFEST="$PROJECT_ROOT/patches/manifest.json"
mkdir -p "$RUNTIME_ROOT"

manifest_commit() {
  python3 - "$PATCH_MANIFEST" "$1" <<'PY'
import json, sys
for d in json.load(open(sys.argv[1]))["dependencies"]:
    if d["name"] == sys.argv[2]:
        print(d["expectedCommit"]); break
else:
    raise SystemExit(f"Dependency {sys.argv[2]} not found in patches/manifest.json")
PY
}

clone_or_update() {
  local name="$1" repo="$2" dest="$3" commit="$4"
  if [[ ! -d "$dest/.git" ]]; then
    mkdir -p "$(dirname "$dest")"
    git clone --recurse-submodules "$repo" "$dest" || fail "Could not clone $name."
  else
    if [[ "$FORCE" -eq 1 ]]; then
      git -C "$dest" reset --hard || fail "Could not reset $name."
      git -C "$dest" clean -fd || fail "Could not clean $name."
    fi
    git -C "$dest" fetch origin "$commit" || fail "Could not update $name."
  fi
  git -C "$dest" checkout --detach "$commit" || fail "Could not select pinned $name commit."
  git -C "$dest" submodule update --init --recursive || fail "Could not initialise $name submodules."
  git -C "$dest" rev-parse HEAD
}

MODERN_COMMIT="$(manifest_commit N64ModernRuntime)"
RT64_COMMIT="$(manifest_commit RT64)"
RESOLVED_MODERN="$(clone_or_update N64ModernRuntime https://github.com/N64Recomp/N64ModernRuntime.git "$MODERN_RUNTIME" "$MODERN_COMMIT")"
[[ -f "$N64RECOMP/CMakeLists.txt" ]] || fail "Pinned N64Recomp submodule is missing: $N64RECOMP"
RESOLVED_N64RECOMP="$(git -C "$N64RECOMP" rev-parse HEAD)"
echo "[OK] N64ModernRuntime: $RESOLVED_MODERN"
echo "[OK] N64Recomp (runtime-pinned submodule): $RESOLVED_N64RECOMP"

RESOLVED_RT64="not requested"
if [[ "$BUILD_RENDERER" -eq 1 ]]; then
  RESOLVED_RT64="$(clone_or_update RT64 https://github.com/rt64/rt64.git "$RT64" "$RT64_COMMIT")"
  echo "[OK] RT64: $RESOLVED_RT64"
  # metal-cpp lives inside plume's contrib but the RT64 patches and DKR-R
  # CMakeLists.txt expect it at rt64/src/contrib/metal-cpp.
  if [[ "$(uname -s)" == "Darwin" && -d "$RT64/src/contrib/plume/contrib/metal-cpp" && ! -e "$RT64/src/contrib/metal-cpp" ]]; then
    ln -s plume/contrib/metal-cpp "$RT64/src/contrib/metal-cpp"
    echo "[OK] Symlinked metal-cpp into rt64/src/contrib/"
  fi
  # hlslpp's scalar fallback uses labs() without including <stdlib.h>.
  # GCC/MSVC pull it in transitively; AppleClang does not.
  _hlslpp_scalar="$RT64/src/contrib/hlslpp/include/hlsl++/platforms/scalar.h"
  if [[ -f "$_hlslpp_scalar" ]] && ! grep -q '<stdlib.h>' "$_hlslpp_scalar"; then
    sed -i '' '/#include <stdint.h>/a\
#include <stdlib.h>' "$_hlslpp_scalar"
    echo "[OK] Patched hlslpp scalar.h: added missing <stdlib.h> for labs()"
  fi
else
  echo "[INFO] RT64 checkout skipped. Add --build-renderer when renderer integration is ready."
fi

step "Applying pinned dependency patches"
PATCH_SCRIPT="$PROJECT_ROOT/scripts/Apply-Dependency-Patches.sh"
[[ -x "$PATCH_SCRIPT" ]] || fail "Missing executable dependency patch script: $PATCH_SCRIPT"
"$PATCH_SCRIPT" || fail "The pinned dependency patch pipeline failed."

step "Building N64Recomp"
TOOL_BUILD="$PROJECT_ROOT/build/runtime-tools/n64recomp"
cmake -S "$N64RECOMP" -B "$TOOL_BUILD" -G Ninja
cmake --build "$TOOL_BUILD" --parallel "$JOBS" --target N64RecompCLI RSPRecomp
RECOMPILER="$(find "$TOOL_BUILD" -type f -name 'N64RecompCLI' -perm -111 -print -quit)"
[[ -n "$RECOMPILER" ]] || RECOMPILER="$(find "$TOOL_BUILD" -type f -name 'N64Recomp' -perm -111 -print -quit)"
RSP_RECOMPILER="$(find "$TOOL_BUILD" -type f -name 'RSPRecomp' -perm -111 -print -quit)"
[[ -n "$RECOMPILER" ]] || fail "N64Recomp executable could not be located."
[[ -n "$RSP_RECOMPILER" ]] || fail "RSPRecomp executable could not be located."
echo "[OK] N64Recomp executable: $RECOMPILER"
echo "[OK] RSPRecomp executable: $RSP_RECOMPILER"

step "Generating N64Recomp configuration"
GENERATED="$RUNTIME_ROOT/RecompiledFuncs"
POLICY="$RUNTIME_ROOT/dkr.us.v77.recomp-policy.json"
TOML="$RUNTIME_ROOT/dkr.us.v77.generated.toml"
mkdir -p "$GENERATED"
[[ "$FORCE" -eq 0 || ! -d "$GENERATED" ]] || rm -rf "$GENERATED"
mkdir -p "$GENERATED"
[[ -f "$POLICY" ]] || fail "DKR recompilation policy is missing: $POLICY"

python3 - "$POLICY" "$TOML" "$ENTRYPOINT_TEXT" "$USE_MDEBUG" "$ELF_PATH" "$BUILT_ROM_PATH" "$GENERATED" <<'PY'
import json, pathlib, sys
policy_path, out_path, entry, mdebug, elf, rom, generated = sys.argv[1:]
p = json.loads(pathlib.Path(policy_path).read_text())
if p.get("schemaVersion") != 1: raise SystemExit("Unsupported DKR recompilation policy schema.")
def q(x): return '"' + str(x).replace("\\", "\\\\").replace('"', '\\"') + '"'
stubs=", ".join(q(x["name"]) for x in p.get("stubs",[]))
renamed=", ".join(q(x["name"]) for x in p.get("renamed",[]))
ignored=", ".join(q(x["name"]) for x in p.get("ignored",[]))
sizes=", ".join(f'{{ name = {q(x["name"])}, size = {x["size"]} }}' for x in p.get("functionSizes",[]))
manual=", ".join(f'{{ name = {q(x["name"])}, section = {q(x["section"])}, vram = {x["vram"]}, size = {x["size"]} }}' for x in p.get("manualFunctions",[]))
patches=[]
for x in p.get("instructionPatches",[]):
    patches.append(f'[[patches.instruction]]\nfunc = {q(x["function"])}\nvram = {x["vram"]}\nvalue = {x["value"]}')
hooks=[]
for x in p.get("functionHooks",[]):
    text=str(x["text"]).replace("\\", "\\\\").replace('"', '\\"')
    hooks.append(f'[[patches.hook]]\nfunc = {q(x["function"])}\nbefore_vram = {x["beforeVram"]}\ntext = "{text}"')
text=f'''# Generated by Prepare-DKR-Runtime.sh.
[input]
entrypoint = {entry}
use_mdebug = {mdebug}
elf_path = {q(pathlib.Path(elf).resolve())}
rom_file_path = {q(pathlib.Path(rom).resolve())}
output_func_path = {q(pathlib.Path(generated).resolve())}
manual_funcs = [{manual}]
function_sizes = [{sizes}]

[patches]
stubs = [{stubs}]
renamed = [{renamed}]
ignored = [{ignored}]

# BEGIN DKR_INSTRUCTION_PATCHES
{chr(10).join(patches)}
# END DKR_INSTRUCTION_PATCHES

# BEGIN DKR_FUNCTION_HOOKS
{chr(10).join(hooks)}
# END DKR_FUNCTION_HOOKS
'''
pathlib.Path(out_path).write_text(text, encoding="utf-8")
PY

if [[ "$SKIP_RECOMPILE" -eq 0 ]]; then
  step "Generating the first native C translation of DKR"
  STDOUT_LOG="$LOG_DIR/n64recomp-$TIMESTAMP.stdout.log"
  STDERR_LOG="$LOG_DIR/n64recomp-$TIMESTAMP.stderr.log"
  set +e
  (cd "$RUNTIME_ROOT" && "$RECOMPILER" "$TOML") >"$STDOUT_LOG" 2>"$STDERR_LOG"
  RC=$?
  set -e
  [[ ! -s "$STDOUT_LOG" ]] || cat "$STDOUT_LOG"
  [[ ! -s "$STDERR_LOG" ]] || cat "$STDERR_LOG" >&2
  if [[ "$RC" -ne 0 ]]; then
    DIAG="$(awk 'NF {gsub(/^[[:space:]]+|[[:space:]]+$/, ""); print; exit}' "$STDERR_LOG" "$STDOUT_LOG")"
    [[ -n "$DIAG" ]] || DIAG="N64Recomp returned no diagnostic text."
    fail "N64Recomp failed with exit code $RC. First diagnostic: $DIAG Full stdout: $STDOUT_LOG Full stderr: $STDERR_LOG"
  fi
  echo "[OK] N64Recomp completed."
fi

FUNCTION_COUNT=0
while IFS= read -r -d '' _; do
  FUNCTION_COUNT=$((FUNCTION_COUNT + 1))
done < <(find "$GENERATED" -type f \( -name '*.c' -o -name '*.cpp' \) -print0)
PROBE_BUILT=false
PROBE_EXECUTABLE=""

if [[ "$SKIP_PROBE" -eq 0 ]]; then
  [[ "$FUNCTION_COUNT" -gt 0 ]] || fail "No generated CPU translation files were found. Rerun without --skip-recompile or use --skip-probe."
  step "Compiling generated DKR CPU code against N64ModernRuntime"
  PROBE_BUILD="$PROJECT_ROOT/build/dkr-runtime-probe"
  RENDERER=OFF
  [[ "$BUILD_RENDERER" -eq 1 ]] && RENDERER=ON
  cmake -S "$RUNTIME_ROOT" -B "$PROBE_BUILD" -G Ninja     "-DDKRPORT_ROOT=$PROJECT_ROOT"     "-DDKR_RUNTIME_BUILD_GENERATED=ON"     "-DDKR_RUNTIME_BUILD_RT64=$RENDERER"
  PROBE_LOG="$LOG_DIR/dkr-runtime-ninja-$TIMESTAMP.log"
  set +e
  cmake --build "$PROBE_BUILD" --target DKRRuntimeProbe --parallel "$JOBS" >"$PROBE_LOG" 2>&1
  RC=$?
  set -e
  if [[ "$RC" -ne 0 ]]; then
    echo "==> Runtime compiler error summary"
    grep -E 'fatal error:|error:|undefined reference|ld:|CMake Error' "$PROBE_LOG" | head -50 || true
    fail "The generated DKR CPU/runtime compilation probe failed. Full build log: $PROBE_LOG"
  fi
  PROBE_EXECUTABLE="$(find "$PROBE_BUILD" -type f -name 'DKRRuntimeProbe' -perm -111 -print -quit)"
  [[ -n "$PROBE_EXECUTABLE" ]] || fail "DKRRuntimeProbe was not found."
  "$PROBE_EXECUTABLE" || fail "DKRRuntimeProbe reported an incomplete generated-code/runtime build."
  PROBE_BUILT=true
  echo "[OK] Runtime compilation probe: $PROBE_EXECUTABLE"
fi

RESOLVED_JSON="$RUNTIME_ROOT/resolved-runtime-dependencies.json"
python3 - "$RESOLVED_JSON" "$EXPECTED_SHA1" "$ROM_HEADER_ENTRYPOINT" "$ENTRYPOINT_TEXT" "$ENTRYPOINT_SYMBOL" "$USE_MDEBUG" "$ELF_PATH" "$FUNCTION_COUNT" "$RECOMPILER" "$RSP_RECOMPILER" "$PROBE_BUILT" "$PROBE_EXECUTABLE" "$BUILD_RENDERER" "$RESOLVED_MODERN" "$RESOLVED_N64RECOMP" "$RESOLVED_RT64" <<'PY'
import json, pathlib, sys
from datetime import datetime, timezone
(out, sha, romentry, entry, symbol, mdebug, elf, count, recomp, rsp, probe, probeexe, renderer, modern, n64recomp, rt64) = sys.argv[1:]
state={
 "schemaVersion":2,
 "preparedUtc":datetime.now(timezone.utc).isoformat().replace("+00:00","Z"),
 "sourceRomSha1":sha,
 "romHeaderEntrypoint":romentry,
 "recompEntrypoint":entry,
 "recompEntrypointSymbol":symbol,
 "useMdebug":mdebug=="true",
 "dkrElf":str(pathlib.Path(elf).resolve()),
 "generatedFunctionFiles":int(count),
 "n64RecompExecutable":str(pathlib.Path(recomp).resolve()),
 "rspRecompExecutable":str(pathlib.Path(rsp).resolve()),
 "runtimeProbeBuilt":probe=="true",
 "runtimeProbeExecutable":str(pathlib.Path(probeexe).resolve()) if probeexe else None,
 "rt64BuildRequested":renderer=="1",
 "dependencies":{"n64ModernRuntime":modern,"n64recomp":n64recomp,"rt64":rt64},
 "nextBoundary":"Register DKR with librecomp, implement SDL audio/input callbacks, recompile F3DDKR and audio RSP microcode, then bind the RT64 render context."
}
pathlib.Path(out).write_text(json.dumps(state,indent=2)+"\n")
PY

echo
echo "[OK] DKR runtime preparation reached the generated-code and N64ModernRuntime compilation boundary."
echo "Generated function files: $FUNCTION_COUNT"
echo "State report: $RESOLVED_JSON"
echo "Full log: $LOG_PATH"

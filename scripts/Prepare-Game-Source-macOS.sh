#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/build-logs"
TIMESTAMP="$(date '+%Y%m%d-%H%M%S')"
LOG_PATH="$LOG_DIR/prepare-game-source-$TIMESTAMP.log"
mkdir -p "$LOG_DIR"
exec > >(tee -a "$LOG_PATH") 2>&1

FORCE=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --force) FORCE=1 ;;
    -h|--help) echo "Usage: $(basename "$0") [--force]"; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
  shift
done

fail() { echo "[ERROR] $*" >&2; echo "Full log: $LOG_PATH" >&2; exit 1; }
command -v git >/dev/null || fail "Git was not found."
command -v python3 >/dev/null || fail "python3 was not found."

LOCK="$PROJECT_ROOT/dependencies.lock.json"
[[ -f "$LOCK" ]] || fail "Dependency lock file was not found: $LOCK"

INFO=()
while IFS= read -r line; do
  INFO+=("$line")
done < <(python3 - "$LOCK" <<'PY'
import json, sys
data=json.load(open(sys.argv[1], encoding="utf-8"))
for entry in data.get("dependencies",[]):
    if entry.get("name") == "dkr-decomp":
        print(entry["destination"])
        print(entry["repository"])
        print(entry["commit"])
        break
else:
    raise SystemExit("The dkr-decomp entry is missing from dependencies.lock.json.")
PY
)

[[ "${#INFO[@]}" -eq 3 ]] || fail "Could not read the dkr-decomp dependency entry."
DEST="$PROJECT_ROOT/${INFO[0]}"
REPO="${INFO[1]}"
COMMIT="${INFO[2]}"

echo "DKR-R game-source preparation"
echo "Project: $PROJECT_ROOT"
echo "Log: $LOG_PATH"
echo "Pinned commit: $COMMIT"
echo "Destination: $DEST"

if [[ -e "$DEST" ]]; then
  [[ -d "$DEST/.git" ]] || fail "$DEST exists but is not a Git checkout."
  DIRTY="$(git -C "$DEST" status --porcelain)"
  if [[ -n "$DIRTY" && "$FORCE" -eq 0 ]]; then
    fail "The existing DKR checkout contains local changes. Commit/stash them, or rerun with --force."
  fi
  if [[ -n "$DIRTY" && "$FORCE" -eq 1 ]]; then
    git -C "$DEST" reset --hard || fail "Could not reset DKR checkout."
    git -C "$DEST" clean -fd || fail "Could not clean DKR checkout."
  fi
else
  mkdir -p "$(dirname "$DEST")"
  git clone --filter=blob:none --no-checkout "$REPO" "$DEST" ||
    fail "Could not clone the DKR decomp repository."
fi

git -C "$DEST" fetch --no-tags origin "$COMMIT" || fail "Could not fetch pinned DKR commit."
git -C "$DEST" checkout --detach "$COMMIT" || fail "Could not check out pinned DKR commit."
git -C "$DEST" submodule update --init --recursive || fail "Could not initialise DKR submodules."

RESOLVED="$(git -C "$DEST" rev-parse HEAD)"
[[ "$RESOLVED" == "$COMMIT" ]] || fail "Pinned revision verification failed. Expected $COMMIT but resolved $RESOLVED."

for required in src include libultra Makefile; do
  [[ -e "$DEST/$required" ]] || fail "The checkout is incomplete; required path is missing: $required"
done

RECORD="$PROJECT_ROOT/extern/.resolved-dkr-source.json"
python3 - "$RECORD" "$REPO" "$RESOLVED" "${INFO[0]}" <<'PY'
import json, pathlib, sys
from datetime import datetime, timezone
out, repo, commit, destination = sys.argv[1:]
p=pathlib.Path(out)
p.parent.mkdir(parents=True, exist_ok=True)
p.write_text(json.dumps({
 "schemaVersion":1,
 "repository":repo,
 "commit":commit,
 "destination":destination,
 "preparedUtc":datetime.now(timezone.utc).isoformat().replace("+00:00","Z")
},indent=2)+"\n")
PY

echo
echo "[OK] The pinned DKR decomp source is ready."
echo "Source: $DEST"
echo "Resolution record: $RECORD"
echo "This command does not copy or download a game ROM or extracted assets."
echo "Full log: $LOG_PATH"

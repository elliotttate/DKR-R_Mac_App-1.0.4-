#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app=""
while IFS= read -r candidate; do
  app="${candidate}"
  break
done < <(find "${project_root}/build/dkr-runtime-macos/bin" -type d -name 'DKR-R.app' -print)
[[ -n "${app}" && -d "${app}" ]] || {
  echo 'DKR-R.app is missing. Run ./Build-macOS.sh first.' >&2
  exit 1
}
open "${app}"

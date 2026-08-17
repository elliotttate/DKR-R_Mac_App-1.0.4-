#!/usr/bin/env bash
set -euo pipefail
for tool in cmake ninja c++; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required tool: $tool" >&2
    echo "Install Xcode Command Line Tools with: xcode-select --install" >&2
    echo "Install CMake and Ninja with: brew install cmake ninja" >&2
    exit 1
  fi
done
cmake --version | head -n1
c++ --version | head -n1
echo "macOS setup checks passed. Run ./Build-macOS.sh."

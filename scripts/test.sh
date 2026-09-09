#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  "$ROOT/scripts/configure.sh"
  "$ROOT/scripts/build.sh"
fi

ctest --test-dir "$BUILD_DIR" --output-on-failure

#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DURATION="${1:-90}"
OUT_DIR="${2:-$ROOT/renders}"

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  "$ROOT/scripts/configure.sh"
fi

cmake --build "$BUILD_DIR" --target pfl_offline_render --parallel
mkdir -p "$OUT_DIR"
"$BUILD_DIR/tests/pfl_offline_render" "$DURATION" "$OUT_DIR"

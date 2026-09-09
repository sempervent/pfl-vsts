#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PRESET="${PRESET:-Release}"

cmake -S "$ROOT" -B "$BUILD_DIR" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE="$PRESET" \
  -DPFL_BUILD_TESTS=ON \
  -DPFL_COPY_PLUGIN_AFTER_BUILD=ON

echo "Configured: $BUILD_DIR ($PRESET)"

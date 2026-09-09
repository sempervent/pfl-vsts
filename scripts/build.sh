#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  "$ROOT/scripts/configure.sh"
fi

cmake --build "$BUILD_DIR" --parallel

echo "Build complete."
echo "Artifacts under: $BUILD_DIR/src/plugins/DroneOrganism/DroneOrganism_artefacts/"

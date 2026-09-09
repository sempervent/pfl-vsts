#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
MODE="${1:-phase2}"
OUT_DIR="${2:-}"

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  "$ROOT/scripts/configure.sh"
fi

cmake --build "$BUILD_DIR" --target pfl_offline_render --parallel

if [[ -z "$OUT_DIR" ]]; then
  if [[ "$MODE" == "phase3" ]]; then
    OUT_DIR="$ROOT/renders/phase3/candidate"
  else
    OUT_DIR="$ROOT/renders/phase2"
  fi
fi

mkdir -p "$OUT_DIR"
"$BUILD_DIR/tests/pfl_offline_render" "$MODE" "$OUT_DIR"

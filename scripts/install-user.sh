#!/usr/bin/env bash
# Plugins are copied by JUCE when PFL_COPY_PLUGIN_AFTER_BUILD=ON.
# This script reports user install locations and optionally forces a rebuild+copy.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

"$ROOT/scripts/build.sh"

echo
echo "User plugin locations (macOS):"
echo "  AU:   ~/Library/Audio/Plug-Ins/Components/PFL Drone Organism.component"
echo "  VST3: ~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3"
echo
echo "Rescan plugins in Ableton Live after install."

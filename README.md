# PFL Generative Instruments

A family of deterministic, generative, playable audio and MIDI plugins for experimental music, live performance, controlled unpredictability, transformation, memory, and interaction.

This repository (`sempervent/pfl-vsts`) is meant to complement the PFL hardware rig—not clone pedals or ship a grab-bag of conventional commercial plugins.

## Philosophy

Software here should do what computers do especially well: seeded composition, musical memory, evolving structure, stateful processing, and simple performance gestures a single human can steer—while staying reproducible and numerically safe.

Shared ideas across the suite include deterministic seeds, versioned algorithms, isolated randomness, and performance controls such as FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE.

## Current Plugins

Six finished instruments with a unified custom PFL interface (Ableton visual PASS):

### PFL Drone Organism

A deterministic generative drone composer (AU/VST3). Host-synced Phrase DNA, macros (SEED, DENSITY, MUTATION, DRIFT, DIRT, SPACE, OUTPUT), and performance commands (FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE).

### PFL Broken Conductor

A deterministic generative MIDI composer (AU/VST3): stereo in (ignored) + silent stereo out + MIDI out. Four-role ensemble, harmonic journey, performance commands. Software complete; physical-rig integration is separate. See `docs/BROKEN_CONDUCTOR.md`.

### PFL Ruin Engine

A deterministic generative audio-degradation environment (AU/VST3). Controls: MIX, AGE, INSTABILITY, OUTPUT, SEED + FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE. See `docs/RUIN_ENGINE.md`.

### PFL Memory Eater

A generative audio-memory organism (AU/VST3). Send-first Ableton Return with MIX=1.0. Controls: MIX, HUNGER, MEMORY, OUTPUT, SEED + performance verbs. See `docs/MEMORY_EATER.md`.

### PFL Pulse Colony

A generative rhythmic audio colony (AU/VST3): ANCHOR / SKITTER / GHOST → one gate/motion stream. Controls: MIX, DENSITY, MUTATION, MOTION, OUTPUT, SEED + performance verbs. See `docs/PULSE_COLONY.md`.

### PFL Signal Parasite

An audio-reactive generative collaborator (AU/VST3): LURKING / ATTACHED / ANSWERING / WITHDRAWN plus FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE. Controls: MIX, SENSITIVITY, HUNGER, MUTATION, OUTPUT, SEED. See `docs/SIGNAL_PARASITE.md`.

## Roadmap

Musical algorithms for the six-plugin suite are parked and Ableton-accepted. Suite UI / release polish is complete (`pfl-suite-ui-release-polish-complete`).

Next only when newly directed:

1. Physical controller / studio integration — deferred until the creative director returns to the studio.
2. No seventh plugin unless listening evidence demands it.

## Shared Architecture

Composition (`src/generative/`), performance commands (`src/performance/`), and DSP (`src/dsp/`) are already shared building blocks for Drone Organism. Later plugins should reuse and carefully generalize these systems when a second real use case appears—not via premature framework extraction. Future domains (analysis, captured-audio memory, additional plugin folders) are architectural direction only until implementation tasks begin.

## Requirements

- macOS Apple Silicon
- Xcode Command Line Tools / Xcode
- CMake ≥ 3.22
- Ninja
- Ableton Live 11+ (for host testing)

Install toolchain (Homebrew):

```bash
brew install cmake ninja
```

## Build

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
```

Release artifacts appear under:

```text
build/src/plugins/DroneOrganism/DroneOrganism_artefacts/Release/
```

With `PFL_COPY_PLUGIN_AFTER_BUILD=ON` (default), plugins also install to:

```text
~/Library/Audio/Plug-Ins/Components/PFL Drone Organism.component
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
```

## Project State

See `PROJECT_STATE.md` for the authoritative session state (versions, known defects, renders, next tasks). Agent working agreements live in `AGENTS.md`.

## License

Project sources are under GPL-3.0 (see `LICENSE`). JUCE is dual-licensed (AGPLv3 / commercial); see `docs/LICENSING.md`.

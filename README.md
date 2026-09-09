# PFL Generative Instruments

A family of deterministic, generative, playable audio and MIDI plugins for experimental music, live performance, controlled unpredictability, transformation, memory, and interaction.

This repository (`sempervent/pfl-vsts`) is meant to complement the PFL hardware rig—not clone pedals or ship a grab-bag of conventional commercial plugins.

## Philosophy

Software here should do what computers do especially well: seeded composition, musical memory, evolving structure, stateful processing, and simple performance gestures a single human can steer—while staying reproducible and numerically safe.

Shared ideas across the suite include deterministic seeds, versioned algorithms, isolated randomness, and performance controls such as FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE.

## Current Plugin

### PFL Drone Organism

Self-composing AU/VST3 dirty-drone instrument (no MIDI required to play). Host-synced generative composition with Phrase DNA / musical memory, continuous macros (SEED, DENSITY, MUTATION, DRIFT, DIRT, SPACE, OUTPUT), and live performance commands (FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE).

**Status:** Implemented reference plugin — functionally complete software milestone. Creative-director listening validation and physical controller/studio integration remain separate follow-ups (see `PROJECT_STATE.md`).

### PFL Broken Conductor (Stage 2 complete)

Deterministic generative MIDI **Instrument** (AU/VST3) for Ableton: stereo in (ignored) + silent stereo out + MIDI out. One Foundation voice with RhythmDNA (algorithm v3). Live DENSITY/MUTATION accepted in Ableton. Load on its own MIDI track; route **MIDI From** to a second track with a stock instrument — **not** after Drone Organism. See `docs/BROKEN_CONDUCTOR.md`.

## Roadmap

Development order (planned unless the creative director changes priority):

1. **PFL Drone Organism** — Implemented. Composition + synthesis + performance control.
2. **PFL Broken Conductor** — Stage 2 complete (RhythmDNA + live DENSITY/MUTATION). Stage 3 multi-voice not started.
3. **PFL Ruin Engine** — Generative transformation of incoming audio; effects as arrangement events.
4. **PFL Memory Eater** — Generative capture / microloop memory (remember, mutate, forget, resurface).
5. **PFL Pulse Colony** — Evolving rhythmic slicer / gate / panner organisms (not static presets).
6. **PFL Signal Parasite** — Later: audio-reactive generative collaborator (deterministic analysis, not ML-by-default).

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

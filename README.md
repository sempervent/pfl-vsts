# PFL Generative Instruments

A family of deterministic, generative, playable audio and MIDI plugins for experimental music, live performance, controlled unpredictability, transformation, memory, and interaction.

This repository (`sempervent/pfl-vsts`) is meant to complement the PFL hardware rig—not clone pedals or ship a grab-bag of conventional commercial plugins.

## Philosophy

Software here should do what computers do especially well: seeded composition, musical memory, evolving structure, stateful processing, and simple performance gestures a single human can steer—while staying reproducible and numerically safe.

Shared ideas across the suite include deterministic seeds, versioned algorithms, isolated randomness, and performance controls such as FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE.

## Current Plugins

### PFL Drone Organism

Self-composing AU/VST3 dirty-drone instrument (no MIDI required to play). Host-synced generative composition with Phrase DNA / musical memory, continuous macros (SEED, DENSITY, MUTATION, DRIFT, DIRT, SPACE, OUTPUT), and live performance commands (FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE).

**Status:** Implemented reference plugin — functionally complete software milestone. Creative-director listening validation and physical controller/studio integration remain separate follow-ups (see `PROJECT_STATE.md`).

### PFL Broken Conductor (Stage 6 complete)

Deterministic generative MIDI **Instrument** (AU/VST3) for Ableton: stereo in (ignored) + silent stereo out + MIDI out. Four-role ensemble, harmonic journey (algorithm v6), performance commands. Software paused — Stage 7 is physical-rig integration. See `docs/BROKEN_CONDUCTOR.md`.

### PFL Ruin Engine (PARKED — Stage 4 complete)

Deterministic generative **audio effect** (AU/VST3): degradation states, accumulated WearState, and performance verbs (FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE). Controls: MIX, AGE, INSTABILITY, OUTPUT, SEED. Algorithm v4. Ableton-accepted through Stage 4; parked as shippable. See `docs/RUIN_ENGINE.md`.

### PFL Memory Eater (Stage 1 complete; Stage 2 in progress)

Generative **audio-memory** effect: bounded short-term history + sparse deterministic microloop recalls. **Send-first**: prefer Ableton Return with MIX=1.0; source via Sends. Controls: MIX, HUNGER, MEMORY, OUTPUT, SEED. See `docs/MEMORY_EATER.md`.

## Roadmap

Development order (planned unless the creative director changes priority):

1. **PFL Drone Organism** — Implemented. Composition + synthesis + performance control.
2. **PFL Broken Conductor** — Stage 6 complete (software). Stage 7 = physical rig.
3. **PFL Ruin Engine** — PARKED at Stage 4 (shippable). Generative transformation of incoming audio.
4. **PFL Memory Eater** — Stage 1 complete (Ableton PASS, send-first). Stage 2 = memory ecology.
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

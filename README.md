# PFL Generative Instruments

Family of generative audio plugins for experimental, dirty, evolving drone music.

## Current focus

**PFL Drone Organism 0.1** — self-composing AU/VST3 instrument (no MIDI required).

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

## Status

See `PROJECT_STATE.md` for the authoritative session state.

## License

Project sources are under GPL-3.0 (see `LICENSE`). JUCE is dual-licensed (AGPLv3 / commercial); see `docs/LICENSING.md`.

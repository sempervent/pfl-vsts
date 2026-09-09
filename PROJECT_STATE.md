# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments / Drone Organism.

## Current development phase

**Phase 1 — Sound** (drone voices implemented; awaiting Ableton audition)

## Current working features

- JUCE 8.0.15 + CMake/Ninja reproducible build (AU / VST3 / Standalone)
- 4 dual-oscillator drone voices (saw/triangle blend), long A/R envelopes
- Per-voice filter + light saturation
- DRIFT wired (cents-scale smoothed instability)
- DENSITY gates active voice count (1–4) in Phase 1
- DIRT partially shapes filter/sat (pre–Phase 3 dirt engine)
- OUTPUT smoothed → DC blocker → safety limiter
- Offline render: `./scripts/render.sh`
- `dsp_smoke` tests green

## Current musical behavior

Manual held D-minor-pentatonic stack (MIDI 38/45/53/60). No generative composition yet. Continuous drone with slow drift; density changes population.

## Last known good Git commit

`101e5eb` — Phase 0 shell (update after Phase 1 commit)

## Build command

```bash
./scripts/configure.sh
./scripts/build.sh
```

## Test command

```bash
./scripts/test.sh
./scripts/render.sh 4 renders/phase1-drone.wav
```

## Plugin artifact paths

```text
build/src/plugins/DroneOrganism/DroneOrganism_artefacts/Release/AU/PFL Drone Organism.component
build/src/plugins/DroneOrganism/DroneOrganism_artefacts/Release/VST3/PFL Drone Organism.vst3
build/src/plugins/DroneOrganism/DroneOrganism_artefacts/Release/Standalone/PFL Drone Organism.app
~/Library/Audio/Plug-Ins/Components/PFL Drone Organism.component
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
```

## Current known problems

- `auval` cannot find adhoc-signed AU (no Apple Developer identity). Prefer VST3 in Ableton if AU does not appear.
- Ableton load/audio not verified in-session.
- Oscillators are naive (not band-limited); fine for dark drones for now.
- Calling `updateVoicesFromParams()` every audio block is heavier than ideal (OK for 4 voices; refine later).

## Unresolved aesthetic questions

- Is the static 4-note stack too consonant / chorale-like for the intended dirt?
- Preferred default DRIFT amount for “subtle analog” vs obvious instability?
- Should Phase 1 default density leave one voice always dark/low?

## Approved musical decisions

None yet (no creative director audition recorded).

## Rejected musical ideas

None yet.

## Next highest-value task

1. You audition Standalone / Ableton VST3 + `renders/phase1-drone.wav`
2. Then Phase 2: deterministic composer + host transport (keep DSP, add MusicalEvent path)

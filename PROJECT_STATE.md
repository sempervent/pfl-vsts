# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments / Drone Organism.

## Current development phase

**Phase 0 complete** → beginning **Phase 1 — Sound**

## Current working features

- JUCE 8.0.15 + CMake/Ninja reproducible build
- AU / VST3 / Standalone targets
- APVTS parameters: seed, density, drift, dirt, space, mutation, output
- Output safety: DC blocker + safety limiter
- Quiet 110 Hz proof tone × Output (Phase 0 audio path)
- `dsp_smoke` tests green
- Standalone binary launches
- Plugins copied to `~/Library/Audio/Plug-Ins/...`

## Current musical behavior

Constant quiet A2 proof tone. Not yet a drone instrument.

## Last known good Git commit

*(updated after Phase 0 commit in this session)*

## Build command

```bash
./scripts/configure.sh
./scripts/build.sh
```

## Test command

```bash
./scripts/test.sh
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

- `auval -v aumu Dro1 PflG` fails: component not found (no Apple Developer codesign identity; adhoc only). Ableton load not yet verified in-session.
- Host load ≠ validated — treat as separate milestones.

## Unresolved aesthetic questions

None yet.

## Approved musical decisions

None yet.

## Rejected musical ideas

None yet.

## Next highest-value task

Phase 1: 3–4 voice dual-oscillator drone with envelopes, filter, light sat, DRIFT, OUTPUT; remove proof tone.

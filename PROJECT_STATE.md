# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 1** — one deterministic Foundation MIDI voice from host PPQ.  
Drone Organism remains the audio reference implementation (Phase 4 performance-engine v1 / Composer v3).

## Last known-good commits / tags

- Pre-BC baseline: tag `drone-organism-pre-broken-conductor` / branch work on `broken-conductor-stage1`
- Drone Organism Phase 4: tag `drone-organism-phase4-performance`
- Phase 3: `5419d83` / `drone-organism-phase3-phrase-dna`
- Phase 2: `aea6cdd` / `drone-organism-phase2-composer`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
```

## Plugin artifacts

```text
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
~/Library/Audio/Plug-Ins/VST3/PFL Broken Conductor.vst3
~/Library/Audio/Plug-Ins/Components/… (AU)
build/src/plugins/*/…_artefacts/Release/{VST3,AU,Standalone}/
```

---

## Drone Organism

| Field | Value |
|-------|--------|
| Status | Software milestone complete (Phase 4); studio/controller + listening validation pending |
| Composer algorithm | **3** |
| Performance-engine | **1** |
| Plugin version | CMake `0.1.0` |
| Approved patches | None yet |

## Broken Conductor

| Field | Value |
|-------|--------|
| Status | **Stage 1 complete** (deterministic one-voice MIDI) |
| ConductorEngine version | **1** |
| Public params | SEED, DENSITY, MUTATION |
| MIDI channel | 1 (fixed) |
| Formats | AU MIDI FX, VST3 Fx, Standalone |
| Tests | `broken_conductor_tests` + full DO regression suite |

### Stage 1 behavior summary

Foundation voice; D min pent; MIDI 26–50; beat grid; durations 1/2/4; rests; velocity 64–96; Phrase DNA pitch; panic on stop/seek.

### Next software task

**Broken Conductor Stage 2** (rhythmic language) — only when explicitly requested.  
Physical controller mapping for Drone Organism remains a separate studio task.

---

## Known defects / limitations

- DO: auval/Ableton notes; no approved patches; mutate subtlety
- BC Stage 1: musically primitive; no performance controls; no multi-voice; no SMF export

## Roadmap order (unchanged)

Drone Organism → Broken Conductor → Ruin Engine → Memory Eater → Pulse Colony → Signal Parasite

# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 1B** — Ableton Live host acceptance (Instrument shell).  
Stage 1 engine preserved at tag `broken-conductor-stage1` / `72d742e`.

## Branch / tags

- Working: `broken-conductor-stage1b-ableton`
- Engine lock: `broken-conductor-stage1` @ `72d742e`
- Pre-BC DO: `drone-organism-pre-broken-conductor`
- DO Phase 4: `drone-organism-phase4-performance`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
```

## Plugin artifacts

```text
~/Library/Audio/Plug-Ins/VST3/PFL Broken Conductor.vst3   # Instrument|Synth, stereo out, MIDI out
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
~/Library/Audio/Plug-Ins/Components/…
```

---

## Drone Organism

| Field | Value |
|-------|--------|
| Status | Software milestone complete (Phase 4); listening/controller validation pending |
| Composer | v3 |
| Performance-engine | v1 |

## Broken Conductor

| Field | Value |
|-------|--------|
| Engine | Stage 1 ConductorEngine (unchanged musically) |
| Host shell | **Stage 1B** Instrument + silent stereo + MIDI out |
| Params | SEED, DENSITY, MUTATION |
| Formats | AU + VST3 (no Standalone) |
| Unit tests | `broken_conductor_tests` + DO regression |

### Ableton host acceptance (manual)

| Check | Status |
|-------|--------|
| VST3 builds / installs | Automated (build) |
| Scanner lists plugin | Awaiting creative-director rescan |
| Instantiates on MIDI track Instrument slot | **Awaiting user confirmation** |
| UI opens (no “could not be opened”) | **Awaiting user confirmation** |
| MIDI From → second track → stock instrument sounds | **Awaiting user confirmation** |
| Stop: no hanging notes | **Awaiting user confirmation** |

See `docs/BROKEN_CONDUCTOR.md` for exact Live 11 routing.

### Next software task

After Stage 1B host confirmation: **Broken Conductor Stage 2** only when explicitly requested.  
Do not start Stage 2 from this milestone.

---

## Roadmap

Drone Organism → Broken Conductor → Ruin Engine → Memory Eater → Pulse Colony → Signal Parasite

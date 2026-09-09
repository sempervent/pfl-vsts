# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 1C** — Forensic Ableton host fix (stereo in+out).  
Awaiting Live open confirmation. Stage 2 not started.  
Tags preserved: `broken-conductor-stage1` / `broken-conductor-stage1b-ableton`.

## Branch / tags

- Working: `broken-conductor-stage1c-host-forensics`
- Stage 1B lock: `broken-conductor-stage1b-ableton` @ `6e43fb6`
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
~/Library/Audio/Plug-Ins/VST3/PFL Broken Conductor.vst3   # Instrument|Synth, stereo in+out, MIDI out
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
| Host shell | **Stage 1C** Instrument + stereo in (ignored) + silent stereo out + MIDI out |
| Root cause (1B fail) | Live: “effect category, but no valid audio input bus” after processor load |
| Params | SEED, DENSITY, MUTATION |
| Formats | AU + VST3 (no Standalone) |
| Unit tests | `broken_conductor_tests` + `plugin_identity_tests` + DO regression |
| Identities | `docs/PLUGIN_IDENTITIES.md` |

### Ableton host acceptance (manual)

| Check | Status |
|-------|--------|
| VST3 builds / installs | Automated (build) |
| Log root cause captured | Done (`analysis/ableton-bc-fail-excerpt.txt`) |
| Identity collision vs DO | Ruled out |
| Scanner lists plugin | Awaiting creative-director rescan of 1C binary |
| Instantiates on MIDI track Instrument slot | **WAITING FOR USER HOST ACCEPTANCE** |
| UI opens (no “could not be opened”) | **WAITING FOR USER HOST ACCEPTANCE** |
| MIDI From → second track → stock instrument sounds | **WAITING FOR USER HOST ACCEPTANCE** |
| Stop: no hanging notes | **WAITING FOR USER HOST ACCEPTANCE** |

See `docs/BROKEN_CONDUCTOR.md` for exact Live 11 routing.

### Next software task

After Stage 1C Live confirmation: **Broken Conductor Stage 2** only when explicitly requested.  
Do not start Stage 2 from this milestone.

---

## Roadmap

Drone Organism → Broken Conductor → Ruin Engine → Memory Eater → Pulse Colony → Signal Parasite

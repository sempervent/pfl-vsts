# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 2** — Rhythmic Language (algorithm v2).  
**WAITING FOR USER STAGE 2 HOST ACCEPTANCE.** Stage 3 not started.

## Branch / tags

- Working: `broken-conductor-stage2-rhythm`
- Stage 1C lock: `broken-conductor-stage1c-ableton-verified` @ `c9bacb2`
- Stage 1B lock: `broken-conductor-stage1b-ableton` @ `6e43fb6`
- Engine Stage 1 lock: `broken-conductor-stage1` @ `72d742e`
- Pre-BC DO: `drone-organism-pre-broken-conductor`
- DO Phase 4: `drone-organism-phase4-performance`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./build/tests/pfl_broken_conductor_render renders/broken-conductor
```

## Plugin artifacts

```text
~/Library/Audio/Plug-Ins/VST3/PFL Broken Conductor.vst3
renders/broken-conductor/stage2-seed-2002.mid
renders/broken-conductor/stage2-metrics.txt
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
| Engine | **Stage 2** RhythmDNA + PhraseDNA (algorithm **v2**) |
| Host shell | Stage 1C stereo in+out Instrument |
| Voices | Foundation only |
| Params | SEED, DENSITY, MUTATION |
| Formats | AU + VST3 |
| Unit tests | `broken_conductor_tests` (v2) + `plugin_identity_tests` + DO regression |

### Ableton host acceptance

| Check | Status |
|-------|--------|
| Stage 1C HOST INSTANTIATION | **VERIFIED** |
| Stage 1C END-TO-END MIDI ROUTING | **NOT YET RECORDED** |
| Stage 2 pluginval | Pass |
| Stage 2 Live open + audible MIDI + syncopation + clean stop | **WAITING FOR USER STAGE 2 HOST ACCEPTANCE** |

### Next software task

After Stage 2 Live confirmation: **Stage 3 multi-voice** only when explicitly requested.

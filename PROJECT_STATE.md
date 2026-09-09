# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 2 — COMPLETE** (algorithm v3, Ableton-accepted).  
Stage 3 multi-voice **NOT STARTED**.

## Branch / tags

- Stage 2 complete: `broken-conductor-stage2-complete` (final accepted Stage 2B tip)
- Stage 2B branch: `broken-conductor-stage2b-control-response`
- Stage 2 rhythm: `broken-conductor-stage2-rhythm` @ `5d33a32`
- Stage 1C: `broken-conductor-stage1c-ableton-verified` @ `c9bacb2`
- Stage 1B: `broken-conductor-stage1b-ableton` @ `6e43fb6`
- Stage 1: `broken-conductor-stage1` @ `72d742e`
- Pre-BC: `drone-organism-pre-broken-conductor` @ `fdbf97f` (also local `main`)

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./build/tests/pfl_broken_conductor_render renders/broken-conductor
```

## Drone Organism

| Field | Value |
|-------|--------|
| Status | Software milestone complete (Phase 4) |
| Composer | v3 |
| Performance-engine | v1 |

## Broken Conductor

| Field | Value |
|-------|--------|
| Status | **Stage 2 COMPLETE** |
| Engine | algorithm **v3** (RhythmDNA + live DENSITY/MUTATION) |
| Host shell | Stage 1C stereo in (ignored) + silent stereo out |
| Voices | Foundation only |
| Params | SEED, DENSITY, MUTATION |

### Ableton host acceptance

| Check | Status |
|-------|--------|
| Stage 1C HOST INSTANTIATION | **VERIFIED** |
| End-to-end MIDI routing + audible target | **VERIFIED** (creative director) |
| Stage 2B live DENSITY response | **PASS** |
| Stage 2B live MUTATION response | **PASS** |
| Control changes without reload/reseed | **PASS** |
| Stage 2 overall | **COMPLETE** |

### Next software task

**Broken Conductor Stage 3** (Foundation + Pulse + Wanderer + Accent) only when explicitly requested.

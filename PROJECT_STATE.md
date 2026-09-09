# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 4 — IN DEVELOPMENT** (deterministic role projection).  
Stage 3 Ableton acceptance: **PASS** (creative director). Tag: `broken-conductor-stage3-complete`.

## Branch / tags

- Stage 4 working: `pr/broken-conductor-stage4-role-projection`
- Stage 3 complete: `broken-conductor-stage3-complete` @ `08df096` (main after PR #5 merge)
- Stage 2 complete: `broken-conductor-stage2-complete` @ `0f42d56`
- Stage 2B branch tip: `pr/broken-conductor-stage2b`
- Stage 2 rhythm: `broken-conductor-stage2-rhythm` @ `5d33a32`
- Stage 1C: `broken-conductor-stage1c-ableton-verified` @ `c9bacb2`
- Stage 1B: `broken-conductor-stage1b-ableton` @ `6e43fb6`
- Stage 1: `broken-conductor-stage1` @ `72d742e`
- Pre-BC: `drone-organism-pre-broken-conductor` @ `fdbf97f`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./build/tests/pfl_broken_conductor_render renders/broken-conductor/stage4
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
| Status | **Stage 4 DRAFT** (role projection; Ableton pending) |
| Engine | algorithm **v4** (full ensemble always) |
| Host shell | Stage 1C stereo in (ignored) + silent stereo out |
| Voices | Four roles; OUTPUT ROLE projects subset after arbitration |
| Params | SEED, DENSITY, MUTATION, OUTPUT ROLE (config) |

### Ableton host acceptance

| Check | Status |
|-------|--------|
| Stage 1C HOST INSTANTIATION | **VERIFIED** |
| End-to-end MIDI routing + audible target | **VERIFIED** |
| Stage 2B live DENSITY / MUTATION | **PASS** |
| Stage 3 ensemble + DENSITY/MUTATION | **PASS** |
| Stage 4 role projection | **WAITING** |

### Next software task

Stage 4 deterministic role projection (draft PR); Stage 5 not started.

# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 6 — COMPLETE** (Ableton-accepted).  
PR #9 merged to `main`.

## Branch / tags

- Stage 6 complete: `broken-conductor-stage6-complete`
- Stage 6 PR: #9 `pr/broken-conductor-stage6-harmonic-form` (merged)
- Stage 5 complete: `broken-conductor-stage5-complete` (PR #8 merged)
- Stage 4 complete: `broken-conductor-stage4-complete`
- Stage 3 complete: `broken-conductor-stage3-complete` @ `dc68bb7`
- Stage 2 complete: `broken-conductor-stage2-complete` @ `0f42d56`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./build/tests/pfl_broken_conductor_render --stage6
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
| Status | **Stage 6 COMPLETE** (Ableton PASS) |
| Engine | algorithm **v6** (HarmonicField + HarmonicJourney) + performance-engine **v1** |
| Host shell | Stage 1C stereo in (ignored) + silent stereo out |
| Continuous params | DENSITY, MUTATION |
| Config | OUTPUT ROLE |
| Performance | FREEZE, SILENCE; MUTATE, COLLAPSE, RESEED |
| Harmony | HOME / SHADOW / LIFT / HAZE / WIDE / DRIFT |

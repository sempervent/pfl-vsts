# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 6 — IN PROGRESS** (harmonic journey / long-form structure).  
Draft PR; awaiting Ableton host acceptance. Do not tag complete / mark ready until PASS.

## Branch / tags

- Stage 6 branch: `pr/broken-conductor-stage6-harmonic-form` (from `main` @ `7e5c379`)
- Stage 5 complete: `broken-conductor-stage5-complete` (PR #8 merged)
- Stage 4 complete: `broken-conductor-stage4-complete`
- Stage 3 complete: `broken-conductor-stage3-complete` @ `dc68bb7`
- Stage 2 complete: `broken-conductor-stage2-complete` @ `0f42d56`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./build/tests/pfl_broken_conductor_render --stage5
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
| Status | **Stage 6 draft** — HarmonicField + HarmonicJourney |
| Engine | algorithm **v5** (+ Stage 6 harmony layer; bump only if generative pitch semantics change) |
| Host shell | Stage 1C stereo in (ignored) + silent stereo out |
| Continuous params | DENSITY, MUTATION |
| Config | OUTPUT ROLE |
| Performance | FREEZE, SILENCE; MUTATE, COLLAPSE, RESEED |

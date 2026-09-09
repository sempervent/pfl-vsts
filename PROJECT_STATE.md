# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 5 — IN PROGRESS** (performance intervention).  
Draft PR; awaiting Ableton host acceptance. Do not tag complete / mark ready until PASS.

## Branch / tags

- Stage 5 branch: `pr/broken-conductor-stage5-performance` (from `main` after PR #7 merge)
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
| Status | **Stage 5 draft** — FREEZE/MUTATE/COLLAPSE/RESEED/SILENCE |
| Engine | algorithm **v5** + performance-engine **v1** |
| Host shell | Stage 1C stereo in (ignored) + silent stereo out |
| Continuous params | DENSITY, MUTATION |
| Config | OUTPUT ROLE |
| Performance | FREEZE, SILENCE (toggle); MUTATE, COLLAPSE, RESEED (edge) |

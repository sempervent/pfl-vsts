# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Ruin Engine Stage 4 — IN PROGRESS** (performance intervention).
Stage 3 complete on `main` (`ruin-engine-stage3-complete`, PR #12 merged).

Broken Conductor software paused after Stage 6 (Stage 7 = physical rig).

## Branch / tags

- Active: `pr/ruin-engine-stage4-performance`
- Stage 3 complete: `ruin-engine-stage3-complete` (PR #12 merged)
- Stage 2 complete: `ruin-engine-stage2-complete` (PR #11 merged)
- Stage 1 complete: `ruin-engine-stage1-complete` (PR #10 merged)
- Stage 6 complete: `broken-conductor-stage6-complete`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./build/tests/pfl_ruin_engine_render --stage4
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
| Status | **Stage 6 COMPLETE** (Ableton PASS); software paused |
| Engine | algorithm **v6** + performance-engine **v1** |

## Ruin Engine

| Field | Value |
|-------|--------|
| Status | **Stage 4 IN PROGRESS** (draft PR) |
| Engine | algorithm **v4** + performance-engine **v1** |
| Formats | AU VST3 (`Fx`) |
| Params | MIX, AGE, INSTABILITY, OUTPUT, SEED |
| Performance | FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE |
| States | INTACT / WEATHERED / FRACTURED / RUINED / RECOVERING |
| Wear | spectral / nonlinear / temporal (internal, persisted) |
| Stage 3 | `ruin-engine-stage3-complete` (PR #12 merged) |
| Stage 2 | `ruin-engine-stage2-complete` (PR #11 merged) |
| Stage 1 | `ruin-engine-stage1-complete` (PR #10 merged) |
| Next | Creative-director Ableton acceptance for Stage 4 |

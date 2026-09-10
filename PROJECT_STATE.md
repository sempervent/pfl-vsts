# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Ruin Engine Stage 3 — IN PROGRESS** (accumulated processing wear).  
Stage 2 complete on `main` (`ruin-engine-stage2-complete`, PR #11 merged).

Broken Conductor software paused after Stage 6 (Stage 7 = physical rig).

## Branch / tags

- Active: `pr/ruin-engine-stage3-aging`
- Stage 2 complete: `ruin-engine-stage2-complete` (PR #11 merged)
- Stage 1 complete: `ruin-engine-stage1-complete` (PR #10 merged)
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
./build/tests/pfl_ruin_engine_render
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
| Status | **Stage 3 IN PROGRESS** (draft PR #12) |
| Engine | algorithm **v3** (WearState processing memory) |
| Formats | AU VST3 (`Fx`) |
| Params | MIX, AGE, INSTABILITY, OUTPUT, SEED |
| States | INTACT / WEATHERED / FRACTURED / RUINED / RECOVERING |
| Wear | spectral / nonlinear / temporal (internal, persisted) |
| Stage 2 | `ruin-engine-stage2-complete` (PR #11 merged) |
| Stage 1 | `ruin-engine-stage1-complete` (PR #10 merged) |
| Next | Creative-director Ableton acceptance for Stage 3 |

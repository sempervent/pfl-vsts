# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Ruin Engine — PARKED** (Stage 4 COMPLETE, Ableton PASS).  
Next active product: **PFL Memory Eater Stage 1**.

Broken Conductor software paused after Stage 6 (Stage 7 = physical rig).

## Branch / tags

- Stage 4 complete: `ruin-engine-stage4-complete` (PR #13)
- Stage 3 complete: `ruin-engine-stage3-complete` (PR #12 merged)
- Stage 2 complete: `ruin-engine-stage2-complete` (PR #11 merged)
- Stage 1 complete: `ruin-engine-stage1-complete` (PR #10 merged)
- Stage 6 complete: `broken-conductor-stage6-complete`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
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
| Status | **PARKED** — Stage 4 COMPLETE (Ableton PASS) |
| Engine | algorithm **v4** + performance-engine **v1** |
| Formats | AU VST3 (`Fx`) |
| Params | MIX, AGE, INSTABILITY, OUTPUT, SEED |
| Performance | FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE |
| Tag | `ruin-engine-stage4-complete` (PR #13) |
| Product decision | v4 sufficiently complete to ship/park; Stage 5 requires new listening evidence |

## Memory Eater

| Field | Value |
|-------|--------|
| Status | **Stage 1 STARTING** |
| Next | Short-term audio memory / microloop recall |

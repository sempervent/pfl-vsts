# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**PFL Memory Eater Stage 1 — IN PROGRESS** (short-term audio recall).
Ruin Engine PARKED at Stage 4 (`ruin-engine-stage4-complete`).

Broken Conductor software paused after Stage 6 (Stage 7 = physical rig).

## Branch / tags

- Active: `pr/memory-eater-stage1`
- Stage 4 complete: `ruin-engine-stage4-complete` (PR #13 merged)
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
| Status | **Stage 1 IN PROGRESS** (draft PR) |
| Engine | algorithm **v1** |
| Formats | AU VST3 (`Fx`) · `Mem1` · `com.pfl.memoryeater` |
| Params | MIX, HUNGER, MEMORY, OUTPUT, SEED |
| Next | Creative-director Ableton acceptance |

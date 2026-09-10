# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Ruin Engine Stage 2 — IN PROGRESS** (generative processing states).  
Stage 1 complete on `main` (`ruin-engine-stage1-complete`, PR #10 merged).

Broken Conductor software paused after Stage 6 (Stage 7 = physical rig).

## Branch / tags

- Active: `pr/ruin-engine-stage2-states`
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
| Status | **Stage 1 COMPLETE** (Ableton PASS) |
| Engine | algorithm **v1** |
| Formats | AU VST3 (`Fx`) |
| Params | MIX, AGE, INSTABILITY, OUTPUT, SEED |
| Buses | mono↔mono, stereo↔stereo |
| Next | Stage 2 — explicit processing states (after PR #10 merge) |

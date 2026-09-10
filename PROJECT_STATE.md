# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**PFL Pulse Colony Stage 3 — IN PROGRESS** (performance intervention).
Stage 2 COMPLETE (Ableton PASS). Memory Eater **PARKED** at Stage 4. Ruin Engine PARKED at Stage 4.

Broken Conductor software paused after Stage 6 (Stage 7 = physical rig).

## Branch / tags

- Stage 3 WIP: `pr/pulse-colony-stage3-performance`
- Stage 2 complete: `pulse-colony-stage2-complete` (PR #19)
- Stage 1 complete: `pulse-colony-stage1-complete` (PR #18)
- Stage 4 complete: `memory-eater-stage4-complete` (PR #17)
- Stage 3 complete: `memory-eater-stage3-complete` (PR #16)
- Stage 2 complete: `memory-eater-stage2-complete` (PR #15)
- Stage 1 complete: `memory-eater-stage1-complete` (PR #14)
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
| Status | **PARKED** — Stage 4 COMPLETE (Ableton PASS) |
| Engine | algorithm **v4** + performance-engine **v1** |
| Formats | AU VST3 (`Fx`) · `Mem1` · `com.pfl.memoryeater` |
| Params | MIX, HUNGER, MEMORY, OUTPUT, SEED |
| Performance | FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE |
| Workflow | **SEND-FIRST** — Return + MIX=1.0; source via Ableton Sends |
| Ecology | short-term ring · 6 slots · gens 0…3 · descendant capture (no feedback) |
| Tags | `memory-eater-stage1-complete` … `memory-eater-stage4-complete` |
| Product decision | **MEMORY EATER v4 IS SUFFICIENTLY COMPLETE TO SHIP AND PARK.** Stage 5 requires new listening evidence or explicit creative-director direction. |

## Pulse Colony

| Field | Value |
|-------|--------|
| Status | **Stage 3 IN PROGRESS** — performance intervention on Stage 2 colony |
| Engine | algorithm **v3** + performance-engine **v1** (Stage 2: v2) |
| Formats | AU VST3 (`Fx`) · `Pls1` · `com.pfl.pulsecolony` |
| Params | MIX, DENSITY, MUTATION, MOTION, OUTPUT, SEED + FREEZE/SILENCE/MUTATE/COLLAPSE/RESEED |
| Roles | ANCHOR · SKITTER · GHOST → ColonyArbiter → one gate/motion |
| Tag (Stage 2) | `pulse-colony-stage2-complete` (PR #19) |
| Acceptance | Stage 2 Ableton PASS preserved when performance idle; Stage 3 Ableton pending |
| Next | Ableton listen → PASS → park |

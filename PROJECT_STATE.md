# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 3 — IN DEVELOPMENT** (algorithm v4, four-voice ensemble).
Draft PR stacks on Stage 2 tip; **Ableton host acceptance PENDING**.
Stage 2 remains recoverable via `broken-conductor-stage2-complete`.

## Branch / tags

- Stage 3 working: `pr/broken-conductor-stage3`
- Stage 2 complete: `broken-conductor-stage2-complete` @ `0f42d56` (content also on `main` via merged PRs #1–#3)
- Stage 2B branch tip: `pr/broken-conductor-stage2b`
- Stage 2 rhythm: `broken-conductor-stage2-rhythm` @ `5d33a32`
- Stage 1C: `broken-conductor-stage1c-ableton-verified` @ `c9bacb2`
- Stage 1B: `broken-conductor-stage1b-ableton` @ `6e43fb6`
- Stage 1: `broken-conductor-stage1` @ `72d742e`
- Pre-BC: `drone-organism-pre-broken-conductor` @ `fdbf97f` (also local `main`)

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./build/tests/pfl_broken_conductor_render renders/broken-conductor/stage3
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
| Status | **Stage 3 DRAFT** (awaiting Ableton acceptance) |
| Engine | algorithm **v4** (Foundation / Pulse / Wanderer / Accent) |
| Host shell | Stage 1C stereo in (ignored) + silent stereo out |
| Voices | Four roles, **one MIDI channel** |
| Params | SEED, DENSITY, MUTATION (no per-role controls yet) |

### Ableton host acceptance

| Check | Status |
|-------|--------|
| Stage 1C HOST INSTANTIATION | **VERIFIED** |
| End-to-end MIDI routing + audible target | **VERIFIED** (creative director) |
| Stage 2B live DENSITY response | **PASS** |
| Stage 2B live MUTATION response | **PASS** |
| Control changes without reload/reseed | **PASS** |
| Stage 2 overall | **COMPLETE** |

### Next software task

**Broken Conductor Stage 3** (Foundation + Pulse + Wanderer + Accent) only when explicitly requested.

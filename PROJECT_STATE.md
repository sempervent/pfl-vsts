# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 4 — COMPLETE** (Ableton-accepted).  
Awaiting PR #7 merge before Stage 5 (performance intervention).

## Branch / tags

- Stage 4 complete: `broken-conductor-stage4-complete` (this tip)
- Stage 4 PR: #7 `pr/broken-conductor-stage4-role-projection` (ready for review; not merged by agent)
- Stage 3 complete: `broken-conductor-stage3-complete` @ `dc68bb7`
- Stage 2 complete: `broken-conductor-stage2-complete` @ `0f42d56`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./build/tests/pfl_broken_conductor_render renders/broken-conductor/stage4
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
| Status | **Stage 4 COMPLETE** (Ableton PASS) |
| Engine | algorithm **v5** (ensemble + role hunger + OUTPUT ROLE projection) |
| Host shell | Stage 1C stereo in (ignored) + silent stereo out |
| Voices | Four roles; OUTPUT ROLE projects subset after arbitration |
| Params | SEED, DENSITY, MUTATION, OUTPUT ROLE (config) |

### Ableton host acceptance

| Check | Status |
|-------|--------|
| Stage 1C HOST INSTANTIATION | **VERIFIED** |
| End-to-end MIDI routing + audible target | **VERIFIED** |
| Stage 2B live DENSITY / MUTATION | **PASS** |
| Stage 3 ensemble + DENSITY/MUTATION | **PASS** |
| Stage 4 role projection + hunger | **PASS** |

### Next software task

**Stage 5 performance intervention** only after PR #7 is merged to `main`. Do not stack Stage 5 on an unmerged draft.

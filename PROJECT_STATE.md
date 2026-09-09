# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 2B** — DENSITY/MUTATION live control response (algorithm v3).  
**WAITING FOR USER STAGE 2B ACCEPTANCE.** Stage 3 not started. Stage 2 not tagged complete.

## Branch / tags

- Working: `broken-conductor-stage2b-control-response`
- Stage 2 baseline (failing controls): `broken-conductor-stage2-rhythm` @ `5d33a32`
- Stage 1C: `broken-conductor-stage1c-ableton-verified` @ `c9bacb2`
- Stage 1B: `broken-conductor-stage1b-ableton` @ `6e43fb6`
- Stage 1 engine: `broken-conductor-stage1` @ `72d742e`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./build/tests/pfl_broken_conductor_render renders/broken-conductor
```

## Broken Conductor

| Field | Value |
|-------|--------|
| Engine | algorithm **v3** (Stage 2B control response) |
| Host | Stage 1C stereo in+out; Live open+route verified at Stage 2 |
| Voices | Foundation only |
| Params | SEED, DENSITY, MUTATION — must be audibly distinct |

### Ableton

| Check | Status |
|-------|--------|
| Stage 1C instantiation | VERIFIED |
| Stage 2 open + MIDI route + audible | VERIFIED (creative director) |
| Stage 2 DENSITY/MUTATION endpoints | **FAILED** → Stage 2B fix |
| Stage 2B endpoint/automation acceptance | **WAITING FOR USER STAGE 2B ACCEPTANCE** |

### Next

After Stage 2B Ableton pass: tag Stage 2 complete. Stage 3 only when explicitly requested.

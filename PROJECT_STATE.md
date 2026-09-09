# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments (`sempervent/pfl-vsts`).

## Current milestone

**Broken Conductor Stage 2** — Rhythmic Language (monophonic Foundation).  
Stage 1C Ableton host instantiation verified and tagged. Stage 3 not started.

## Branch / tags

- Working: `broken-conductor-stage1c-host-forensics` → Stage 2 work continues here (or `broken-conductor-stage2-rhythm`)
- Stage 1C lock: `broken-conductor-stage1c-ableton-verified` @ `407670b`
- Stage 1B lock: `broken-conductor-stage1b-ableton` @ `6e43fb6`
- Engine Stage 1 lock: `broken-conductor-stage1` @ `72d742e`
- Pre-BC DO: `drone-organism-pre-broken-conductor`
- DO Phase 4: `drone-organism-phase4-performance`

## Build / test

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
```

## Plugin artifacts

```text
~/Library/Audio/Plug-Ins/VST3/PFL Broken Conductor.vst3   # Instrument|Synth, stereo in+out, MIDI out
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
~/Library/Audio/Plug-Ins/Components/…
```

---

## Drone Organism

| Field | Value |
|-------|--------|
| Status | Software milestone complete (Phase 4); listening/controller validation pending |
| Composer | v3 |
| Performance-engine | v1 |

## Broken Conductor

| Field | Value |
|-------|--------|
| Engine | Stage 1 → Stage 2 RhythmDNA (algorithm bump pending) |
| Host shell | **Stage 1C** Instrument + stereo in (ignored) + silent stereo out + MIDI out |
| Root cause (1B fail) | Live: “effect category, but no valid audio input bus” after processor load |
| Params | SEED, DENSITY, MUTATION |
| Formats | AU + VST3 (no Standalone) |
| Unit tests | `broken_conductor_tests` + `plugin_identity_tests` + DO regression |
| Identities | `docs/PLUGIN_IDENTITIES.md` |

### Ableton host acceptance (manual)

| Check | Status |
|-------|--------|
| VST3 builds / installs | Automated (build) |
| Log root cause captured | Done (`analysis/ableton-bc-fail-excerpt.txt`) |
| Identity collision vs DO | Ruled out |
| pluginval 1.0.4 | Pass (`analysis/pluginval-bc-stage1c.txt`) |
| **HOST INSTANTIATION** (opens in Live 11.3.43) | **VERIFIED** (creative director, 2026-09-09) |
| **END-TO-END LIVE MIDI ROUTING** (MIDI From → stock instrument audible) | **NOT YET RECORDED** |
| Stop: no hanging notes (in Live) | **NOT YET RECORDED** |

Engine unit tests already prove MIDI generation / pairing / stop panic internally — separate from Live routing.

See `docs/BROKEN_CONDUCTOR.md` for exact Live 11 routing.

### Next software task

**Broken Conductor Stage 2: Rhythmic Language** (in progress).  
Do not start Stage 3 multi-voice until Stage 2 is host-accepted.

---

## Roadmap

Drone Organism → Broken Conductor → Ruin Engine → Memory Eater → Pulse Colony → Signal Parasite

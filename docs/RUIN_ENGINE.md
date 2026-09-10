# PFL Ruin Engine

Deterministic generative audio-transformation effect.

## Status

**Stage 2 COMPLETE** — creative-director Ableton acceptance **PASS**.  
Tag: `ruin-engine-stage2-complete` · PR #11 merged.  
Stage 1: `ruin-engine-stage1-complete` (PR #10 merged).

Broken Conductor software development remains paused (Stage 7 = physical-rig integration).

## Purpose

Treat signal degradation and transformation as compositional events.

## Stage 2 objective

Give Ruin Engine recognizable processing conditions and deterministic structural arcs so it can settle, deteriorate, fracture, become ruined, and recover — rather than merely drifting continuously.

Stage 1 DSP foundation is preserved.

## Processing states

| State | Meaning |
|-------|---------|
| INTACT | Least-damaged wet identity |
| WEATHERED | Aged but useful processed condition |
| FRACTURED | Continuity breaks (wet-path attenuation windows) |
| RUINED | Severe but bounded transformation |
| RECOVERING | Reassembly from damage |

### Transition graph

```text
INTACT → WEATHERED
WEATHERED → INTACT | FRACTURED
FRACTURED → WEATHERED | RUINED
RUINED → RECOVERING
RECOVERING → INTACT | WEATHERED
```

## Public controls (unchanged)

| Control | Role |
|---------|------|
| MIX | dry ↔ ruined (true parallel) |
| AGE | damage eligibility / severity landscape |
| INSTABILITY | restlessness / rate / depth |
| OUTPUT | final level |
| SEED | deterministic universe |

## Signal path

```text
INPUT
  ├──────── DRY ──────────────────┐
  ▼                               │
FILTER → SAT → DELAY              │
  ↓                               │
DC → SafetyLimiter                │
  ↓                               │
× fracture envelope (wet only)    │
  ↓                               │
WET ── MIX ◄──────────────────────┘
  ↓
OUTPUT clamp
```

## Algorithm

Version **2** (explicit-state ecology). Version 1 was continuous profile nudges only.

## Determinism

- 4-beat PPQ eval grid; absolute rebuild on seek/insert (state + fracture schedule)
- Sample-rate noise/micro RNG is **not** reset on seek (delay audio history may differ)
- Isolated streams: `state.transition`, `state.duration`, `state.profile`, `state.fracture`, `state.recovery`

## Explicitly out of Stage 2

FREEZE/MUTATE/COLLAPSE/RESEED/SILENCE · content memory · custom GUI · hardware mapping · Stage 3

## Reference renders

```bash
./build/tests/pfl_ruin_engine_render --stage2
# → renders/ruin-engine/stage2/
```

## Ableton acceptance (Stage 2)

**PASS** (2026-09-09) — creative director confirmed in Ableton Live:

- explicit states are musically useful
- INTACT / WEATHERED / FRACTURED / RUINED / RECOVERING behave as intended
- AGE and INSTABILITY remain perceptually distinct
- AGE .20 / INSTABILITY .80 differs from AGE .90 / INSTABILITY .15
- no host-level issue observed

Next musical direction: **Stage 3 — accumulated processing wear** (not audio-loop memory).

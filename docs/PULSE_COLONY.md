# PFL Pulse Colony

Deterministic generative **rhythmic audio gate** — evolving PulseCell organisms.

## Status

**Stage 1 IN PROGRESS** — single PulseCell (draft PR).

Memory Eater is PARKED. Do not begin Stage 2 until Stage 1 Ableton PASS.

## Musical job

> Impose one reproducible evolving rhythmic organism on incoming audio:
> recognizable pulse phrases, useful silence, related mutations, stereo motion —
> without becoming a fixed-pattern slicer or an SL-2 imitation.

## Stage 1 controls

| Param | Default | Meaning |
|-------|---------|---------|
| MIX | 0.70 | dry ↔ gated wet |
| DENSITY | 0.50 | rhythmic activity / occupancy |
| MUTATION | 0.35 | evolutionary hunger |
| MOTION | 0.35 | wet stereo movement |
| OUTPUT | 0.85 | post-mix level |
| SEED | 2002 | deterministic personality |

## Signal path

```text
in ─► dry ──────────────────────────────┐
     │                                  MIX → OUTPUT
     ▼                                  │
  PulseCell gate → stereo motion → DC → lim → wet ─┘
```

## Explicitly out of scope (Stage 1)

Multi-cell · multiband · performance FREEZE/MUTATE/COLLAPSE · MIDI · audio memory · feedback · studio/controllers · SL-2 pattern banks

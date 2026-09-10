# PFL Pulse Colony

Deterministic generative **rhythmic audio gate** — evolving PulseCell organisms.

## Status

**Stage 1 COMPLETE** — Ableton **PASS**. Tag: `pulse-colony-stage1-complete`.

## CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS (Stage 1)

Confirmed by human Ableton testing:

- Pulse Colony operates as engineered
- the single PulseCell rhythmic model works
- DENSITY and MUTATION behave as intended
- the plugin is safe and usable in Ableton
- the Stage 1 rhythmic foundation is accepted

Accepted findings:

- one PulseCell can establish a recognizable rhythmic organism
- negative space is musically useful
- DENSITY controls activity
- MUTATION controls evolution
- MOTION provides bounded stereo behavior
- Stage 1 does not need audio memory, feedback, or multiband processing
- the concept warrants development into an actual multi-cell colony

## Musical job (Stage 1)

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

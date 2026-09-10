# PFL Pulse Colony

Deterministic generative **rhythmic audio gate** — evolving PulseCell organisms.

## Status

**Stage 2 IN PROGRESS** (draft PR) — multi-cell colony (algorithm **v2**).

Stage 1 COMPLETE — Ableton **PASS**. Tag: `pulse-colony-stage1-complete`.

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

## Musical job (Stage 2)

> Three differentiated rhythmic organisms propose gestures into one bounded
> shared audio stream, compete for limited rhythmic space, respond to one
> another, leave intentional gaps, and evolve independently — a colony,
> not three stacked slicers.

## Controls (unchanged)

| Param | Default | Meaning |
|-------|---------|---------|
| MIX | 0.70 | dry ↔ gated wet |
| DENSITY | 0.50 | **colony-wide** activity budget |
| MUTATION | 0.35 | evolutionary hunger (role-scaled) |
| MOTION | 0.35 | wet stereo movement |
| OUTPUT | 0.85 | post-mix level |
| SEED | 2002 | deterministic personality |

## Signal path (Stage 2)

```text
in ─► dry ──────────────────────────────────────────┐
     │                                              MIX → OUTPUT
     ▼                                              │
  ANCHOR / SKITTER / GHOST proposals                │
          ↓                                         │
     ColonyArbiter → accepted pulses                │
          ↓                                         │
     ONE gate → stereo motion → DC → lim → wet ─────┘
```

## Roles

| Role | Job |
|------|-----|
| ANCHOR | Groove spine — longer opens, stable DNA |
| SKITTER | Propulsion around Anchor — short opens |
| GHOST | Sparse gap punctuation |

## Explicitly out of scope (Stage 2)

Multiband · performance FREEZE/MUTATE/COLLAPSE · MIDI · audio memory · feedback · parallel cell audio sum · public role knobs · studio/controllers

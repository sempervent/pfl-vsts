# PFL Pulse Colony

Deterministic generative **rhythmic audio gate** — evolving PulseCell organisms.

## Status

**Stage 2 COMPLETE** — Ableton **PASS**. Tag: `pulse-colony-stage2-complete`.

Stage 1 COMPLETE — Ableton **PASS**. Tag: `pulse-colony-stage1-complete`.

## CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS (Stage 2)

Confirmed by human Ableton testing:

- the three-cell colony works musically
- ANCHOR / SKITTER / GHOST interaction is accepted
- the ColonyArbiter architecture is accepted
- global DENSITY remains useful
- MUTATION remains distinct from DENSITY
- MOTION remains useful
- the multi-cell implementation adds value beyond Stage 1

Accepted findings:

- Pulse Colony is now genuinely multi-cell
- ANCHOR establishes rhythmic structure
- SKITTER contributes propulsion/syncopation
- GHOST provides sparse gap-oriented punctuation
- proposals feed one ColonyArbiter
- there is still one final audio gate/motion path
- global DENSITY prevents threefold activity inflation
- interaction adds value beyond independent patterns
- Stage 2 is accepted despite known minor musical limitations such as
  occasional high-density slicer character
- accepted Stage 2 musical tuning should NOT be silently retuned during Stage 3

## Musical job (Stage 2)

> Three differentiated rhythmic organisms propose gestures into one bounded
> shared audio stream, compete for limited rhythmic space, respond to one
> another, leave intentional gaps, and evolve independently — a colony,
> not three stacked slicers.

## Controls

| Param | Default | Meaning |
|-------|---------|---------|
| MIX | 0.70 | dry ↔ gated wet |
| DENSITY | 0.50 | **colony-wide** activity budget |
| MUTATION | 0.35 | evolutionary hunger (role-scaled) |
| MOTION | 0.35 | wet stereo movement |
| OUTPUT | 0.85 | post-mix level |
| SEED | 2002 | deterministic personality |

## Signal path

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

Next: Stage 3 performance intervention (FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE), then park.

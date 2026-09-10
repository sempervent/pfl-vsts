# PFL Pulse Colony

Deterministic generative **rhythmic audio gate** — evolving PulseCell organisms.

## Status

**PARKED** — Stages 1–3 COMPLETE (Ableton **PASS**).

Tag: `pulse-colony-stage3-complete` (PR #20).

**PULSE COLONY v3 IS SUFFICIENTLY COMPLETE TO SHIP AND PARK.**

No Stage 4 without new listening evidence or explicit creative-director direction.
Accepted Stage 2/3 musical behavior must NOT be silently retuned.

## CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS (Stage 3)

Confirmed by human Ableton testing:

- FREEZE holds a playable colony groove
- MUTATE changes one organism without unfreezing
- COLLAPSE is an audible population arc (≠ DENSITY fade)
- RESIDUE is useful for breakdown/transition
- RESEED grows a new three-cell colony
- SILENCE mutes total plugin output safely (≠ MIX=0)
- continuous DENSITY / MUTATION / MOTION remain distinct from performance verbs
- Stage 2 idle autonomy remains when no performance command is active

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

## Musical job

> Three differentiated rhythmic organisms propose gestures into one bounded
> shared audio stream, compete for limited rhythmic space, respond to one
> another, leave intentional gaps, and evolve independently — a colony,
> not three stacked slicers — conductible via FREEZE / MUTATE / COLLAPSE /
> RESEED / SILENCE.

## Controls

| Param | Default | Meaning |
|-------|---------|---------|
| MIX | 0.70 | dry ↔ gated wet |
| DENSITY | 0.50 | **colony-wide** activity budget |
| MUTATION | 0.35 | evolutionary hunger (role-scaled) |
| MOTION | 0.35 | wet stereo movement |
| OUTPUT | 0.85 | post-mix level |
| SEED | 2002 | deterministic personality |
| FREEZE | off | latch — pause DNA evolution + hunger; arbiter/gate live |
| SILENCE | off | latch — total output mute (≠ MIX=0) |
| MUTATE | edge | one cell, one bounded MutOp |
| COLLAPSE | edge | 24-beat SWARM→STARVE→FRACTURE→RESIDUE |
| RESEED | edge | new three-cell universe |

## Performance (Stage 3)

Priority: **SILENCE > COLLAPSE > FREEZE > MUTATE**. Algorithm **v3**, performance-engine **v1**.
Idle (no commands) preserves Stage 2 autonomous colony behavior.

## Signal path

```text
in ─► dry ──────────────────────────────────────────┐
     │                                              MIX → OUTPUT → silenceGain
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

## Known limitations (accepted / non-blocking)

- Occasional high-density slicer character (Stage 2)
- FREEZE entry not hard-quantized to bar in all paths
- Stay MutOp may mark DNA edited without structural change
- Seek while paused may reset hunger counters

## Explicitly out of scope

Multiband · MIDI · audio memory · feedback · parallel cell audio sum · public role knobs · studio/controllers · silent Stage 2/3 retunes · Stage 4

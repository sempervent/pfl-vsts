# PFL Memory Eater

Deterministic generative **audio-memory** effect.

## Status

**Stage 1 COMPLETE** — Ableton **PASS**. Tag: `memory-eater-stage1-complete`.

**Stage 2 COMPLETE** — Ableton **PASS**. Tag: `memory-eater-stage2-complete`.

**Stage 3 IN PROGRESS** — generational / bounded self-resampling (draft PR).

Ruin Engine remains PARKED at Stage 4.

## SEND-FIRST (canonical)

```text
SOURCE TRACKS
    ↓ Ableton Sends
MEMORY EATER RETURN
    MIX = 1.0
```

Insert supported. Do **not** route the Return into its own Send.

## Stage 3 architecture

```text
INPUT ──► short-term ring (original only) ──► gen0 promotion
              │
              ▼
         memory ecology (6 slots, gens 0…3)
              │
              ▼
         one-voice recall ──► DC/limiter ──┬──► MIX ← DRY → OUTPUT
                                           │
                                           └──► optional descendant capture scratch
                                                    ↓
                                               child MemorySlot (gen+1)
```

No continuous wet→input feedback. Descendants come only from explicit capture of the internal wet recall path (pre MIX/OUTPUT).

## Generation model

| Gen | Meaning |
|-----|---------|
| 0 | Live-input / ring promotion |
| 1–3 | Descendant of a stored recall (cap = 3) |

Primary mutation is structural (capture offset/length). Mild level + soft-sat copy-loss at birth only.

## Public controls

MIX · HUNGER · MEMORY · OUTPUT · SEED (unchanged)

## Algorithm

Version **3**.

## Ableton acceptance (Stage 3)

**WAITING FOR CREATIVE-DIRECTOR ACCEPTANCE**

Return + MIX 1.0 · HUNGER .50 · MEMORY .70 · 128–256+ beats.
Listen for original memories, then later recognizably related descendants.

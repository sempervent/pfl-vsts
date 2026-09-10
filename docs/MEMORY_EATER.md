# PFL Memory Eater

Deterministic generative **audio-memory** effect.

## Status

**Stage 1 COMPLETE** — Ableton **PASS**. Tag: `memory-eater-stage1-complete` · PR #14.

**Stage 2 COMPLETE** — Ableton **PASS**. Tag: `memory-eater-stage2-complete` · PR #15.

**Stage 3 IN PROGRESS** — generational / bounded self-resampling.

Ruin Engine remains PARKED at Stage 4.

## CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS (Stage 2)

Memory ecology accepted. Recurring fragments, forgetting, and deep callbacks beyond the 32-beat ring are musically useful.

### SEND-FIRST (preserved)

```text
SOURCE TRACKS
    ↓ Ableton Sends
MEMORY EATER RETURN
    MIX = 1.0
```

Insert remains supported. Do not route the Return into its own Send.

## Purpose

Listen to recent audio, retain bounded history, promote longer-lived memories, and (Stage 3) allow remembered fragments to occasionally become parents of descendant memories — without a continuous feedback loop.

## Architecture (Stage 2)

```text
INPUT ──► short-term ring (original only)
              │
              ├─► sparse PROMOTE ──► memory ecology (6 slots)
              │
              ▼
         recall scheduler ──► recent ring OR stored slot
              │
              ▼
         one-voice microloop → DC → limiter → MIX ← DRY → OUTPUT
```

## Public controls

MIX · HUNGER · MEMORY · OUTPUT · SEED

## Algorithm

Stage 2: **v2**. Stage 3 will increment to **v3**.

# PFL Memory Eater

Deterministic generative **audio-memory** effect.

## Status

**Stage 1 COMPLETE** — Ableton **PASS**. Tag: `memory-eater-stage1-complete` · PR #14.

**Stage 2 IN PROGRESS** — generative memory ecology (draft PR).

Ruin Engine remains PARKED at Stage 4.

## Purpose

Listen to recent incoming audio, retain a bounded short-term history, and autonomously recall fragments — including longer-lived remembered motifs that can recur, tire, and eventually be forgotten.

## Send-first workflow (canonical)

```text
SOURCE TRACK
    ├── dry/main
    └── Send ──► MEMORY EATER RETURN (MIX = 1.0)
```

- Prefer Ableton **Return** with **MIX = 1.0** (memory only).
- Source tracks feed via Ableton Sends.
- Insert still works; MIX retained (0=dry, 1=wet-only).
- Do **not** send the Return back into its own Send (DAW feedback).

## Processing memory vs audio memory

| Ruin Engine WearState | Memory Eater |
|-----------------------|--------------|
| Processing condition history | Actual audio samples |
| No capture / playback | Ring + ecology slots + fragment player |

## Stage 2 architecture

```text
INPUT ──► short-term ring (original only)
              │
              ├─► sparse PROMOTE ──► memory ecology (6 slots)
              │
              ▼
         recall scheduler ──► recent ring OR stored slot
              │
              ▼
         one-voice microloop player
              │
              ▼
         DC → limiter → MIX ← DRY → OUTPUT
```

No self-resampling. Wet is never written back into ring or slots.

## Public controls

| Control | Role |
|---------|------|
| MIX | dry ↔ memory (Return: use 1.0) |
| HUNGER | recall activity / spacing |
| MEMORY | historical depth + modest persistence bias |
| OUTPUT | post-mix / Return gain |
| SEED | deterministic personality |

## Memory ecology (Stage 2)

- **6** fixed slots; each owns ≤ **1 beat** stereo fragment (preallocated @ 40 BPM / 96 kHz design)
- Strength, beat-based decay, reinforcement (diminishing), fatigue cooldown
- Sparse promotion; deterministic replacement; never overwrite active playback
- Stored memories survive seek / loop wrap; short-term ring clears
- Project reload: controls only — ecology audio not serialized

## Algorithm

Version **2**.

## Ableton acceptance (Stage 2)

**WAITING FOR CREATIVE-DIRECTOR ACCEPTANCE**

Canonical: Return + MIX 1.0; HUNGER .50; MEMORY .65; listen 64–128 beats for recurring memories beyond the 32-beat ring.

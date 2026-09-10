# PFL Memory Eater

Deterministic generative **audio-memory** effect.

## Status

**Stage 1 IN PROGRESS** — draft PR (`pr/memory-eater-stage1`).
Waiting for creative-director Ableton acceptance.

Ruin Engine is PARKED at Stage 4.

## Purpose

Listen to recent incoming audio, retain a bounded short-term history, and autonomously recall small fragments as sparse microloop events.

## Processing memory vs audio memory

| Ruin Engine WearState | Memory Eater |
|-----------------------|--------------|
| Processing condition history | Actual recent audio samples |
| No capture / playback | Ring buffer + fragment player |

## Stage 1 architecture

```text
INPUT ──► history ring (write original only)
              │
              ▼
         recall scheduler (musical time)
              │
              ▼
         one-voice microloop player
              │
              ▼
         DC → limiter → MIX ← DRY
              │
           OUTPUT
```

## Public controls

| Control | Role |
|---------|------|
| MIX | dry ↔ memory layer (MIX1 = wet-only, silence between recalls is valid) |
| HUNGER | recall activity / spacing (not buffer size) |
| MEMORY | lookback horizon / depth bias (not event density) |
| OUTPUT | post-mix level |
| SEED | deterministic recall personality |

## Memory capacity

- Max history: **32 beats**
- Designed for min tempo **40 BPM**, up to **96 kHz** stereo float
- At tempos below 40 BPM, musical horizon is clamped to available samples (no overflow)
- Allocated in `prepare` only; ~35 MiB worst case @ 96 kHz stereo (tests report)

## Transport

| Event | Policy |
|-------|--------|
| Stop | Pause write + scheduling; keep buffer |
| Seek / loop wrap | Clear audio memory; cancel recall |
| Project reload | Controls restore; audio memory fresh (not serialized) |

## Algorithm

Version **1**.

## Out of Stage 1

Self-resampling · polyphony · reverse/pitch · performance commands · hardware · GUI · Stage 2 ecology

## Ableton acceptance

**WAITING FOR CREATIVE-DIRECTOR ACCEPTANCE**

See PR body for listening procedure.

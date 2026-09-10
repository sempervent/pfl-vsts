# PFL Memory Eater

Deterministic generative **audio-memory** effect.

## Status

**Stage 1 COMPLETE** — creative-director Ableton acceptance **PASS**.
Tag: `memory-eater-stage1-complete` · PR #14.

**Stage 2 IN PROGRESS** — generative memory ecology.

Ruin Engine remains PARKED at Stage 4.

## Purpose

Listen to recent incoming audio, retain a bounded short-term history, and autonomously recall small fragments as sparse microloop events.

## CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS

Stage 1 accepted in Ableton (computer-only listening).

### Product design finding: SEND-FIRST

Memory Eater is substantially more useful on an Ableton **Send/Return** than as a normal insert.

**Canonical workflow:**

```text
SOURCE TRACK
    ├── dry/main signal
    └── Send ──► MEMORY EATER RETURN (MIX = 1.0 → recalled memory only)
```

- Source performance stays on the source track.
- Memory Eater contributes recalled material in parallel on the Return.
- Send level and Return fader mix memory independently of the dry signal.

Insert use remains supported. MIX is retained (0 = dry, 1 = memory-only).
There is **no** separate SEND MODE parameter — use MIX=1.0 on a Return.

**Do not** send the Memory Eater Return back into its own Ableton Send
(that creates DAW-level feedback outside the plugin).

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
| MIX | dry ↔ memory layer (MIX1 = wet-only / Return canonical) |
| HUNGER | recall activity / spacing (not buffer size) |
| MEMORY | lookback horizon / depth bias (not event density) |
| OUTPUT | post-mix / Return gain staging |
| SEED | deterministic recall personality |

## Memory capacity

- Max history: **32 beats**
- Designed for min tempo **40 BPM**, up to **96 kHz** stereo float
- At tempos below 40 BPM, musical horizon is clamped to available samples
- Allocated in `prepare` only; ~35 MiB worst case @ 96 kHz stereo

## Transport

| Event | Policy |
|-------|--------|
| Stop | Pause write + scheduling; keep buffer |
| Seek / loop wrap | Clear short-term audio memory; cancel recall |
| Project reload | Controls restore; audio memory fresh (not serialized) |

## Algorithm

Stage 1: version **1**.

## Out of Stage 1 / deferred

Self-resampling · polyphony · reverse/pitch · performance commands · hardware · GUI

Stage 2: memory ecology (slots, strength, decay, reinforcement, fatigue).

## Ableton acceptance (Stage 1)

**PASS** — creative director.

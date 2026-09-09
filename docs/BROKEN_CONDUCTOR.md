# PFL Broken Conductor

Focused design document for the generative MIDI composer.

## Purpose

A deterministic, host-synchronized generative MIDI composer that develops musical ideas using memory, voice identity, rhythm, rests, Phrase DNA, tonal gravity, controlled mutation, and (later) performance intervention.

It is a **MIDI generator / musical source**, not an audio effect and not a processor for Drone Organism.

## Stages

| Stage | Status |
|-------|--------|
| **1** | Engine + deterministic MIDI tests (`broken-conductor-stage1`) |
| **1B** | Instrument shell + silent stereo out (`broken-conductor-stage1b-ableton`) — still failed Live load |
| **1C** | Ableton host instantiation verified (`broken-conductor-stage1c-ableton-verified`) |
| **2** | Rhythmic Language — algorithm **v2** (awaiting Stage 2 Live acceptance) |
| **3+** | Not started (multi-voice deferred) |

## Architecture

```text
Host tempo / transport / PPQ
        → ConductorEngine (algorithm v2)
              ├─ RhythmDNA (16th cells: X / _ / .)
              ├─ PhraseDNA (pitch degree deltas)
              └─ pitch walk / memory / gravity
        → MidiTraceEvent (sample-accurate via PPQ→offset)
        → juce::MidiBuffer
        → stereo audio in (ignored) + silent stereo out (Ableton shell)
```

```text
src/generative/RhythmDNA.h         # Stage 2 rhythmic ancestry
src/generative/ConductorEngine.h   # Foundation voice + scheduling
src/plugins/BrokenConductor/       # Host wrapper / buses / category
```

Drone Organism still uses `Composer` v3. Broken Conductor does not call `Composer`.

## Host plug-in configuration (Stage 1C — unchanged in Stage 2)

| Flag | Value | Rationale |
|------|-------|-----------|
| `IS_SYNTH` | `TRUE` | Ableton loads it as an **Instrument** |
| `IS_MIDI_EFFECT` | `FALSE` | Live does not treat third-party VST3 as native MIDI Effects |
| `NEEDS_MIDI_INPUT` | `TRUE` | Declared; Stage 1 ignores input notes |
| `NEEDS_MIDI_OUTPUT` | `TRUE` | Generated MIDI |
| `VST3_CATEGORIES` | `Instrument Synth` | Live Instrument browser |
| `AU_MAIN_TYPE` | `kAudioUnitType_MusicDevice` | AU instrument (`aumu`) |
| `FORMATS` | `AU VST3` | Standalone omitted |
| Audio buses | Stereo **input** (ignored) + stereo **output** (silent) | Live 11 required valid audio **input** for this MIDI-out VST3 |

### Ableton Live 11.3.43 acceptance

| Gate | Status |
|------|--------|
| **HOST INSTANTIATION** | **VERIFIED** (Stage 1C) |
| **END-TO-END LIVE MIDI ROUTING** | **NOT YET RECORDED** for Stage 1C; Stage 2 requires re-check |
| Engine MIDI (unit tests) | Proven |

Stage 1B failure log (exact):

```text
VST3: plugin processor successfully loaded: PFL Broken Conductor ...
error: Vst3: plugin has an effect category, but no valid audio input bus
```

## Ableton Live 11 topology (correct)

```text
TRACK: BC SOURCE
  Instrument: PFL Broken Conductor

TRACK: BC TARGET
  MIDI From: BC SOURCE → PFL Broken Conductor
  Monitor: In
  Instrument: Operator (or stock)
```

Do **not** place Broken Conductor after Drone Organism in the audio-effect chain.

## Stage 2 rhythmic language (algorithm v2)

### Grid

- Resolution: **sixteenth notes** (0.25 beat)
- Supported: quarters, eighths, sixteenths
- No triplets in Stage 2
- Time signature: **4/4 only** (bars = 4 beats)

### RhythmDNA

Cells: `X` onset, `_` hold, `.` rest. Phrase length **1–4 bars** (default bias **2**).  
Playback walks absolute sixteenth slots through the cycling DNA — **no per-`processBlock` randomness**.

Occupancy hard cap: **≤ 0.58** (never continuous sixteenths at density 100%).

### Durations

`{4, 2, 1, 0.5, 0.25}` beats, Foundation-biased toward longer values. Short notes are punctuation.

### Accents

Velocity from rhythmic structure (downbeat / quarter / 8th / odd-16th) + small `velocity` stream jitter; clamp **[64, 96]**.

### DENSITY

| Band | dens | Max occupancy | Character |
|------|------|---------------|-----------|
| sparse | 0–0.25 | ≤0.20 | long notes, stillness |
| open | 0.25–0.45 | ≤0.32 | Foundation default lean |
| balanced | 0.45–0.65 | ≤0.42 | 8th syncopation common |
| busy | 0.65–0.85 | ≤0.52 | more activity, still rests |
| dense | 0.85–1.00 | ≤0.58 | busier + rarer 16ths, capped |

Also biases duration weights and syncopation allowance. **Not** “more notes only.”

### MUTATION

Controls RhythmDNA (+ pitch Phrase DNA) **lifespan** and mutation rate. On expiry: **one** bounded op (duration nudge, rest↔onset, ±1 sixteenth phase shift, copy neighbor). Ancestry remains recognizable (`A → A'`).

### RNG streams

| Tag | Role |
|-----|------|
| `rhythm` | RhythmDNA generate + mutate only |
| `phrase` | Pitch Phrase DNA |
| `pitch` | Walk / follow / pitch-eval period |
| `velocity` | Accent jitter |

### Scheduling guarantees

- Decisions at absolute sixteenth PPQ slots with Stage 1 half-open `(from, to]` semantics
- Buffer-size independent at the PPQ event level
- Tempo-independent PPQ traces
- Seek = reseed + silent fast-forward through the same slot iterator

## Current musical behavior

- **One voice:** Foundation only
- **Scale:** D minor pentatonic
- **Register:** MIDI 26–50, start D2
- **Channel:** 1
- Pitch Phrase DNA + memory / gravity (Stage 1 architecture, still valid)

## Transport semantics

| Situation | Behavior |
|-----------|----------|
| Host Stop | Panic note-offs + AllNotesOff |
| Play from ≤ beat 0.25 | `reseed(SEED)` |
| Seek | Panic → silent reconstruct → optional re-articulate |
| SEED change | Panic, then reseed |

## Parameters

`seed`, `density`, `mutation` — Stage 2 meanings above.

## Determinism / tests

`broken_conductor_tests` (algorithm v2): determinism, seeds, buffers including odd sizes, tempos including 93/137, sixteenth grid, syncopation, rests/holds, rhythm mutation, RNG isolation, density activity, stop/seek/long-run/pairing.

Comparison artifacts: `renders/broken-conductor/stage2-*.{txt,mid}`, `stage2-metrics.txt`.

## Validation ladder

```text
compile
  ≠ artifact exists
  ≠ scanner lists plugin
  ≠ host instantiates + UI opens
  ≠ MIDI routable to another track
  ≠ target instrument sounds
  ≠ Stage 2 syncopation audible
```

## Known limitations

- Monophonic Foundation only (no Pulse/Wanderer/Accent)
- 4/4 only; no triplets / microtiming
- Pitch may remain static for stretches (rhythm is the Stage 2 focus)
- AU MusicDevice MIDI-out routing may be weaker than VST3 in Live
- Stage 2 Live end-to-end routing not tagged until creative-director confirmation

## Future stages

**Stage 3 (proposed):** Foundation + Pulse + Wanderer + Accent on one MIDI channel, listening to each other — **not** per-channel routing yet.  
Do not start without an explicit task.

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
| **2** | Rhythmic Language — algorithm v2 @ `5d33a32` |
| **2B** | Control response — algorithm **v3**; creative-director Ableton **PASS** |
| **2 complete** | Tag `broken-conductor-stage2-complete` |
| **3** | Four-voice ensemble — algorithm **v4** (DRAFT; Ableton acceptance pending) |
| **4+** | Per-role channels / routing — not started |

## Architecture

```text
Host tempo / transport / PPQ
        → ConductorEngine (algorithm v4)
              ├─ EnsembleState snapshot
              ├─ VoiceState ×4 (Foundation / Pulse / Wanderer / Accent)
              ├─ EventIntent propose → EnsembleArbiter
              ├─ per-role RhythmDNA + PhraseDNA + RNG streams
              └─ MidiNoteTracker (role-owned pitches, one MIDI channel)
        → MidiTraceEvent (sample-accurate via PPQ→offset)
        → juce::MidiBuffer
        → stereo audio in (ignored) + silent stereo out (Ableton shell)
```

```text
src/generative/EnsembleTypes.h     # Stage 3 roles / intents / budgets
src/generative/RhythmDNA.h         # Stage 2 rhythmic ancestry (+ role bias)
src/generative/ConductorEngine.h   # Ensemble orchestrator
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
| **END-TO-END LIVE MIDI ROUTING** | **VERIFIED** (creative director, Stage 2 acceptance) |
| **Stage 2B live DENSITY / MUTATION** | **PASS** |
| **Stage 2 overall** | **COMPLETE** |
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

### DENSITY (Stage 2B)

| dens | Expression of DNA onsets | Max occupancy | Live response |
|------|--------------------------|---------------|---------------|
| 0.0 | ~12% | 0.10 | very sparse |
| 0.5 | ~58% | 0.36 | normal |
| 1.0 | 100% | 0.58 | busiest Foundation |

Live: expression updates immediately; \|Δdensity\|≥0.25 rebuilds RhythmDNA at next boundary.

### MUTATION (Stage 2B)

| mut | DNA lifespan | Pitch eval | Live response |
|-----|--------------|------------|---------------|
| 0.0 | 12–24 bars | rare / cling | stable |
| 1.0 | 2–6 bars | frequent / freer walk | evolve within ≤4 bars |

Live: rising mutation shortens remaining lifespan (≤1–4 bars). No transport restart required.

### Scheduling guarantees

- Decisions at absolute sixteenth PPQ slots with Stage 1 half-open `(from, to]` semantics
- Buffer-size independent at the PPQ event level
- Tempo-independent PPQ traces
- Seek = reseed + silent fast-forward through the same slot iterator
- Parameter automation is part of the deterministic timeline

### RNG streams

| Tag | Role |
|-----|------|
| `rhythm` | RhythmDNA generate + mutate only |
| `phrase` | Pitch Phrase DNA |
| `pitch` | Walk / follow / pitch-eval period |
| `velocity` | Accent jitter |

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

`broken_conductor_tests` (algorithm **v3**): determinism, seeds, buffers including odd sizes, tempos including 93/137, sixteenth grid, syncopation, rests/holds, rhythm mutation, RNG isolation, density/mutation endpoints, live automation, stop/seek/long-run/pairing.

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
- Pitch may remain static for stretches at low MUTATION (Foundation identity)
- AU MusicDevice MIDI-out routing may be weaker than VST3 in Live
- Stage 3 multi-voice not started

## Future stages

**Stage 3 (proposed):** Foundation + Pulse + Wanderer + Accent on one MIDI channel, listening to each other — **not** per-channel routing yet.  
Do not start without an explicit task.

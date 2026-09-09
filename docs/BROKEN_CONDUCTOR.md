# PFL Broken Conductor

Focused design document for the generative MIDI composer.

## Purpose

A deterministic, host-synchronized generative MIDI composer that develops musical ideas using memory, voice identity, rhythm, rests, Phrase DNA, tonal gravity, controlled mutation, and (later) performance intervention.

Stage 1 proves:

```text
Host tempo / transport / PPQ
        → ConductorEngine (shared generative primitives)
        → MidiTraceEvent
        → MIDI note-on / note-off
```

## Current architecture (Stage 1)

```text
src/generative/
  ConductorEngine.h     # Stage 1 one-voice MIDI brain
  MidiTrace.h           # Non-RT MIDI event trace
  MidiNoteTracker.h     # Note ownership / panic
  Scale, RandomWalk, MusicalMemory, PhraseDNA, MusicalClock, DeterministicRNG

src/plugins/BrokenConductor/
  PluginProcessor.*     # MIDI effect AU/VST3/Standalone
```

Drone Organism continues to use `Composer` (algorithm v3). Stage 1 does **not** route Broken Conductor through `Composer`, so DO autonomous output is unchanged. Shared modules are the concrete primitives above.

Plugin type (JUCE 8.0.15): MIDI effect — `IS_MIDI_EFFECT`, MIDI in+out, `kAudioUnitType_MIDIProcessor`, VST3 `Fx`, empty audio buses.

## Current musical behavior

- **One voice:** Foundation
- **Scale:** D minor pentatonic (D F G A C)
- **Register:** MIDI 26–50 (D1–D3), start D2 (38)
- **Grid:** integer beats (quarter-note)
- **Durations:** {1, 2, 4} beats (deterministic weights from density/mutation)
- **Rests:** yes — rest probability `clamp(0.55 - 0.45*density, 0.10, 0.55)`
- **Velocity:** 64–96, seeded (`velocity` RNG stream)
- **Pitch:** RandomWalk + Phrase DNA + memory (Foundation-like), pitch eval ~4–8 bars
- **Channel:** MIDI channel **1** (fixed)

## MIDI output behavior

- Monophonic: note-off before re-attack of same note if needed
- Explicit note-off at scheduled musical time
- `MidiNoteTracker` owns active notes; panic on stop/seek
- No audio synthesis

## Transport semantics

| Situation | Behavior |
|-----------|----------|
| Host Stop | Panic: note-offs + CC AllNotesOff; no new note-ons |
| Play from ≤ beat 0.25 | `reseed(SEED)` full restart |
| Contiguous play | Advance PPQ; decisions on integer beats |
| Seek (jump >2 beats or backward) | Host panic → silent fast-forward reconstruct (preserves mid-sustain occupancy/RNG) → optional NoteOn re-articulate if still sounding → resume |
| SEED change | Panic host notes, then reseed |

**Note:** Large seeks still reconstruct synchronously in `processBlock` (silent, no pending growth). Extreme jumps may hitch the audio thread; Stage 2+ may defer reconstruct. Standalone currently treats transport as always playing (same pattern as Drone Organism standalone).

## Parameters

| ID | Role |
|----|------|
| `seed` | Deterministic universe |
| `density` | Rest probability + duration bias (**not** DO voice count) |
| `mutation` | Pitch adventurousness + duration bias + Phrase DNA |

No DRIFT / DIRT / SPACE / OUTPUT.

## Determinism guarantees

Same SEED + density + mutation + tempo + host musical timeline + ConductorEngine version → identical high-level MIDI event sequence (PPQ-based). Buffer-size independent for musical events. Tempos change wall time only, not PPQ event lists.

## Known limitations (Stage 1)

- Single voice only
- No performance layer (FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE)
- No multi-channel routing
- No sub-beat rhythm, swing, Euclidean, polymeter
- Seed changes apply immediately (not bar-quantized)
- Seek clears sounding notes (no mid-note continuity)
- No SMF export yet (trace via tests / capture)

## Future stages

2. Rhythmic language (richer rests/durations/syncopation/rhythmic DNA)  
3. Multi-voice roles (Foundation / Pulse / Wanderer / Accent)  
4. Voice routing (channels, mute, range)  
5. Performance layer adapted to MIDI  
6. Expressive MIDI (CC/bend/MPE only if justified)  
7. Physical rig / studio validation  

Do not start Stage 2 without an explicit task.

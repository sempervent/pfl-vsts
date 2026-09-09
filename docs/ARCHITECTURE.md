# Architecture

## Repository layout

```text
src/
  generative/   # Shared musical brain primitives + ConductorEngine (BC)
  performance/  # Drone Organism performance commands (FREEZE…)
  dsp/          # Drone Organism audio DSP
  plugins/
    DroneOrganism/
    BrokenConductor/
```

## Drone Organism (Phase 4)

```text
Host tempo / transport / PPQ
        +
Host automation / UI performance commands
        ↓
PerformanceController (FREEZE MUTATE COLLAPSE RESEED SILENCE)
        ↓
Composer  (algorithm v3 — Phrase DNA; may be composition-locked)
        ↓
MusicalEvent / voice state → DSP voices
        ↓
Dirt → Space → DC → Limiter → out
```

Composer lives in `src/generative/` and has **no** knowledge of oscillators, filters, distortion, or GUI.  
Performance lives in `src/performance/`.

Composer algorithm version: **3**. Performance-engine version: **1**.

## Broken Conductor (Stage 1 / 1B)

```text
Host tempo / transport / PPQ
        ↓
ConductorEngine (algorithm v1 — Foundation MIDI voice)
        ↓
MidiTraceEvent → MidiNoteTracker → juce::MidiBuffer
        ↓
Stereo audio in (ignored) + silent stereo out (Ableton MIDI-out VST3 shell; no synthesis)
```

Stage 1B host flags: `IS_SYNTH`, not `IS_MIDI_EFFECT`; VST3 `Instrument|Synth`.  
See `docs/BROKEN_CONDUCTOR.md` for Ableton routing.

## Audio engine (Drone Organism)

Voices, drift, dirt bus, feedback delay, DC blocker, safety limiter — audio-engine v0.1.

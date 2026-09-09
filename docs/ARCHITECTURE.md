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

## Broken Conductor (Stage 1)

```text
Host tempo / transport / PPQ
        ↓
ConductorEngine (algorithm v1 — Foundation MIDI voice)
        ↓
MidiTraceEvent
        ↓
MidiNoteTracker + juce::MidiBuffer
        ↓
MIDI out (channel 1)
```

`ConductorEngine` reuses `Scale`, `RandomWalk`, `MusicalMemory`, `PhraseDNA`, `MusicalClock`, `DeterministicRNG`.  
It does **not** call `Composer`, so Drone Organism autonomous composition is isolated.

See `docs/BROKEN_CONDUCTOR.md`.

## Audio engine (Drone Organism)

Voices, drift, dirt bus, feedback delay, DC blocker, safety limiter — audio-engine v0.1.

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

## Broken Conductor (Stage 2)

```text
Host tempo / transport / PPQ
        ↓
ConductorEngine (algorithm v4 — four-role ensemble + RhythmDNA + live DENSITY/MUTATION)
        ↓
MidiTraceEvent → MidiNoteTracker → juce::MidiBuffer
        ↓
Stereo audio in (ignored) + silent stereo out (Ableton MIDI-out VST3 shell)
```

RhythmDNA: sixteenth cells (`X`/`_`/`.`), 1–4 bar phrases, bounded mutation.  
Pitch Phrase DNA remains independent (`phrase` vs `rhythm` RNG).  
Stage 1C host flags unchanged. See `docs/BROKEN_CONDUCTOR.md`.

Composer algorithm version: **3** (Drone Organism). Conductor algorithm version: **2**.
Performance-engine version: **1**.

## Audio engine (Drone Organism)

Voices, drift, dirt bus, feedback delay, DC blocker, safety limiter — audio-engine v0.1.

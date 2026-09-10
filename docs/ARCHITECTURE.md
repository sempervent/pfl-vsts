# Architecture

## Repository layout

```text
src/
  generative/   # Shared musical brain primitives + ConductorEngine (BC)
  performance/  # Drone Organism performance commands (FREEZE…)
  dsp/          # Shared audio DSP (DO + Ruin Engine)
  plugins/
    DroneOrganism/
    BrokenConductor/
    RuinEngine/
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

## Broken Conductor (Stage 6)

```text
Host tempo / transport / PPQ
        ↓
ConductorEngine (algorithm v6 — ensemble + RhythmDNA + HarmonicJourney)
        +
ConductorPerformanceController (FREEZE…SILENCE)
        ↓
MidiTraceEvent → MidiNoteTracker → juce::MidiBuffer
        ↓
Stereo audio in (ignored) + silent stereo out (Ableton MIDI-out VST3 shell)
```

See `docs/BROKEN_CONDUCTOR.md`. Software paused after Stage 6.

## Ruin Engine (Stage 1)

```text
Host audio in (mono or stereo)
        +
Host tempo / transport / PPQ
        ↓
RuinEngine (algorithm v1 — AGE/INSTABILITY evolution)
        ↓
dry tap ‖ filter → sat → delay → DC → limiter
        ↓
MIX → OUTPUT clamp → audio out
```

Real AU/VST3 audio effect (`Fx`). See `docs/RUIN_ENGINE.md`.

## Audio engine (shared DSP)

Filter, saturator, dirt bus, feedback delay, DC blocker, safety limiter, RuinEngine core — used by Drone Organism and Ruin Engine.
# Architecture

## Scope

PFL Generative Instruments is a family of plugins sharing one compositional core. Immediate target: **PFL Drone Organism**.

## High-level signal / control flow (target)

```text
Host Transport
     ↓
Composer  (deterministic, seed + musical time)
     ↓
MusicalEvent
   ↙       ↘
Synth      MIDI out   (Broken Conductor later)
Engine
     ↓
Dirt / Space chain
     ↓
DCBlocker → SafetyLimiter → Output
```

Constraints:

- Composer must not depend on oscillator/DSP implementation.
- All randomness derives from a master SEED via independent streams.
- Musical events are scheduled from the audio timeline, not GUI timers.
- Output safety is never bypassable by musical parameters.

## Repository layout

```text
src/
  generative/   # Composer, RNG, scale, walks, events (Phase 2+)
  dsp/          # Voices, FX, safety
  plugins/
    DroneOrganism/
tests/
scripts/
docs/
```

## Plugin formats

Built with JUCE CMake:

- AU
- VST3
- Standalone (dev/test)

## Phase notes

- **Phase 0:** plugin shell + safety path + quiet proof tone
- **Phase 1:** manual drone voices (no generative composer yet)
- **Phase 2+:** deterministic composition engine

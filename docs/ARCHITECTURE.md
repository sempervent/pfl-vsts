# Architecture

## Audio engine v0.1 signal flow

```text
Host transport (Option B gate)
        ↓
3 Voices (dual osc + drift + env + pan)
        ↓
DirtBus (pre-gain, sat, noise, resonant LP)
        ↓
FeedbackDelay (stereo, crossfeed, tanh feedback)
        ↓
Transport fade
        ↓
DCBlocker → SafetyLimiter → OUTPUT → hard ceiling
        ↓
Stereo out
```

Pitch values are assigned in the processor (fixed D2/A2/D3). Oscillators do not own compositional pitch logic.

## Future (Phase 2+)

```text
Host Transport → Composer → MusicalEvent → Voices / MIDI
```

Composer must not depend on oscillator implementation.

## Formats

JUCE 8.0.15 — AU / VST3 / Standalone via CMake + Ninja.

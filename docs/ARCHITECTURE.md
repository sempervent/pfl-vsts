# Architecture

## Phase 2 signal / control flow

```text
Host tempo / transport / PPQ
        ↓
MusicalClock (seek detect, bar grid)
        ↓
Composer  (pitch / memory / density / mutation)
        ↓
MusicalEvent → voice pitch + gate targets
        ↓
Existing audio engine (Voice / Drift / Dirt / Space / Safety)
        ↓
Stereo out
```

Composer lives in `src/generative/` and has **no** knowledge of oscillators, filters, distortion, or GUI.

## Generative modules

```text
DeterministicRNG  MusicalEvent  Scale
MusicalMemory     RandomWalk    MusicalClock
Composer
```

## Audio engine (unchanged responsibility)

Voices, drift, dirt bus, feedback delay, DC blocker, safety limiter — see audio-engine v0.1.

Recoverable via branch/tag: `audio-engine-v0.1` / `drone-organism-audio-v0.1`.

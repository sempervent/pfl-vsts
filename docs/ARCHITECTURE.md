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

## Architecture

```text
src/
  generative/   # Composer, PhraseDNA, RNG, scale, walks, events
  dsp/          # Voices, FX, safety
  plugins/
    DroneOrganism/
```

Composer algorithm version is `Composer::kAlgorithmVersion` (currently **3** = Phrase DNA).

## Audio engine (unchanged responsibility)

Voices, drift, dirt bus, feedback delay, DC blocker, safety limiter — see audio-engine v0.1.

Recoverable via branch/tag: `audio-engine-v0.1` / `drone-organism-audio-v0.1`.

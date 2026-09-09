# Architecture

## Phase 4 signal / control flow

```text
Host tempo / transport / PPQ
        +
Host automation / UI performance commands
        ↓
PerformanceController (FREEZE MUTATE COLLAPSE RESEED SILENCE)
        ↓
Composer  (may be composition-locked)
        ↓
MusicalEvent → voice pitch + gate targets (+ collapse voice cap)
        ↓
Existing audio engine × silenceGain × reseedFade
        ↓
Stereo out
```

Composer lives in `src/generative/` and has **no** knowledge of oscillators, filters, distortion, or GUI.  
Performance lives in `src/performance/` and does not redesign Composer; it locks/mutates/reseeds through a small public API.

## Architecture

```text
src/
  performance/  # PerformanceCommand, PerformanceController
  generative/   # Composer, PhraseDNA, RNG, scale, walks, events
  dsp/          # Voices, FX, safety
  plugins/
    DroneOrganism/
```

Composer algorithm version is `Composer::kAlgorithmVersion` (currently **3** = Phrase DNA).  
Performance-engine version is `pfl::performance::kPerformanceEngineVersion` (**1**).

See `docs/PERFORMANCE.md` for command semantics.

## Audio engine (unchanged responsibility)

Voices, drift, dirt bus, feedback delay, DC blocker, safety limiter — see audio-engine v0.1.

Recoverable via branch/tag: `audio-engine-v0.1` / `drone-organism-audio-v0.1`.

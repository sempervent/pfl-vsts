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
    MemoryEater/
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

## Memory Eater (Stage 4)

```text
Host audio in (mono or stereo)
        +
Host tempo / transport / PPQ + performance commands
        ↓
MemoryEaterPerformanceController (FREEZE MUTATE COLLAPSE RESEED SILENCE)
        ↓
MemoryEaterEngine (algorithm v4 — ring + 6-slot ecology + genealogy)
        ↓
recall voice → DC → limiter → MIX → OUTPUT → silenceGain → audio out
```

Real AU/VST3 audio effect (`Fx`). Send-first Return workflow (MIX=1.0). Never writes wet into the ring. See `docs/MEMORY_EATER.md`.

## Pulse Colony (PARKED — Stage 3 COMPLETE)

```text
Host audio in (mono or stereo)
        +
Host tempo / transport / PPQ
        ↓
PulseColonyPerformanceController (v1)
  FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE
        ↓
PulseColonyEngine (algorithm v3)
  ANCHOR / SKITTER / GHOST PulseDNA proposals
        ↓
  ColonyArbiter (budget / collision / interaction)
        ↓
  ONE accepted gate stream → stereo motion → DC → limiter
        ↓
dry ‖ wet → MIX → OUTPUT → silenceGain (processor)
```

Ableton PASS Stages 1–3. PARKED at v3. Stage 2 idle autonomy preserved when performance idle.
See `docs/PULSE_COLONY.md`.

COLLAPSE: SWARM → STARVE → FRACTURE → RESIDUE (24 beats). FREEZE ≠ MUTATION=0; SILENCE ≠ MIX=0.

## Signal Parasite (Stage 2 COMPLETE — Ableton PASS)

```text
Host audio in (mono or stereo)          Host tempo / transport / PPQ
        |                                        |
        +--- ORIGINAL input tap (before mix) ----+
        |                                        v
        v                              ParasiteFeatureExtractor
       dry                               energy · attack · brightness
        |                                change · balance · fill01
        |                                        |
        |                                        v
        |                              ParasiteStimulusDetector
        |                                ATTACK | SHIFT, queue cap 8
        |                                        |
        |                                        v
        |                              ParasiteRelationshipModel
        |                                RecentStimulusHistory (16 descriptors)
        |                                -> source / attachment / conversation
        |                                   / withdrawal / fatigue pressures
        |                                -> LURKING | ATTACHED | ANSWERING
        |                                   | WITHDRAWN
        |                                        |
        |                                        v
        |                              ParasiteBehavior
        |                                DNA (birth/mutate) + HUNGER gate
        |                                biased by state, same vocabularies
        |                                -> absolute-sample schedule
        |                                        |
        |                                        v
        |                              ONE ParasiteVoice (generated)
        |                                noise -> resonant LP chirp -> AR
        |                                -> saturator -> constant-power pan
        |                                        |
        |                                        v
        |                                   DC -> limiter
        |                                        |
        +----------------> MIX -----------------+
                            |
                            v
                        OUTPUT -> clamp -> audio out
```

Ableton PASS Stages 1–2. Tags `signal-parasite-stage1-complete`,
`signal-parasite-stage2-complete`. Shared editor sizing via
`PflGenericEditorSizing.h` accepted. See `docs/SIGNAL_PARASITE.md`.

The wet path is **generated**, never the input reprocessed. Analysis reads the
original input only, so the parasite cannot trigger on its own output. No FFT,
no pitch tracking. Performance verbs arrive in Stage 3. SENSITIVITY owns
detection, HUNGER owns response density, MUTATION owns grammar drift only.
Response onsets are scheduled in absolute samples, so the event stream is
independent of the host buffer size.

## Audio engine (shared DSP)

Filter, saturator, dirt bus, feedback delay, DC blocker, safety limiter, RuinEngine core — used by Drone Organism and Ruin Engine. Memory Eater / Pulse Colony / Signal Parasite reuse DC/limiter/smoother.
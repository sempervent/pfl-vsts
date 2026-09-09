# Decisions

## 2026-09-08 — Framework: JUCE 8.0.15 + CMake + Ninja

### Context

Environment audit:

- macOS Apple Silicon, Xcode present, Ableton Live 11 Suite present
- `cmaj` / Cmajor toolchain **not** installed
- CMake and Ninja were missing; installed via Homebrew for the project

### Decision

Use **JUCE 8.0.15** fetched by CMake `FetchContent`, generator **Ninja**, C++20.

### Why not Cmajor first

Preferred progression listed Cmajor first, but the installed toolchain does not make Cmajor practical here (binary/CLI absent). Installing and learning an additional language runtime would delay a loadable AU/VST3. JUCE directly yields AU + VST3 + Standalone with host transport APIs we need.

### Why not JUCE 9.x yet

JUCE 9.0.2 exists. Pinned to **8.0.15** for a well-exercised CMake AudioPlugin example path and fewer unknowns on first bring-up. Revisit 9.x after Phase 1–2 are musically stable.

### Consequences

- Reproducible configure/build via `scripts/`
- Private prototype development is compatible with JUCE under AGPLv3 terms for non-distributed use; public/commercial distribution requires license review (`docs/LICENSING.md`)
- Composition engine stays in portable C++ under `src/generative/` for later MIDI reuse

---

## 2026-09-08 — Phase 3 primary intervention: Phrase DNA

### Problem

Pitch events are sparse (good for drone stillness) but lack recognizable ancestry. When pitches do move, sequences do not establish motifs — local random-walk wandering without lineage.

### Evidence

- No creative-director FEEDBACK.md / approved generative patches yet
- `PROJECT_STATE` known weakness: sparse changes; identity questions unanswered
- Phase 2 traces (64 bars, dens=0.45, mut=0.35): seed 2002 → 6 note changes; seed 3003 → 10; no phrase-level structure
- Pitch paths revisit notes only by chance; MusicalMemory only penalizes, does not propose motifs
- Constant novelty / busy-ness are **not** dominant (opposite: low event rate)
- Increasing raw event probability forbidden as sole fix for “too static”

### Chosen intervention

**Phrase DNA** (composer algorithm version **3**)

### Why

Creates A → A′ → A″ lineage so sparse events can still feel related, without Weather (needs clearer chapters/activity) or Weirdness Budget (targets excess novelty we do not have) or FREEZE/MUTATE (performance layer once organism is already strong).

### Alternatives rejected for now

- Weirdness Budget — novelty is rare, not excessive
- Weather — premature without phrase-level identity; risk of preset-like arcs
- Performance FREEZE/MUTATE — no approved seeds yet; autonomy still needs identity work
- Simply raising mutation/density — activity ≠ music

### Expected musical effect

Recognizable degree-step motifs that evolve by single-element mutation every ~8–32 bars; pitch changes often follow DNA while free walk remains available; stillness preserved.

---

## 2026-09-08 — Phase 4: Performance layer above Composer

### Context

Phase 3 established Phrase DNA (v3). Phase 4 turns the organism into a steerable live instrument without redesigning Composer or adding another autonomous generative subsystem.

### Decisions

1. **PerformanceController** owns FREEZE/MUTATE/COLLAPSE/RESEED/SILENCE; Composer gains only `setCompositionLocked` + `applyManualMutation`.
2. **Composer algorithm version stays 3** — autonomous output unchanged when no performance commands fire. Introduce **performance-engine v1** instead.
3. **FREEZE** = composition lock (not audio freeze); discarded evolution, not queued catch-up.
4. **MUTATE** ≠ MUTATION param; uses isolated `manualMutation` RNG; one DNA element; works while frozen.
5. **COLLAPSE** = 8-bar staged trajectory ending in COLLAPSED residue; overrides FREEZE; MUTATE ignored during COLLAPSING.
6. **RESEED** = deterministic seed derivation + `applySeedAtBar` (new epoch, no bar-1 rewind); seed always written to SEED.
7. **SILENCE** = highest priority; ~5 ms ramp; pauses composition.
8. **Project restore**: persistent macros/seed; transient freeze/collapse/silence reset safely.
9. **No hardware MIDI maps** in this phase — control-ready only.

### Consequences

See `docs/PERFORMANCE.md`. Ready for external controller mapping after listening validation.

---

## 2026-09-09 — Broken Conductor Stage 1 architecture

### Context

Second real plugin use case for the generative brain. Stage 1 must emit deterministic MIDI without changing Drone Organism algorithm v3 behavior.

### Decisions

1. **Do not modify Composer autonomous behavior** for Stage 1. Keep `Composer::kAlgorithmVersion = 3` and DO regression tests as the lock.
2. **Share concrete modules** already in `src/generative/`: `DeterministicRNG`, `Scale`, `MusicalClock`, `MusicalMemory`, `RandomWalk`, `PhraseDNA`, `MusicalEvent`. No speculative plugin framework.
3. **Add `ConductorEngine`** (Broken Conductor Stage 1 brain): one Foundation voice with beat-grid rhythm, rests, and explicit durations — reusing pitch primitives (walk / memory / Phrase DNA) without routing through the drone `Composer` population model.
4. **DENSITY / MUTATION semantics differ** from Drone Organism: density → rest probability + duration bias; mutation → pitch adventurousness + duration bias + Phrase DNA (same as DO pitch DNA). Not voice count / audio macros.
5. **MIDI plugin type**: `IS_MIDI_EFFECT TRUE`, `NEEDS_MIDI_OUTPUT TRUE`, `AU_MAIN_TYPE kAudioUnitType_MIDIProcessor`, `VST3_CATEGORIES Fx`, empty audio buses (JUCE Arpeggiator pattern). Validated against pinned JUCE 8.0.15.
6. **MIDI channel**: fixed channel 1 (no public channel param in Stage 1).
7. **Seek policy**: all-notes-off / clear ownership → deterministic `ConductorEngine` reconstruct from seed via fast-forward to target PPQ → resume. Safety over mid-note continuity.
8. **Performance layer** (FREEZE/MUTATE/COLLAPSE/RESEED/SILENCE): deferred to later BC stages.

### Consequences

Broken Conductor Stage 1 proves host PPQ → shared primitives → MIDI note-on/off. Multi-voice / performance controls / Euclidean rhythm wait for later stages.

---

## 2026-09-09 — Broken Conductor Stage 1B Ableton host shell

### Context

Stage 1 VST3 (`IS_MIDI_EFFECT` / `Fx` / zero audio buses) compiled and passed unit tests but Ableton Live 11 reported **“This VST3 plug-in could not be opened.”** User also placed it after Drone Organism in the audio-effect portion of a MIDI track.

### Evidence

- On-disk VST3 `moduleinfo.json`: Sub Categories `["Fx"]`, no audio bus layout; JUCE MIDI-effect pattern.
- Ableton does not treat third-party VST3 as native MIDI Effects; MIDI generators need Instrument-class loading + cross-track **MIDI From**.
- Placement after an Instrument is audio-domain and cannot feed MIDI into that instrument.

### Decisions

1. **Supersede Stage 1 host flags** for Ableton: `IS_SYNTH TRUE`, `IS_MIDI_EFFECT FALSE`, `VST3_CATEGORIES Instrument Synth`, `AU_MAIN_TYPE MusicDevice`.
2. **Silent stereo output bus** required for Live; clear every block; **no** test oscillator.
3. Keep `NEEDS_MIDI_OUTPUT TRUE` (and MIDI input declared).
4. **Disable Standalone** for Broken Conductor (does not prove DAW MIDI routing).
5. **Do not change ConductorEngine** Stage 1 musical behavior.
6. Document two-track Ableton topology; forbid DO → BC audio-chain placement.
7. Unit tests ≠ host acceptance; document validation ladder.

### Consequences

Broken Conductor appears in Live as an Instrument with silent audio and MIDI out. Creative director must confirm load + MIDI routing + audible target instrument.

---

## 2026-09-09 — Broken Conductor Stage 1C Ableton audio input bus

### Context

Stage 1B Instrument shell still failed Ableton instantiate with “could not be opened,” alone on a MIDI track.

### Evidence (Live 11.3.43 `Log.txt`)

```text
VST3: plugin processor successfully loaded: PFL Broken Conductor
error: Vst3: plugin has an effect category, but no valid audio input bus
error: VST3: No valid input bus could be found
error: VST3: Failed: PFL Broken Conductor
```

Identity collision with Drone Organism ruled out (unique `PLUGIN_CODE` / CIDs / bundle IDs). Architecture, dylibs, quarantine ruled out.

### Decisions

1. **One targeted fix:** declare stereo **audio input** + stereo output; `isBusesLayoutSupported` accepts matching mono/stereo in+out.
2. Input audio is **ignored**; output remains silent; **ConductorEngine unchanged**.
3. Do not cycle VST3 categories further without new log evidence.
4. Add `docs/PLUGIN_IDENTITIES.md` + `plugin_identity_tests` (unique codes + BC must keep `.withInput`).
5. Host milestone incomplete until Live opens the device and MIDI routing is confirmed.

### Consequences

Ableton’s MIDI-out VST3 path requires a valid audio input bus even when scanned as `instr`. Silent-out-only instruments that also produce MIDI fail Live bus setup.


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

---

## 2026-09-09 — Stage 1C Ableton host instantiation verified

### Context

Creative director confirmed the Stage 1C binary opens and works as a loadable device in Ableton Live 11.3.43.

### Recorded vs not recorded

| Claim | Status |
|-------|--------|
| Host instantiation (device opens, no “could not be opened”) | **VERIFIED** |
| End-to-end Live MIDI From → stock instrument audible | **NOT YET RECORDED** |
| Engine MIDI generation (unit tests) | Proven separately |

### Decisions

1. Tag `broken-conductor-stage1c-ableton-verified` after documentation of Live open confirmation; do not rewrite earlier tags.
2. Proceed to Stage 2 Rhythmic Language without revisiting the bus-layout diagnosis unless new evidence appears.
3. Future PFL MIDI-generator VST3s targeting Live 11 must expose a valid audio input bus (stereo in ignored + silent stereo out is the proven shell).

---

## 2026-09-09 — Broken Conductor Stage 2 Rhythmic Language

### Context

Stage 1C Ableton instantiation verified. Single Foundation voice still used a coarse integer beat grid (`{1,2,4}` durations, per-beat rest draws). Goal: recognizable rhythmic ancestry before multi-voice Stage 3.

### Subagent reconciliation

| Source | Kept |
|--------|------|
| Archaeologist | Split `rhythm` from pitch-eval draws; DNA must own onsets |
| Designer | RhythmDNA cells X/_/.; 16th grid; occupancy caps; 4 mutation ops |
| Scheduling | Absolute slot iterator `(from,to]`; no per-block RNG |
| Critic | Stillness quota; no 100% fill; bounded mutation; long bias |

Rejected for v2: triplets, pattern banks, Euclidean, multi-voice, per-block Bernoulli.

### Decisions

1. **`ConductorEngine::kAlgorithmVersion = 2`**. Stage 1 outputs remain at tag `broken-conductor-stage1`.
2. **`RhythmDNA` / `RhythmEngine`** in `src/generative/RhythmDNA.h`; playback walks cells.
3. **Grid:** 0.25 beat; durations `{0.25,0.5,1,2,4}` with long bias; max occupancy **0.58**.
4. **DENSITY** → occupancy band + duration/syncopation; **MUTATION** → DNA lifespan + one-op mutate.
5. **RNG:** `rhythm` exclusive to RhythmDNA; pitch-eval period moves to `pitch` stream.
6. Host bus shell unchanged (Stage 1C). Stage 2 requires fresh Live acceptance for MIDI timing.
7. Stage 3 not started.

## 2026-09-09 — Broken Conductor Stage 2B control response

### Context

Creative director: Live opens and routes MIDI, but DENSITY/MUTATION at 1.0 did not audibly change a repetitive low-pitch passage.

### Root cause (confirmed by wiring audit)

Host values **did** reach `ConductorEngine` every `processBlock`. Failure was generative:

1. RhythmDNA occupancy/cells baked at birth; live density only updated floats.
2. Lifespan (8–32 bars) not shortened when MUTATION rose — up to 32-bar dead zone.
3. Occupancy band too narrow (0.20–0.58); pitch eval too rare (~3–5 bars × ~35% at mut=1).

### Decisions

1. **Expression gate:** density scales which DNA onsets fire (immediate, deterministic hash).
2. **Large density jump (|Δ|≥0.25):** rebuild RhythmDNA at next musical boundary (`reborn-density`).
3. **MUTATION up:** shorten remaining rhythm/phrase lifespan (≤1–4 bars).
4. Widen occupancy (0.10–0.58); mut=1 DNA lifespan 2–6; stronger pitch chance/period; lower followBias floor (0.25).
5. Algorithm version **3**. Stage 3 multi-voice not started.
6. Do not tag Stage 2 complete until Ableton endpoint/automation acceptance.

---

## 2026-09-09 — Broken Conductor Stage 2 complete (Ableton acceptance)

### Context

Creative director passed Stage 2B Ableton acceptance: open, MIDI route, audible target, live DENSITY, live MUTATION, no reload/reseed required.

### Decisions

1. Tag `broken-conductor-stage2-complete` at the accepted tip.
2. Stage 2 is closed; Stage 3 multi-voice remains not started.
3. Publish as a stacked GitHub PR series (Stage 1 / Stage 2 rhythm / Stage 2B controls).

---

## 2026-09-09 — Broken Conductor Stage 3 multi-voice ensemble (design)

### Problem

Stage 2 ships one Foundation voice. Stage 3 must become a four-role generative ensemble on **one MIDI channel** that occupies different musical jobs, reacts to one another, leaves space, and forms a coherent composition — not four independent random generators.

### Musical objective

Listener can infer, without labels: something anchors, something pulses, something wanders, something occasionally interrupts. Accent should almost feel underused.

### Chosen architecture

```text
Host timeline
  → EnsembleState snapshot (shared, immutable for the slot)
  → Role-specific EventIntent proposals (independent RNG streams)
  → EnsembleArbiter (congestion / gap / call-response / collision / budget)
  → MusicalEvents / MidiTraceEvent
  → MidiNoteTracker (role-owned notes)
```

Algorithm version **4**. Two-phase propose → commit. No Stage 4 channel routing. No new public controls beyond SEED / DENSITY / MUTATION.

### Voice-role definitions

| Role | Register | Duration bias | Activity | Mutation sensitivity |
|------|----------|---------------|----------|----------------------|
| Foundation | 26–50 | 4 / 2 / 1 | Sparse long anchor | Low (0.25×) |
| Pulse | 38–62 | 1 / 0.5 / 0.25 | Offbeat propulsion | Medium (0.7×) |
| Wanderer | 50–74 | 1 / 0.5 / 2 | Gap-sensitive melody | High (1.35×) |
| Accent | 62–86 | 0.25 / 0.5 | Rare punctuation | Timing high / pitch low |

Directional listen only: Foundation → Pulse; Foundation (+ Pulse holes) → Wanderer; (F∧P∧W cues) → Accent. No full mesh.

### Coordination model

1. **Congestion avoidance** — high recent ensemble activity lowers Wanderer/Accent entry.
2. **Gap filling** — sustained empty windows raise Wanderer/Accent eligibility.
3. **Call/response** — Foundation pitch change may open a Wanderer answer window; Pulse gesture end may open an Accent punctuation window (probabilistic, not guaranteed).

Global **activity budget** from DENSITY (internal; not a parameter). Yield order when oversubscribed: Accent → Wanderer → Pulse → Foundation. Foundation remains audible at dens=1. Aggregate occupancy ceiling ~0.50 (below Stage 2 solo max 0.58).

### Collision policy

One owner per `(channel, pitch)`. Same-pitch overlap between roles is forbidden.

When a proposed pitch is already owned: try nearest free scale tone, then octave displacement within role range; else suppress the lower-priority new event. Priority for preserving sustained material: Foundation > Pulse > Wanderer > Accent. Higher-priority new claims may preempt lower-priority owners only after remap fails.

**Explicit:** If Foundation and Pulse both attempt MIDI 50 overlapping on ch.1, only one owns 50 — typically Foundation keeps/claims it; Pulse remaps or is suppressed. Pulse must never NoteOff Foundation’s 50.

### DENSITY semantics

Controls global budget, role presence curves, expression rates, rest frequency — not “unlock four continuous streams.” dens→0 ≈ Foundation alone; dens→1 = full ensemble with space; Accent remains sparse.

### MUTATION semantics

Role-scaled DNA lifespan / pitch adventure. Role identity must survive mut=1. Accent fire-rate caps and Wanderer run/rest rules are hard under all macros.

### RNG

Independent streams `{role}/{purpose}` plus `ensemble/arbitrate`. Foundation may retain legacy Stage 2 tags (`pitch|rhythm|phrase|velocity`) as its streams. Adding Accent decisions must not rewrite Foundation’s autonomous intention sequence; arbitration may still change final MIDI by documented rules.

### Rejected alternatives

- Four independent arpeggiators / shared RNG in voice order
- Per-role MIDI channels (Stage 4)
- Per-role density/mute/level parameters
- Chord progression / Roman-numeral / voice-leading engines
- Full mutual-reaction mesh
- Density = activate all four at 1.0 without budget

### Expected musical result

Small ensemble texture: Foundation spine, Pulse groove, Wanderer questions in gaps, rare upper Accent “what was that?” — coherent on one Ableton instrument.

### GitHub note (actual state at Stage 3 start)

PRs #1–#3 were **already MERGED** into `main` when Stage 3 began. Draft PR #4 bases on `pr/broken-conductor-stage2b` so the review diff stays Stage-3-only versus the accepted Stage 2 tip (content also on `main` via merges). Tag `broken-conductor-stage2-complete` remains the Ableton-accepted milestone (parallel history SHA vs merge tip).


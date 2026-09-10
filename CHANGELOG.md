# Changelog

## Unreleased

### Memory Eater — Stage 4 COMPLETE (PARKED)

- Creative-director Ableton acceptance **PASS** (performance intervention)
- FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE; algorithm **v4** + performance-engine **v1**
- **SEND-FIRST** preserved (Return + MIX=1.0)
- **Product decision:** MEMORY EATER v4 IS SUFFICIENTLY COMPLETE TO SHIP AND PARK
- Tag: `memory-eater-stage4-complete`
- PR #17 merged to `main`


### Memory Eater — Stage 3 COMPLETE

- Creative-director Ableton acceptance **PASS** (generational memory)
- Bounded self-resampling via explicit descendant capture; gens 0…3; algorithm **v3**
- **SEND-FIRST** preserved; Return-track descendants musically successful
- Tag: `memory-eater-stage3-complete`
- PR #16 merged to `main`

### Memory Eater — Stage 2 COMPLETE

- Creative-director Ableton acceptance **PASS** (memory ecology)
- 6 fixed slots with strength / decay / reinforcement / fatigue; algorithm **v2**
- **SEND-FIRST** preserved (Return + MIX=1.0); insert still supported
- Seek preserves ecology; project reload does not serialize audio
- Tag: `memory-eater-stage2-complete`
- PR #15 merged to `main`

### Memory Eater — Stage 1 COMPLETE

- Creative-director Ableton acceptance **PASS** (short-term audio recall)
- **SEND-FIRST** product finding: canonical use is Ableton Return with MIX=1.0; source tracks feed via Sends
- Bounded ring history + sparse microloop recalls; algorithm **v1**
- Insert still supported; MIX retained (0=dry, 1=memory-only)
- Tag: `memory-eater-stage1-complete`
- PR #14 merged to `main`

### Ruin Engine — Stage 4 COMPLETE (PARKED)

- Creative-director Ableton acceptance **PASS** (performance intervention)
- FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE; algorithm **v4** + performance-engine **v1**
- Product decision: Ruin Engine v4 sufficiently complete to park (no Stage 5 without new direction)
- Tag: `ruin-engine-stage4-complete`
- PR #13 merged to `main`

### Ruin Engine — Stage 3 COMPLETE

- Creative-director Ableton acceptance **PASS** (accumulated processing wear)
- `WearState` spectral / nonlinear / temporal; algorithm **v3**
- Seek preserves wear; stop/bypass pause; silence ≈ no aging; SEED preserves wear
- Tag: `ruin-engine-stage3-complete`
- PR #12 merged to `main`

### Ruin Engine — Stage 2 COMPLETE

- Creative-director Ableton acceptance **PASS** (explicit processing states)
- INTACT / WEATHERED / FRACTURED / RUINED / RECOVERING; AGE vs INSTABILITY remain distinct
- Algorithm **v2**; wet-path fracture windows; constrained transition graph
- Tag: `ruin-engine-stage2-complete`
- PR #11 merged to `main`

### Ruin Engine — Stage 1 COMPLETE

- Creative-director Ableton acceptance **PASS** (safe generative audio vertical slice)
- Real AU/VST3 audio effect: MIX / AGE / INSTABILITY / OUTPUT / SEED
- Signal path: dry ‖ filter → sat → delay → DC → limiter → mix → output
- Algorithm **v1**; pluginval strictness 5 SUCCESS
- Tag: `ruin-engine-stage1-complete`
- PR #10 ready for merge (merge by creative director)

### Broken Conductor — Stage 6 COMPLETE

- Creative-director Ableton acceptance **PASS** (harmonic journey / long-form structure)
- HarmonicField ecology + HarmonicJourney; algorithm **v6**
- HOME departure/return; projection union preserved; Stage 5 commands intact
- Tag: `broken-conductor-stage6-complete`
- PR #9 merged to `main`

### Broken Conductor — Stage 5 COMPLETE

- Creative-director Ableton acceptance **PASS** (FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE)
- `ConductorPerformanceController` performance-engine **v1**; generative algorithm stays **v5**
- Projection union preserved under performance command timelines
- Tag: `broken-conductor-stage5-complete`
- PR #8 ready for review (merge by creative director)

### Broken Conductor — Stage 4 COMPLETE

- Creative-director Ableton acceptance **PASS** (role projection + hunger timescales)
- OUTPUT ROLE: ENSEMBLE / FOUNDATION / PULSE / WANDERER / ACCENT
- Algorithm **v5** role hunger; projection union preserved
- Tag: `broken-conductor-stage4-complete`
- PR #7 merged to `main`

### Broken Conductor — Stage 3 COMPLETE

- Creative-director Ableton acceptance **PASS** (ensemble + DENSITY + MUTATION + MIDI routing)
- Tag: `broken-conductor-stage3-complete`
- PR #5 merged to `main`

### Broken Conductor — Stage 2 COMPLETE

- Creative-director Ableton acceptance **PASS** (Stage 2B controls + routing + audible MIDI)
- Tag: `broken-conductor-stage2-complete`
- PRs #1–#3 merged to `main`

### Broken Conductor — Stage 2B (control response)

- Root cause: host params reached engine, but RhythmDNA occupancy baked at birth; mutation lifespan not shortened live; pitch eval too weak at mut=1
- Fix: density expression gate + rebuild on large Δdensity; lifespan shorten on mutation; wider occupancy; stronger pitch evolution
- Algorithm **v3**; endpoint + live automation tests; metrics in `analysis/stage2b-metrics.txt`
- pluginval passes; creative-director Ableton acceptance **PASS**

### Broken Conductor — Stage 2 (Rhythmic Language)

- `RhythmDNA` / `RhythmEngine`: sixteenth-grid cells with ancestry and bounded mutation
- `ConductorEngine` algorithm **v2**: schedule on absolute sixteenth slots; pitch-eval uses `pitch` RNG
- DENSITY → occupancy/rest/duration/syncopation bands (max occupancy 0.58)
- MUTATION → RhythmDNA lifespan + single-op mutate
- Tests: odd buffers, awkward tempos, syncopation, RNG isolation, rests/holds
- Artifacts: `renders/broken-conductor/stage2-*.{txt,mid}`, metrics
- pluginval passes; superseded by Stage 2B for control acceptance

### Broken Conductor — Stage 1C (Ableton host forensics)

- Root cause: Live loads processor then rejects MIDI-out VST3 without a valid **audio input** bus
- Fix: stereo audio in (ignored) + silent stereo out; bus-layout accepts matching mono/stereo
- `docs/PLUGIN_IDENTITIES.md` + `plugin_identity_tests` (unique codes + `.withInput` regression)
- pluginval 1.0.4 passes; identity/arch/dylib/quarantine ruled out
- **HOST INSTANTIATION** verified in Ableton Live 11.3.43 (creative director)
- **END-TO-END LIVE MIDI ROUTING** verified with Stage 2 Ableton acceptance
- Tag: `broken-conductor-stage1c-ableton-verified`

### Broken Conductor — Stage 1B (Ableton host shell)

- Convert VST3/AU from pure MIDI-effect (`Fx`, zero audio buses) to **Instrument** with **silent stereo** out + MIDI out
- Ableton Live 11 cannot reliably open empty-bus MIDI-effect VST3s; document two-track MIDI From topology
- Drop Broken Conductor Standalone format
- ConductorEngine Stage 1 musical behavior unchanged
- Host instantiation / audible MIDI path require creative-director confirmation in Live

### Broken Conductor — Stage 1

- `ConductorEngine` one-voice deterministic MIDI composer (Foundation) reusing generative primitives
- `PFL Broken Conductor` AU/VST3/Standalone MIDI effect target
- `MidiNoteTracker` + transport panic/seek policy
- `broken_conductor_tests` (determinism, buffers, tempos, pairing, stop/seek, long-run)
- Docs: `docs/BROKEN_CONDUCTOR.md`
- Drone Organism Composer v3 behavior preserved (regression suite green)

### Phase 4 — Performance Instrument (performance-engine v1)

- Performance command layer above Composer (`PerformanceController`)
- FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE with explicit priority and state machine
- Host-automatable toggles + edge-triggered actions; minimal performance buttons in editor
- `performance_tests` (determinism, buffer independence, long scripted stability)
- Renders: `renders/phase4/performance-demo-seed-2002.wav`, gestures A/B
- Docs: `docs/PERFORMANCE.md`
- Composer algorithm remains **v3**; Phase 3 recoverable via `phase3-phrase-dna-baseline` / `drone-organism-phase3-baseline`

### Phase 3 — Phrase DNA (composer v3)

- Phrase DNA model with isolated RNG stream and single-element mutation
- Composer follows DNA with high probability while preserving stillness/free walk
- `phrase_tests`; baseline vs candidate renders; `analysis/comparison.md`
- Phase 2 preserved: branch `phase2-composer-baseline`, tag `drone-organism-phase2-composer`

### Phase 2 — Deterministic composition

- Generative core + host PPQ integration (algorithm v2)

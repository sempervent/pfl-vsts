# Changelog

## Unreleased

### Broken Conductor — Stage 3 (four-voice ensemble) — DRAFT

- Algorithm **v4**: Foundation / Pulse / Wanderer / Accent on one MIDI channel
- Two-phase propose → arbitrate; congestion / gap / call-response; same-pitch collision policy
- Independent per-role RNG streams; MidiNoteTracker role ownership
- Metrics/MIDI under `renders/broken-conductor/stage3/`
- Draft PR; Ableton acceptance **PENDING** (do not tag complete yet)

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

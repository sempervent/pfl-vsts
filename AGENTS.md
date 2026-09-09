# AGENTS.md — PFL Generative Instruments

Guidance for AI agents working in this repository (`sempervent/pfl-vsts`).

## What this project is

**PFL Generative Instruments** is a family of deterministic, generative, playable audio and MIDI plugins for experimental music, live performance, controlled unpredictability, transformation, memory, and interaction.

It is **not** a miscellaneous collection of conventional synths/effects, and it is **not** meant to clone physical pedals or duplicate the existing PFL hardware rig. Prefer software jobs hardware cannot do well:

- deterministic generative composition
- musical memory and evolving structure
- controlled randomness / seeded unpredictability
- stateful behavior and generative processing
- rhythmic mutation and captured-audio memory
- performer interaction and simple performance gestures
- reproducibility via seeds and versioned algorithms

**PFL Drone Organism** is the first implemented plugin and the current reference implementation. The repository remains a multi-plugin suite with a defined roadmap.

### Priority order

1. Finished music
2. Playability (one performer, limited hands/feet)
3. Reliability / DSP safety
4. Reproducibility (seed + timeline + command history)

Aesthetic judgments belong to the human creative director. Agents execute engineering and produce listening materials; they do not declare musical approval.

## Suite roadmap (order matters)

```text
PFL Drone Organism          ← implemented (reference)
        ↓
PFL Broken Conductor        ← Stage 2 complete; Stage 3 not started
        ↓
PFL Ruin Engine             ← planned
        ↓
PFL Memory Eater            ← planned
        ↓
PFL Pulse Colony            ← planned
        ↓
PFL Signal Parasite         ← planned (most ambitious / later)
```

Why this order compounds shared architecture:

| Plugin | Adds |
|--------|------|
| **Drone Organism** | Composition + synthesis + performance control |
| **Broken Conductor** | Composition generalized to reusable MIDI generation |
| **Ruin Engine** | Generative processing of external audio |
| **Memory Eater** | Stateful captured-audio memory / resampling |
| **Pulse Colony** | Generative rhythmic architecture |
| **Signal Parasite** | Analysis → generative-response interaction |

### Plugin summaries (status)

1. **PFL Drone Organism** — Implemented AU/VST3 drone instrument. Deterministic self-composition, Phrase DNA / memory, host-synced evolution, macros (SEED / DENSITY / MUTATION / DRIFT / DIRT / SPACE / OUTPUT), and performance commands (FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE). Functionally complete software milestone; awaiting creative-director listening validation and separate physical controller/studio integration. Do not claim full musical approval or hardware validation unless `PROJECT_STATE.md` / `docs/APPROVED_PATCHES.md` say so.

2. **PFL Broken Conductor** — Stage 4 complete (algorithm v5, Ableton-accepted role projection + hunger). Stage 5 performance intervention waits for PR #7 merge.

3. **PFL Ruin Engine** — Planned generative audio transformation. Incoming audio as material; effects become arrangement events (clean → unstable → narrow → distort → smear → feedback → recover/decay). May reuse dirt/drift/filter/feedback/safety DSP concepts without being “Drone Organism FX without oscillators.” Possible signature: **AGE / DECAY**.

4. **PFL Memory Eater** — Planned generative capture / microloop / resampling. Circular memory with capture → remember → repeat → mutate → decay → forget. Complements samplers / KAOSS Replay rather than duplicating them. Possible signature: **GHOSTS**.

5. **PFL Pulse Colony** — Planned generative rhythmic slicer / gate / panner. Evolving rhythmic organisms (not static slicer presets): Euclidean/probability/polymeter/mutation/phase/stereo motion. Possible signature: **LOST ONE**.

6. **PFL Signal Parasite** — Later planned audio-reactive collaborator. Deterministic real-time analysis (RMS, transients, centroid, activity, etc.) — **not** ML/LLM by default. Interactive MIMIC ↔ CONTRADICT continuum. Prefer leveraging Broken Conductor MIDI infrastructure first.

**Do not implement the next roadmap plugin merely because it is documented.** Wait for an explicit implementation task from the creative director / user.

Physical MIDI/controller mapping for Drone Organism is a **separate studio integration task**. It must not block laptop-only software work on shared architecture or later plugins.

## Authoritative docs (read before changing behavior)

| File | Role |
|------|------|
| `PROJECT_STATE.md` | Current milestone, versions, known defects, next task |
| `docs/PERFORMANCE.md` | FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE |
| `docs/ARCHITECTURE.md` | Signal / control flow (current Drone Organism tree) |
| `docs/DECISIONS.md` | Why we chose what we chose |
| `docs/MUSICAL_MODEL.md` | Scale, composer, transport |
| `docs/PARAMETERS.md` | Public controls |
| `docs/TESTING.md` | Test / render commands |
| `docs/APPROVED_PATCHES.md` | Human-approved listening states |
| `CHANGELOG.md` | What landed |

Do not invent a parallel design doc. Update these when behavior changes.

## Current implemented stack (Drone Organism)

```text
Host / automation / UI
        ↓
PerformanceController   (performance-engine v1)
        ↓
Composer                (algorithm v3 — Phrase DNA)
        ↓
Voices → Dirt → Space → DC → Limiter → out
```

Reusable subsystems already in-tree (do not casually rewrite):

```text
src/generative/   Composer, PhraseDNA, MusicalClock, MusicalMemory,
                  DeterministicRNG, Scale, RandomWalk, MusicalEvent
src/performance/  PerformanceCommand, PerformanceController
src/dsp/          Voice, Drift, DirtBus, FeedbackDelay, Filter,
                  Saturator, Oscillator, ParamSmoother, DCBlocker, SafetyLimiter
src/plugins/DroneOrganism/
```

- **Composer algorithm version** and **performance-engine version** are independent.
- Do **not** bump Composer algorithm version unless autonomous composition output changes.
- Do **not** redesign the Composer when adding performance, controllers, or sibling plugins.
- Do **not** add another autonomous generative subsystem “because one is possible.”
- Do **not** casually rewrite working Drone Organism code while starting later plugins.

## Shared architecture direction (documentation only)

Long-term layout should grow toward reusable domains. **Do not create empty directories or reorganize the tree unless an explicit task requires it.** Extract/generalize shared code only when a **second real plugin** needs it — avoid premature framework extraction.

Conceptual target:

```text
src/
  core/           shared deterministic infrastructure
  generative/     Composer, PhraseDNA, MusicalClock, memory, RNG, future rhythm modules
  performance/    PerformanceCommand / Controller / shared semantics
  analysis/       future input analysis
  dsp/            reusable signal processing
  memory/         future captured-audio / circular-buffer systems
  plugins/
    DroneOrganism/
    BrokenConductor/
    RuinEngine/
    MemoryEater/
    PulseColony/
    SignalParasite/
```

Today only `generative/`, `performance/`, `dsp/`, and `plugins/DroneOrganism/` exist.

## Working agreements for agents

1. This is a **multi-plugin suite**, not a Drone Organism-only repo.
2. Drone Organism is the first completed **reference implementation**.
3. Preserve working / approved Drone Organism behavior before experiments.
4. Prefer shared modules over copy/paste — but extract only when a second use case exists.
5. Shared **deterministic** behavior is a major project principle.
6. All randomness must be explicit, seeded, testable, and isolated into logical RNG streams when practical.
7. Musical behavior over feature count; each plugin needs a **distinct musical job**.
8. Do not build conventional plugin clones when PFL hardware already covers the need.
9. Performance controls must stay simple enough for one human performer.
10. Real-time audio safety is mandatory (DC / numeric / feedback / limiter; no audio-callback filesystem I/O).
11. Build, test, and render before claiming technical or musical success.
12. Follow this roadmap unless the creative director explicitly changes priority.
13. **Broken Conductor Stage 2 is complete**; Stage 3 multi-voice only when explicitly requested.
14. Controller integration ≠ software roadmap blocker.
15. **Do not start implementing roadmap plugins without an explicit task.**

## Build / test / install

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
```

Default build copies plugins to:

```text
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
~/Library/Audio/Plug-Ins/Components/PFL Drone Organism.component
```

After meaningful audio/control changes: **rebuild and reinstall** before claiming the DAW has the new binary. Prefer VST3 in Ableton (adhoc AU often fails `auval`).

A plugin milestone is **not host-complete** until these are distinguished and recorded:

```text
build → artifact validation → scanner acceptance → host instantiation → functional routing
```

Scanner listing ≠ openable device. Instantiation ≠ end-to-end MIDI routing. See Broken Conductor Stage 1B→1C (`docs/BROKEN_CONDUCTOR.md`).

### Ableton Live 11 + MIDI-generator VST3s (proven)

Live 11.3.43 loaded Broken Conductor’s processor then rejected it with **“no valid audio input bus”** when the plug-in produced MIDI but only declared stereo **output**. The proven workaround:

```text
stereo audio input  (ignored musically)
silent stereo output
Instrument|Synth category
NEEDS_MIDI_OUTPUT
```

Do not claim this for every DAW — it is Ableton Live 11 evidence for this suite.

## Hard constraints (current + suite-wide)

- Deterministic given seed + params + host timeline + performance command history + versions.
- Isolated RNG streams (`pitch`, `phrase`, `manualMutation`, `reseed`, etc.) — do not casually cross-consume.
- Buffer-size independence for high-level composition / performance events.
- DSP safety always on. Ugly sound is allowed; NaN/Inf/runaway is not.
- No filesystem I/O from the audio callback.
- Project restore: persistent musical params restore; transient freeze/collapse/silence gestures reset safely.
- No device-specific MIDI maps (FCB1010 CC numbers, etc.) inside plugin core until a dedicated mapping phase.

## Performance controls (Drone Organism — do not conflate)

| Continuous | Action |
|------------|--------|
| SEED, DENSITY, MUTATION, DRIFT, DIRT, SPACE, OUTPUT | FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE |

- `MUTATION` (param) ≠ `MUTATE` (command)
- Priority: `SILENCE > COLLAPSE > FREEZE > MUTATE`
- Triggers (`mutate` / `collapse` / `reseed`): **0→1 edge once**; hold at 1 must not retrigger

Later plugins should reuse this performance philosophy where musically appropriate, not invent incompatible gesture languages without cause.

## Git / recovery

Preserve musically approved baselines with branches/tags before risky work. Known recovery points are listed in `PROJECT_STATE.md`.

Commit in focused slices (architecture → behavior → tests → docs). Do not mix unrelated cleanup into feature commits.

## Out of scope until asked

- Implementing Broken Conductor / Ruin Engine / Memory Eater / Pulse Colony / Signal Parasite
- Hardware controller mappings / studio MIDI integration
- Fancy GUI/skins, cloud/LLM/ML, commercial packaging, installer work
- Premature source-tree reorganization or empty roadmap directories

## When stuck

1. Rebuild + retest from a clean known-good commit.
2. Prefer the smallest change that preserves seed reproducibility.
3. Ask the creative director only for aesthetic / “is this music?” calls — not for routine engineering defaults already covered in `docs/DECISIONS.md`.

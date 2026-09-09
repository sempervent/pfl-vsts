# AGENTS.md — PFL Drone Organism

Guidance for AI agents working in this repository.

## What this project is

**PFL Generative Instruments / Drone Organism** — a generative dirty-drone AU/VST3 (JUCE) for Ableton Live on Apple Silicon.

Priority order:

1. Finished music
2. Playability (one performer, limited hands/feet)
3. Reliability / DSP safety
4. Reproducibility (seed + timeline + command history)

Aesthetic judgments belong to the human creative director. Agents execute engineering and produce listening materials; they do not declare musical approval.

## Authoritative docs (read before changing behavior)

| File | Role |
|------|------|
| `PROJECT_STATE.md` | Current milestone, versions, known defects, next task |
| `docs/PERFORMANCE.md` | FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE |
| `docs/ARCHITECTURE.md` | Signal / control flow |
| `docs/DECISIONS.md` | Why we chose what we chose |
| `docs/MUSICAL_MODEL.md` | Scale, composer, transport |
| `docs/PARAMETERS.md` | Public controls |
| `docs/TESTING.md` | Test / render commands |
| `docs/APPROVED_PATCHES.md` | Human-approved listening states |
| `CHANGELOG.md` | What landed |

Do not invent a parallel design doc. Update these when behavior changes.

## Current stack (Phase 4)

```text
Host / automation / UI
        ↓
PerformanceController   (performance-engine v1)
        ↓
Composer                (algorithm v3 — Phrase DNA)
        ↓
Voices → Dirt → Space → DC → Limiter → out
```

- **Composer algorithm version** and **performance-engine version** are independent.
- Do **not** bump Composer algorithm version unless autonomous composition output changes.
- Do **not** redesign the Composer when adding performance or controller features.
- Do **not** add another autonomous generative subsystem “because one is possible.”

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

## Hard constraints

- Deterministic given seed + params + host timeline + performance command history + versions.
- Isolated RNG streams (`pitch`, `phrase`, `manualMutation`, `reseed`, etc.) — do not casually cross-consume.
- Buffer-size independence for high-level composition / performance events.
- DSP safety always on (DC, numeric, feedback, limiter). Ugly sound is allowed; NaN/Inf/runaway is not.
- No filesystem I/O from the audio callback.
- Project restore: persistent musical params restore; transient freeze/collapse/silence gestures reset safely.
- No device-specific MIDI maps (FCB1010 CC numbers, etc.) inside the plugin core until a dedicated mapping phase.

## Performance controls (do not conflate)

| Continuous | Action |
|------------|--------|
| SEED, DENSITY, MUTATION, DRIFT, DIRT, SPACE, OUTPUT | FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE |

- `MUTATION` (param) ≠ `MUTATE` (command)
- Priority: `SILENCE > COLLAPSE > FREEZE > MUTATE`
- Triggers (`mutate` / `collapse` / `reseed`): **0→1 edge once**; hold at 1 must not retrigger

## Git / recovery

Preserve musically approved baselines with branches/tags before risky work. Known recovery points are listed in `PROJECT_STATE.md`.

Commit in focused slices (architecture → behavior → tests → docs). Do not mix unrelated cleanup into feature commits.

## Out of scope until asked

Hardware controller mappings, Broken Conductor / Ruin Engine, fancy GUI/skins, cloud/LLM/ML, commercial packaging, installer work.

## When stuck

1. Rebuild + retest from a clean known-good commit.
2. Prefer the smallest change that preserves seed reproducibility.
3. Ask the creative director only for aesthetic / “is this music?” calls — not for routine engineering defaults already covered in `docs/DECISIONS.md`.

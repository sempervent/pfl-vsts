# Performance Controls — Drone Organism

Performance-engine version: **1** (`pfl::performance::kPerformanceEngineVersion`)  
Composer algorithm version: **3** (unchanged by this layer)

## Purpose

Turn autonomous composition into a live instrument one performer can steer without destroying reproducibility.

## Architecture

```text
HOST / UI / automation
        ↓
PerformanceController  (commands + state machine)
        ↓
Composer  (locked or free)
        ↓
Voice engine → DSP → safety → out × silenceGain × reseedFade
```

Source: `src/performance/PerformanceCommand.h`, `PerformanceController.h`.

## Commands

| Command | Param / UI | Kind | Meaning |
|---------|------------|------|---------|
| FREEZE_ON / OFF | `freeze` bool | Toggle | Composition freeze |
| MUTATE | `mutate` 0→1 edge | Trigger | One bounded manual DNA mutation |
| COLLAPSE | `collapse` 0→1 edge | Trigger | 8-bar multi-stage ending |
| RESEED | `reseed` 0→1 edge | Trigger | New deterministic seed/epoch |
| SILENCE_ON / OFF | `silence` bool | Toggle | Safe mute + pause composition |

`MUTATION` (continuous) ≠ `MUTATE` (command).

## Priority

```text
SILENCE > COLLAPSE > FREEZE > MUTATE
```

SILENCE always wins for audible output. COLLAPSE overrides FREEZE (stasis released). MUTATE ignored while COLLAPSING or SILENCED.

## State machine

```text
NORMAL ↔ FROZEN
NORMAL → COLLAPSING → COLLAPSED
any (except SILENCED blocks some) → SILENCED → prior (FROZEN/COLLAPSED/NORMAL)
NORMAL/COLLAPSED/… → RESEEDING → NORMAL
SILENCED + RESEED → stays SILENCED under new seed
```

Transient gesture state **does not** restore from project save; musical params (SEED, macros) do. On load/prepare, performance resets to NORMAL.

## FREEZE

- **Composition freeze**, not buffer freeze.
- Locks: pitch decisions, voice enter/exit, phrase/structure evolution.
- Continues: oscillators, drift, filters, delay, feedback, existing envelopes, current voice population.
- Latency: applies on command receipt (next process block); not quantized to phrase length.
- While frozen, host PPQ advances; **missed autonomous decisions are discarded**, not queued.
- MUTATE while frozen: **immediate** one mutation; remain frozen.

## MUTATE

- Mutates **one** Phrase DNA element via isolated `manualMutation` RNG (derived from master seed).
- Conservative change (nudge / invert / zero); preserves organism identity.
- Timing: immediate if FROZEN or COLLAPSED; else queued to **next bar**.
- Same seed + same MUTATE sequence → same DNA mutations.

## COLLAPSE

- Duration: **8 bars** at host tempo (musical time).
- Stages (2 bars each): DESTABILIZE → THIN → DECAY → RESIDUE.
- Boosts drift/dirt/space within 0–1; thins max active voices 3→2→1; locks autonomous composition.
- Ends in **COLLAPSED** residue until RESEED, MUTATE (allowed), or transport restart.
- DSP safety path unchanged (DC, limiter, feedback caps).

## RESEED

- Next seed = deterministic mix of current seed + `reseed` RNG stream; always **0…999999** and written to SEED param.
- Uses `Composer::applySeedAtBar` — new epoch **without** rewinding host timeline to bar 1.
- Crossfade ~1 beat (`reseedFade`) when not silenced.
- Seed always knowable/storable.

## SILENCE

- ~5 ms safety ramp to 0; not bar-quantized.
- Pauses composition (locked); release resumes saved mode (freeze/collapse/normal).
- Highest priority over collapse/freeze/mutate.

## Host automation

- Bools: `freeze`, `silence` — level is state.
- Triggers: `mutate`, `collapse`, `reseed` — **0→1 edge once**; hold at 1 does not retrigger; 1→0 rearms.
- No device-specific MIDI CC maps in-plugin (Phase 5+).

## Determinism

Given seed, parameters, host timeline, command timestamps, composer v3, performance-engine v1:

```text
scripted Run A == Run B
```

at performance-event level; buffer-size independent (64–512 verified in `performance_tests`).

## Event trace

`PerformanceController::setTraceEnabled` records non-RT event lists (tests / offline render). Not written from the audio callback to disk.

## Known limitations

- MUTATE only touches Phrase DNA (not voice pitch register directly) — audible mainly when pitches follow DNA after unfreeze or on later changes.
- Collapse sonic recipe is macro/voice-count based, not a dedicated “ruin” DSP bus.
- Trigger params may sit at 1.0 until host returns them to 0 (no auto-reset) — edge semantics still hold.
- No MIDI Learn / hardware maps yet.

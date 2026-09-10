# PFL Ruin Engine

Deterministic generative audio-transformation effect.

## Status

**Stage 3 IN PROGRESS** — draft PR #12 (`pr/ruin-engine-stage3-aging`).
Waiting for creative-director Ableton acceptance.

Stage 2 COMPLETE — tag `ruin-engine-stage2-complete` · PR #11 merged.
Stage 1 COMPLETE — tag `ruin-engine-stage1-complete` · PR #10 merged.

Broken Conductor software development remains paused (Stage 7 = physical-rig integration).

## Purpose

Treat signal degradation and transformation as compositional events.

## Stage 3 objective

Make Ruin Engine remember how much abuse the **processing system** has experienced over musical time — bounded scars that linger and recover — without becoming Memory Eater (no audio capture / loops / granular recall).

## Processing memory vs audio memory

| WearState (Stage 3) | Memory Eater (future, separate) |
|---------------------|----------------------------------|
| Accumulated processing condition | Captured audio content |
| spectral / nonlinear / temporal wear | Loops, grains, resampling |
| Modulates Stage 2 profiles | Playback of remembered audio |

## WearState

```text
WearState { spectralWear, nonlinearWear, temporalWear } ∈ [0, 1]³
```

Internal only — no public WEAR knob. AGE remains intentional damage pressure; wear is what that pressure has done over time.

### Accumulation / recovery (summary)

- Musical-time integration while playing and not bypassed
- State-specific rates × AGE × soft input activity (silence ≈ no aging)
- INSTABILITY does **not** set wear speed
- MIX=0 continues wear (MIX = blend)
- Seek preserves wear (no fabricated PPQ exposure; backward seek does not reverse)
- SEED change preserves wear
- Persisted in private plugin state; legacy Stage 1/2 → fresh wear

## Processing states (Stage 2, still authoritative)

| State | Meaning |
|-------|---------|
| INTACT | Least-damaged wet identity |
| WEATHERED | Aged but useful processed condition |
| FRACTURED | Continuity breaks (wet-path attenuation windows) |
| RUINED | Severe but bounded transformation |
| RECOVERING | Reassembly from damage |

Wear **modifies** state profiles; it does not replace the state machine.

## Public controls (unchanged)

| Control | Role |
|---------|------|
| MIX | dry ↔ ruined (true parallel) |
| AGE | damage eligibility / severity landscape |
| INSTABILITY | restlessness / rate / depth |
| OUTPUT | final level |
| SEED | deterministic universe (preserves WearState) |

## Algorithm

Version **3** (accumulated processing wear). Version 2 = explicit-state ecology. Version 1 = continuous profile nudges.

## Determinism

- 4-beat PPQ eval grid; absolute rebuild of **structural** state on seek/insert
- WearState is **exposure history**, not reconstructed from absolute PPQ
- Sample-rate noise/micro RNG is **not** reset on seek

## Explicitly out of Stage 3

FREEZE/MUTATE/COLLAPSE/RESEED/SILENCE · audio memory · custom GUI · hardware mapping · Stage 4

## Reference renders

```bash
./build/tests/pfl_ruin_engine_render --stage3
# → renders/ruin-engine/stage3/
```

## Ableton acceptance (Stage 3)

**WAITING FOR CREATIVE-DIRECTOR ACCEPTANCE**

### Listening procedure

**TEST A — Fresh vs aged:** MIX .70 / AGE .20 / INST .35 briefly, then AGE .85 / INST .60 for ~128–256 beats, return AGE .20 — residual wear should remain.

**TEST B — Recovery:** After damage, AGE .10 / INST .20 for 128–256 beats — gradual recovery, not instant erase.

**TEST C — AGE vs history:** Fresh instance at AGE .40 vs previously abused/recovered at AGE .40 — subtle difference, same recognisable intent.

**TEST D — Save/restore:** Damage, save Set, reload — WearState restored.

## Stage 2 acceptance (complete)

**PASS** (2026-09-09) — explicit states musically useful; AGE vs INSTABILITY distinct; no host issues.

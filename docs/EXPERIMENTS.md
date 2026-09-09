# Experiments

## EXP-001 — Phrase DNA (composer v3)

| Field | Value |
|-------|-------|
| Date | 2026-09-08 |
| Hypothesis | Adding Phrase DNA will create audible ancestry between pitch motions without increasing busyness |
| Baseline | Composer algorithm v2 (Phase 2), tag `drone-organism-phase2-composer` / branch `phase2-composer-baseline` |
| Change | Phrase DNA influence + single-element phrase mutation; `Composer::kAlgorithmVersion = 3` |
| Seeds | 2002 (primary), 3003 (contrast), 4242 (fresh, candidate only) |
| Parameters | dens=0.45 mut=0.35 drift=0.35 dirt=0.45 space=0.55 output=0.65 tempo=72 4/4 |
| Baseline renders | `renders/phase3/baseline/baseline-seed-2002.wav`, `baseline-seed-3003.wav` (Phase 2, ~3 min) |
| Candidate renders | `renders/phase3/candidate/candidate-seed-*.wav` (8 min) + `*-180s.wav` matched clips |
| Measurements | see `analysis/comparison.md` |
| Creative-director feedback | *pending* |
| Decision | *pending listening* |

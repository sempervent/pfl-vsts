# Parameters — Drone Organism Phase 2

| ID | Name | Range | Default | Role |
|----|------|-------|---------|------|
| `seed` | Seed | 0…999999 | 1001 | Deterministic universe (bar-boundary apply if changed live) |
| `density` | Density | 0…1 | 0.45 | Voice population 1–3 + activity |
| `mutation` | Mutation | 0…1 | 0.35 | Compositional change rate |
| `drift` | Drift | 0…1 | 0.35 | DSP pitch instability |
| `dirt` | Dirt | 0…1 | 0.45 | Saturation/noise/filter macro |
| `space` | Space | 0…1 | 0.55 | Stereo delay depth |
| `output` | Output | 0…1 skewed | 0.65 | Post-safety gain |

All stored in APVTS (automate + project recall).

DRIFT ≠ MUTATION. DIRT/SPACE never bypass safety.

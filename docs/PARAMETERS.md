# Parameters — Drone Organism

## Continuous

| ID | Name | Range | Default | Role |
|----|------|-------|---------|------|
| `seed` | Seed | 0…999999 | 1001 | Deterministic universe (bar-boundary apply if changed live; also updated by RESEED) |
| `density` | Density | 0…1 | 0.45 | Voice population 1–3 + activity |
| `mutation` | Mutation | 0…1 | 0.35 | Autonomous compositional change rate (**not** the MUTATE command) |
| `drift` | Drift | 0…1 | 0.35 | DSP pitch instability |
| `dirt` | Dirt | 0…1 | 0.45 | Saturation/noise/filter macro |
| `space` | Space | 0…1 | 0.55 | Stereo delay depth |
| `output` | Output | 0…1 skewed | 0.65 | Post-safety gain |

## Performance actions

| ID | Name | Kind | Role |
|----|------|------|------|
| `freeze` | Freeze | Bool toggle | Composition freeze |
| `silence` | Silence | Bool toggle | Safe mute + pause composition |
| `mutate` | Mutate | Float trigger 0→1 | One bounded manual DNA mutation |
| `collapse` | Collapse | Float trigger 0→1 | 8-bar collapse trajectory |
| `reseed` | Reseed | Float trigger 0→1 | Deterministic new seed/epoch |

Triggers fire **once per 0→1 edge**. Holding at 1.0 does not retrigger.

All stored in APVTS (automate + project recall). Transient freeze/silence/collapse **gesture** state resets on project load.

DRIFT ≠ MUTATION. MUTATION ≠ MUTATE. DIRT/SPACE never bypass safety.

See `docs/PERFORMANCE.md`.

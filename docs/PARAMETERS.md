# Parameters — Drone Organism (audio engine v0.1)

Public surface:

| ID         | Name     | Range        | Default | Behavior |
|------------|----------|--------------|---------|----------|
| `drift`    | Drift    | 0…1          | 0.35    | Pitch instability (deterministic) |
| `dirt`     | Dirt     | 0…1          | 0.45    | Macro: pre-gain + sat + noise + filter |
| `space`    | Space    | 0…1          | 0.55    | Stereo feedback delay depth |
| `output`   | Output   | 0…1 (skewed) | 0.65    | Post-safety gain |
| `density`  | Density  | 0…1          | 0.70    | Active voice count 1–3 |
| `seed`     | Seed     | 0…999999     | 1001    | Drift/noise RNG universe |

All are in APVTS → automate + restore via `getStateInformation` / `setStateInformation`.

DIRT and SPACE never bypass the safety stage.

## Macro intent

### DRIFT
0% stable → 25% alive → 50% analog-ish → 75% seasick → 100% unstable but recognizable

### DIRT
0% clean → mid-range clearly dirty (shaped curve) → 100% extreme but bounded

### SPACE
0% dry → 50% atmospheric → 100% deep controlled smear

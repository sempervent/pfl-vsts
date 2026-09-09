# Parameters — Drone Organism 0.1

Public surface (deliberately small):

| ID         | Name     | Range        | Default | Phase wiring |
|------------|----------|--------------|---------|--------------|
| `seed`     | Seed     | 0…999999     | 1001    | Phase 2      |
| `density`  | Density  | 0…1          | 0.45    | Phase 2      |
| `drift`    | Drift    | 0…1          | 0.25    | Phase 1 (wired) |
| `dirt`     | Dirt     | 0…1          | 0.35    | Phase 1 partial / Phase 3 full |
| `space`    | Space    | 0…1          | 0.40    | Phase 3      |
| `mutation` | Mutation | 0…1          | 0.30    | Phase 2      |
| `output`   | Output   | 0…1 (skewed) | 0.70    | Phase 0/1 (wired) |

Phase 1 also uses **density** to gate how many of 4 voices are held (1–4).

## Meanings

- **SEED** — deterministic universe
- **DENSITY** — voice population / event activity
- **DRIFT** — tuning instability (cents-scale)
- **DIRT** — saturation + noise + filter aggression macro
- **SPACE** — delay / depth
- **MUTATION** — compositional change rate
- **OUTPUT** — gain before protected output stage

Future performance controls (not in 0.1 UI): FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE.

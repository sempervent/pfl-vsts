# Musical Model — Drone Organism 0.1

Status: **planned** (implementation begins Phase 2). Phase 0–1 establish sound only.

## Aesthetic target

Strange, dirty, atmospheric, rhythmically aware, slowly evolving drones. Continuity over constant novelty.

## Tonal system (0.1)

D minor pentatonic, semitone offsets from D:

```text
0, 3, 5, 7, 10
```

## Pitch motion

Weighted random walk (tunable):

| Move        | Weight |
|-------------|--------|
| stay        | 20%    |
| −1 degree   | 25%    |
| +1 degree   | 25%    |
| −2 degree   | 10%    |
| +2 degree   | 10%    |
| octave      | 5%     |
| mutation    | 5%     |

Prefer local motion. Rare mutation may leave the scale temporarily.

## Memory

Keep ~8–16 recent events. Discourage immediate trivial repetition without forbidding motifs.

## Timescales (host musical time)

- sub-beat: drift / noise / micro-mod
- beat: articulation
- bar: minor compositional moves
- 4 bars: pitch/voice mutation
- 16+ bars: structural decisions

## Approved / rejected

Recorded here and in `PROJECT_STATE.md` once Phase 4 audition begins. No aesthetic approvals yet.

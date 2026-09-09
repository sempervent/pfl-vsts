# Testing

## Commands

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./scripts/render.sh renders/phase2
```

## Automated

| Test | Covers |
|------|--------|
| `dsp_smoke` | DC / limiter safety |
| `audio_engine_smoke` | Hostile DSP finite; buffers/SR |
| `composer_tests` | Scale; determinism; seed difference; buffer independence; tempo; timbre isolation; density/mutation; long run; seek |
| `phrase_tests` | Algorithm v3; DNA determinism; mutation lineage; phrase/timbre isolation; buffer independence |

## Phase 2 renders (72 BPM, 4/4)

Shared macros unless noted: DRIFT 0.35, DIRT 0.45, SPACE 0.55, OUTPUT 0.65

| File | SEED | DENSITY | MUTATION | Length |
|------|------|---------|----------|--------|
| `renders/phase2/seed-1001.wav` | 1001 | 0.45 | 0.35 | 180s |
| `renders/phase2/seed-2002.wav` | 2002 | 0.45 | 0.35 | 180s |
| `renders/phase2/seed-3003.wav` | 3003 | 0.45 | 0.35 | 180s |
| `seed-2002-density-{20,50,80}.wav` | 2002 | varies | 0.35 | 120s |
| `seed-2002-mutation-{10,40,80}.wav` | 2002 | 0.45 | varies | 120s |

## Ableton

1. Rescan plug-ins (prefer VST3)
2. Load **PFL Drone Organism** on MIDI track (no notes required)
3. Set tempo 72 → Play — organism evolves on bar grid
4. Stop — releases; Play from start — same SEED should recreate composition
5. Confirm SEED / DENSITY / MUTATION / DRIFT / DIRT / SPACE / OUTPUT

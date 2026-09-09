# Testing

## Commands

```bash
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./scripts/render.sh 4 renders/phase1-drone.wav
```

## Current automated tests

| Test / tool            | Covers                                      |
|------------------------|---------------------------------------------|
| `dsp_smoke`            | DC blocker settles; limiter bounds + NaN    |
| `pfl_offline_render`   | Voice stack produces finite non-silent WAV  |

## Required suite (charter) — status

| Requirement                 | Status        |
|-----------------------------|---------------|
| Determinism (seed/events)   | Phase 2       |
| RNG stream independence     | Phase 2       |
| Scale integrity             | Phase 2       |
| Buffer-size independence    | Phase 2       |
| Numeric safety              | Partial (0)   |
| Feedback safety             | Phase 3       |
| State restoration           | Manual / later automated |
| Transport start/stop        | Phase 2       |
| Offline WAV renders         | Phase 4       |

## Plugin validation

When AU/VST3 artifacts exist:

```bash
# AU (if available on system)
auval -v aumu Dro1 PflG

# Report paths
ls ~/Library/Audio/Plug-Ins/Components/
ls ~/Library/Audio/Plug-Ins/VST3/
```

Distinguish milestones: compiled ≠ validated ≠ loaded in Ableton ≠ produces musical audio.

# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments / Drone Organism.

## Current milestone

**Phase 4 — Performance Instrument** (performance-engine v1). Composer algorithm remains v3 (Phrase DNA).

## Last known-good commit

Phase 4: `ef9e4ee` / tag `drone-organism-phase4-performance`  
Phase 3: `5419d83` / tag `drone-organism-phase3-phrase-dna` / branch `phase3-phrase-dna-baseline`  
Phase 2: `aea6cdd` / tag `drone-organism-phase2-composer`  
Audio engine: tag `drone-organism-audio-v0.1`

## Plugin version

CMake project `0.1.0` (plugin binary); performance-engine **1**

## Composer algorithm version

`3`

## Performance-engine version

`1`

## Build command

```bash
./scripts/configure.sh
./scripts/build.sh
```

## Test command

```bash
./scripts/test.sh
# performance render:
./build/tests/pfl_performance_render demo renders/phase4
./build/tests/pfl_performance_render gestures renders/phase4
```

## Plugin artifacts

```text
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
~/Library/Audio/Plug-Ins/Components/PFL Drone Organism.component
build/.../DroneOrganism_artefacts/Release/{VST3,AU,Standalone}/
```

## Performance render

```text
renders/phase4/performance-demo-seed-2002.wav   # ~7 min scripted
renders/phase4/performance-demo-events.txt
renders/phase4/performance-gestures-45s.wav
```

## Approved seeds

None formally approved by creative director. Representative: `2002`.

## Current controls

Continuous: SEED, DENSITY, MUTATION, DRIFT, DIRT, SPACE, OUTPUT  
Actions: FREEZE, MUTATE, COLLAPSE, RESEED, SILENCE

## FREEZE behavior

Composition lock; DSP stays alive; missed evolution discarded; MUTATE allowed while frozen.

## MUTATE behavior

One Phrase DNA element via `manualMutation` RNG; immediate if frozen/collapsed else next bar.

## COLLAPSE behavior

8-bar DESTABILIZE→THIN→DECAY→RESIDUE; ends COLLAPSED; overrides FREEZE.

## RESEED behavior

Deterministic next seed → SEED param; applySeedAtBar epoch; ~1 beat fade.

## SILENCE behavior

~5 ms ramp; highest priority; pauses composition; resume prior mode.

## Command priority

SILENCE > COLLAPSE > FREEZE > MUTATE

## Verified buffer sizes

64, 128, 256, 512 (`performance_tests` scripted determinism)

## Verified sample rates

48 kHz primary (tests/renders); engine also prepared at host rate.

## Performance determinism status

Scripted event traces identical across buffer sizes; freeze ON/OFF + mutate isolation tests green.

## Known technical defects

- Prior auval/Ableton notes unchanged
- Trigger float params do not auto-return to 0 after edge (host/UI must release)
- MUTATE audibility depends on later phrase-follow pitch motion

## Known musical weaknesses

- Manual mutate is DNA-only (subtle until pitches move)
- Collapse is structural/macro, not a dedicated failure FX bus
- No creative-director approved performance patches yet

## Next highest-value task

Map FREEZE/MUTATE/COLLAPSE/RESEED/SILENCE + continuous macros to physical controllers (FCB1010 / Launch Control / etc.) **after** listening validation of this layer.

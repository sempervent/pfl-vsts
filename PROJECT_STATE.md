# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments / Drone Organism.

## Current milestone

**Phase 3 — Phrase DNA** (composer algorithm v3). Phase 2 recoverable.

## Last known-good commit

`5419d83` — Phrase DNA integration  
Phase 2: `aea6cdd` / tag `drone-organism-phase2-composer` / branch `phase2-composer-baseline`  
Audio engine: tag `drone-organism-audio-v0.1`  
Tag: `drone-organism-phase3-phrase-dna`

## Build command

```bash
./scripts/configure.sh
./scripts/build.sh
```

## Test command

```bash
./scripts/test.sh
./scripts/render.sh phase3 renders/phase3/candidate
```

## Plugin artifacts

```text
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
AU + Standalone under build/.../DroneOrganism_artefacts/Release/
```

## Render artifacts

```text
renders/phase3/baseline/baseline-seed-2002.wav
renders/phase3/baseline/baseline-seed-3003.wav
renders/phase3/candidate/candidate-seed-2002.wav          # 8 min
renders/phase3/candidate/candidate-seed-3003.wav
renders/phase3/candidate/candidate-seed-4242.wav
renders/phase3/candidate/candidate-seed-*-180s.wav        # matched A/B length
analysis/comparison.md
```

## Current Composer behavior

v3: Phase 2 walk + Phrase DNA influence/mutation. Density/mutation/seed/transport unchanged in role.

## Current default seed

`1001`

## Current scale

D minor pentatonic

## Current random-walk model

Unchanged base weights; often overridden by phrase step when following DNA

## Current memory model

12-event penalties (unchanged)

## Phrase DNA

3–7 degree deltas; follow bias; single-element mutation every 8–32 bars; isolated `phrase` stream

## DENSITY / MUTATION / Transport / Seeking

As Phase 2; MUTATION also shortens phrase lifespan and lowers follow bias slightly

## Composer algorithm version

`3`

## Verified determinism / buffers / sample rates

composer_tests + phrase_tests green (incl. phrase lineage + isolation + buffer independence)

## Known defects

- Prior auval/Ableton notes unchanged
- Occasional no-op DNA mutations (e.g. 0→0) still advance generation
- Seed output **differs** from v2 by design (versioned)

## Known musical weaknesses

- Phrase follow may still be subtle at dens=0.45 (few pitch events to reveal DNA)
- No approved patches yet

## Approved patches/seeds

None

## Questions for creative director

Phrase DNA listening set (see session report)

## Next highest-value engineering step

Await Phrase DNA evaluation before Weather / Weirdness / FREEZE

# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments / Drone Organism.

## Current milestone

**Phase 2 — Deterministic Generative Composition** (audio engine v0.1 preserved)

## Last known-good commit

*(set on commit)* Phase 2 composer integration  
Audio-engine recovery: tag `drone-organism-audio-v0.1` / branch `audio-engine-v0.1`

## Build command

```bash
./scripts/configure.sh
./scripts/build.sh
```

## Test command

```bash
./scripts/test.sh
./scripts/render.sh renders/phase2
```

## Plugin artifacts

```text
build/.../Release/AU/PFL Drone Organism.component
build/.../Release/VST3/PFL Drone Organism.vst3
build/.../Release/Standalone/PFL Drone Organism.app
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
```

## Render artifacts

```text
renders/phase2/seed-1001.wav
renders/phase2/seed-2002.wav
renders/phase2/seed-3003.wav
renders/phase2/seed-2002-density-{20,50,80}.wav
renders/phase2/seed-2002-mutation-{10,40,80}.wav
```

## Current Composer behavior

Host-PPQ bar-grid composer: D minor pentatonic weighted walk + memory + voice identities; density gates 1–3 voices; mutation scales change rate/chromatic chance. No weather/tension/weirdness.

## Current default seed

`1001`

## Current scale

D minor pentatonic (`0,3,5,7,10`)

## Current random-walk model

stay 20 / ±1 25 / ±2 10 / octave 5 / mutation 5 — reshaped by MUTATION; gravity on root/fifth/m3

## Current memory model

12-event MIDI history with recency penalties

## DENSITY behavior

Target active voices 1–3; mild effect on eval period / change probability; enter/exit on bar boundaries

## MUTATION behavior

Compositional evolution rate (not DSP drift); low = stillness, high = more walks + chromatic

## Transport policy

Stop = no new events + release. Play from start (ppq≤0.25) = full reseed. Continue = PPQ advance.

## Seeking policy

Discontinuous jump → reseed + deterministic fast-forward to seek PPQ (not continuous mid-performance identity)

## Verified determinism

Same seed/params → identical event sequences (composer_tests)

## Verified buffer sizes

64, 128, 256, 512, 1024

## Verified sample rates

44.1k / 48k on DSP smoke; composer is sample-rate agnostic (PPQ/bar based)

## Known defects

- `auval` adhoc AU registration failure
- Ableton load not agent-verified
- Seek path ≠ uninterrupted playback identity (documented)
- Foundation can walk toward very low register (D1)

## Known musical weaknesses

- Default density 0.45 uses 2 voices — texture voice often absent until density raised
- Over 64 bars at defaults, pitch changes are sparse (by design); may feel static to some listeners
- Chromatic events exist but are minority; may be hard to notice at low mutation

## Approved patches/seeds

None yet — see `docs/APPROVED_PATCHES.md`

## Questions for creative director

See session report (seed identity, pacing, density/mutation feel, stillness, preserve patch)

## Next highest-value engineering step

Await musical evaluation before weather / FREEZE / phrase DNA / MIDI out

# PROJECT_STATE

Authoritative session memory for PFL Generative Instruments / Drone Organism.

## Current milestone

Drone Organism **audio engine v0.1** — playable dirty stereo drone (no generative composer yet)

## Last known-good commit

`395477a` — feat: establish Drone Organism playable audio engine  
Tag: `drone-organism-audio-v0.1`

## Build command

```bash
./scripts/configure.sh
./scripts/build.sh
```

## Test command

```bash
./scripts/test.sh
./scripts/render.sh 90 renders
```

## Artifact paths

```text
build/src/plugins/DroneOrganism/DroneOrganism_artefacts/Release/AU/PFL Drone Organism.component
build/src/plugins/DroneOrganism/DroneOrganism_artefacts/Release/VST3/PFL Drone Organism.vst3
build/src/plugins/DroneOrganism/DroneOrganism_artefacts/Release/Standalone/PFL Drone Organism.app
~/Library/Audio/Plug-Ins/Components/PFL Drone Organism.component
~/Library/Audio/Plug-Ins/VST3/PFL Drone Organism.vst3
```

## Render paths

```text
renders/drone-organism-audio-engine-v0.1.wav   # 90s nominal DRIFT35/DIRT45/SPACE55
renders/clean.wav
renders/nominal.wav
renders/hostile.wav
```

## Working features

- 3 dual-oscillator voices: D2 / A2 / D3, long A/R, stereo pans
- Deterministic DRIFT (seeded RNG streams + smoothed walk + slow LFO)
- DIRT macro bus: pre-gain, tanh sat, noise, resonant LP aggression
- SPACE: stereo feedback delay with crossfeed + saturating feedback
- Transport Option B: drone fades in while host playing; Standalone always on
- Safety: DC → limiter → OUTPUT → hard ceiling (DIRT/SPACE cannot bypass)
- Params: DRIFT, DIRT, SPACE, OUTPUT, DENSITY, SEED (APVTS save/restore)
- Offline renders + `audio_engine_smoke` (finite/hostile/buffer/SR)

## Known issues

- `auval` fails on adhoc AU (no Apple Developer ID). Prefer **VST3** in Ableton.
- Ableton interactive load not verified in-agent session.
- Oscillators still naive (not band-limited).
- `updateVoicesFromParams` runs every audio block (acceptable at 3 voices).

## Musical questions

1. Clean source too bright / dull / about right?
2. DRIFT organic enough, or need more instability?
3. DIRT: too polite / appropriately damaged / too harsh?
4. SPACE: more delay / diffusion / cavern / unstable tape?
5. Fixed D/A/D voicing a useful foundation?

## Next engineering step

After creative audition of renders/plugins: Phase 2 deterministic Composer on host transport (replace fixed pitches with MusicalEvents). Do not add generative features until this audio engine is musically approved or directed.

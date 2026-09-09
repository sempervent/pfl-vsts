# Musical Model — Drone Organism 0.1

## Transport behavior (audio engine v0.1)

**Option B — transport-gated drone**

- In a DAW host: voices gate open and a transport fade rises only while the host playhead reports playing. On Stop, gates release (~2.5s fade) and the texture decays predictably.
- Standalone build: always treated as playing (no musical transport), so the instrument is immediately audible for audition.
- Offline renders force transport playing.

MIDI notes are not required. MIDI input is accepted for host compatibility but ignored musically in this version.

## Current fixed voicing (pre-Composer)

Root = D. Voices:

```text
Voice 1: D2  (MIDI 38)  pan slightly L
Voice 2: A2  (MIDI 45)  near center (unstable fifth)
Voice 3: D3  (MIDI 50)  pan slightly R
```

Pitch assignment is outside the oscillator class so Composer can replace it later.

## Aesthetic target

Strange, dirty, atmospheric, slowly evolving drones. Continuity over constant novelty.

## Planned tonal system (Phase 2 Composer)

D minor pentatonic offsets: `0, 3, 5, 7, 10`

Weighted random walk and musical memory — not active in audio engine v0.1.

## Approved / rejected

None yet — awaiting creative director audition of audio engine v0.1 renders.

# PFL Ruin Engine

Deterministic generative audio-transformation effect.

## Status

**Stage 1 in progress** — safe generative audio vertical slice.

Broken Conductor software development is paused (Stage 7 is physical-rig integration). Ruin Engine is the next active software target.

## Purpose

Treat signal degradation and transformation as compositional events.

Not another distortion pedal, static multi-effect, or random parameter modulator.

## Stage 1 objective

Build a real AU/VST3 stereo audio effect that safely processes incoming audio through a deterministic evolving transformation chain and works in Ableton Live.

## Public controls (Stage 1 hypothesis)

| Control | Role |
|---------|------|
| MIX | dry ↔ ruined |
| AGE | depth of accumulated degradation |
| INSTABILITY | amount/rate of evolving processing |
| OUTPUT | safe final level |
| SEED | deterministic universe (if exposed) |

## Explicitly out of Stage 1

- FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE
- True content AGE / DECAY / memory
- Audio analysis, ML, custom GUI
- Hardware mapping

## Provisional later stages

1. Generative processing states / larger structural arcs  
2. True AGE / DECAY / memory-sensitive degradation  
3. Performance commands  
4. Advanced stereo / destructive feedback ecology  
5. PFL physical rig integration  

Design details land in `docs/DECISIONS.md` after Stage 1 reconnaissance.

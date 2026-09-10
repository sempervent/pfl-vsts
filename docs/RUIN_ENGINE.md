# PFL Ruin Engine

Deterministic generative audio-transformation effect.

## Status

**Stage 1 COMPLETE** — creative-director Ableton acceptance **PASS**.  
Tag: `ruin-engine-stage1-complete` · PR #10 ready for merge.

Broken Conductor software development remains paused (Stage 7 = physical-rig integration).

Next musical direction (not started until PR #10 merges): **Stage 2 — explicit generative processing states**.

## Purpose

Treat signal degradation and transformation as compositional events.

Not another distortion pedal, static multi-effect, or random parameter modulator.

## Stage 1 objective

Real AU/VST3 stereo audio effect that safely processes incoming audio through a deterministic evolving transformation chain and works in Ableton Live.

## Signal path

```text
INPUT
  ├──────── DRY ──────────────────┐
  ▼                               │
FILTER                            │
  ↓                               │
SATURATION (+ gated noise at high AGE)
  ↓                               │
FEEDBACK DELAY / SMEAR (internal wet = 1)
  ↓                               │
DC → SafetyLimiter                │
  ↓                               │
WET ── MIX ◄──────────────────────┘
  ↓
hard clamp × OUTPUT
  ↓
OUTPUT
```

Dry is taken before any ruin processing. DC/limiter apply to the wet path only; the sum is hard-clamped.

## Public controls

| Control | Role |
|---------|------|
| MIX | dry ↔ ruined (true parallel) |
| AGE | depth of degradation (coherent multi-target curve) |
| INSTABILITY | amount/rate of evolving behavior |
| OUTPUT | final level |
| SEED | deterministic universe |

## Deterministic evolution

- Isolated RNG streams: `structure`, `profile`, `instability`, `noise`
- Structural decisions on a **4-beat** PPQ grid (buffer-independent)
- Continuous micro-motion (LFO/walk) around filter cutoff and delay time
- Transport stopped / host bypass → structural evolution pauses (no catch-up burst)
- AGE=0 → wet path identity; INSTABILITY=0 → frozen structural targets

## Formats / buses

- AU + VST3 audio effect (`Fx` / `kAudioUnitType_Effect`)
- Mono↔mono and stereo↔stereo
- PLUGIN_CODE `Rui1`

## Safety

- Feedback capped at 0.72
- Terminal wet-path DC + SafetyLimiter
- Extreme MIX/AGE/INSTABILITY/OUTPUT remain bounded in tests

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

## Reference renders

```bash
./build/tests/pfl_ruin_engine_render
# → renders/ruin-engine/stage1/
```

## Ableton acceptance (Stage 1)

**PASS** (2026-09-09) — creative director confirmed Stage 1 behaves as engineered in Ableton Live.

Accepted capabilities:

- real AU/VST3 audio-effect insert
- MIX 0 ≈ dry
- AGE / INSTABILITY / OUTPUT / SEED
- deterministic evolving wet path with safety bounds

### Stage 1 listening notes → Stage 2 direction

Explicit generative processing states are desired next. Do not redesign the Stage 1 DSP foundation; organize existing processing into recognizable deterministic musical states and longer structural arcs.

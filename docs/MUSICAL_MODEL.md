# Musical Model — Drone Organism Phase 2

## Scale

D minor pentatonic. Semitone offsets from D: `0, 3, 5, 7, 10` → D F G A C.

Root MIDI class = 2 (D). Register roughly D1–D4, clamped per voice identity.

## Transport policy (Option B + Composer)

| Situation | Behavior |
|-----------|----------|
| Host Stop | No new composition events; voices release via envelopes; transport fade out |
| Host Play from ≤ beat 0.25 | Composer `reseed(SEED)` — full deterministic restart |
| Host Play continue | Advance PPQ; decisions on bar downbeats only |
| Timeline seek (jump > 2 beats or backward) | `handleSeek`: reseed + deterministic fast-forward to target PPQ |
| Standalone | Always “playing”; internal PPQ advances at offline tempo (default 72 BPM) |
| SEED change | Queued; applied at next bar boundary (`applySeedAtBar`) |

## Musical clock

Decisions occur on **bar boundaries** (4/4 default). Phrase grouping (4 bars) is available on the clock but not yet a separate decision layer.

DSP drift/dirt/space continue every sample; they are not compositional events.

## Random-walk model

Base weights (before MUTATION shaping):

| Move | Weight |
|------|--------|
| stay | 20% |
| −1 degree | 25% |
| +1 degree | 25% |
| −2 degree | 10% |
| +2 degree | 10% |
| octave | 5% |
| chromatic mutation | 5% |

MUTATION reduces stay, increases octave/chromatic share. Gravity favors root > fifth > minor third > fourth/m7. Soft rejection uses memory penalties.

## Memory model

12-slot recent MIDI history.

| Recency | Penalty multiplier |
|---------|-------------------|
| previous event | ×0.22 |
| 2 ago | ×0.45 |
| <4 ago | ×0.70 |
| older | ×0.88 |

Repetition is discouraged, not forbidden.

## Voice identities

| Voice | Role | Register | Eval period | Change bias |
|-------|------|----------|-------------|-------------|
| 0 | Foundation | oct 1–3, home 2 | 4–8 bars | low |
| 1 | Middle | oct 2–4 | 2–4 bars | medium |
| 2 | Texture | oct 2–4, home 3 | 1–4 bars | higher |

## DENSITY

Maps to target active voice count 1–3 and slightly shortens eval periods / raises change chance. Voice enter/exit at bar boundaries with envelope gates (no hard mute).

## MUTATION

Controls compositional change rate/adventurousness (not DRIFT). Low → conservative stillness; high → more frequent walks and chromatic chances. Still uses memory + scale gravity.

## SEED / RNG

Master SEED derives independent streams via FNV-ish tag hash + splitmix-style mix:

`pitch`, `voice`, `structure`, `rhythm`, `timbre`

Timbre stream is unused by pitch/voice logic (isolation tested).

## Determinism guarantees

Same SEED + density + mutation + tempo timeline + bar grid → same MusicalEvent sequence across buffer sizes 64–1024 and tempos 40–180 BPM (bar-based).

Audio samples may differ microscopically due to DSP; composition must not.

## Seeking limitation

Arbitrary seek does **not** reconstruct prior continuous runtime oscillator phase. Composer reseeds and fast-forwards event state to the seek PPQ. Documented, predictable, not bit-identical to uninterrupted playback mid-piece.

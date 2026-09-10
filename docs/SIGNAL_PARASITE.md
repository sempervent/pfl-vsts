# PFL Signal Parasite — Stage 3

An audio-reactive generative collaborator. It listens to whatever you feed it,
decides for itself when something is worth answering, and answers with its own
generated sound. Stage 3 makes that relationship *performable*.

- Identity: `SignalParasite`, `Sig1`, `com.pfl.signalparasite`, manufacturer `PflG`
- Formats: AU, VST3 (Fx), stereo in / stereo out, no MIDI
- Algorithm version: **3** · performance-engine **v1**

## Status

**Stage 3 COMPLETE — CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS.**
**PARKED.** Tag: `signal-parasite-stage3-complete` (PR #23).
Also: `signal-parasite-stage2-complete` (PR #22), `signal-parasite-stage1-complete` (PR #21).

## CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS (Stage 3)

Confirmed by human Ableton testing: performance intervention works as engineered.
FREEZE / MUTATE / COLLAPSE / RESEED / SILENCE accepted. Relationship model and
SENSITIVITY / HUNGER / MUTATION distinctions remain.

**Product decision:** SIGNAL PARASITE v3 IS SUFFICIENTLY COMPLETE TO SHIP AND PARK.

No Stage 4. No tonal-listening work. Next suite effort is UI / release polish.

## CREATIVE-DIRECTOR ABLETON ACCEPTANCE: PASS (Stage 2)

Relationship model accepted. Editor sizing/footer fixed and accepted.
**SIGNAL PARASITE STAGE 2 RELATIONSHIP MODEL IS ACCEPTED.**

## Continuous controls

| Control | Default | What it does |
|---------|---------|--------------|
| MIX | 0.50 | Wet/dry blend. At 0 you hear only your input; the parasite still listens. |
| SENSITIVITY | 0.50 | **How much of the source it hears.** |
| HUNGER | 0.35 | **How often it answers what it heard.** 0 is silent. |
| MUTATION | 0.25 | **How fast its manner drifts** (autonomous DNA). ≠ command MUTATE. |
| OUTPUT | 0.85 | Final trim. |
| SEED | 2002 | Personality at fixed macros. |

## Performance vocabulary

| Command | Type | Meaning |
|---------|------|---------|
| FREEZE | toggle | Hold this personality **and** this relationship. Still listens and answers. |
| MUTATE | edge | Change **one** answering habit (bounded DNA edit). |
| COLLAPSE | edge | Relationship destabilizes over 24 beats → DORMANT. |
| RESEED | edge | New parasite personality; relationship restarts at LURKING. |
| SILENCE | toggle | Stop speaking; keep listening. Total output mute ≠ MIX=0. |

Priority: **SILENCE > COLLAPSE > FREEZE > MUTATE**.

### FREEZE

Holds ParasiteDNA (no autonomous evolve) and RelationshipState (no transitions).
Analyzer/detector stay live. HUNGER and SENSITIVITY remain live. Continuous
MUTATION does not evolve DNA until unfreeze. Unfreeze: no catch-up storm.

FREEZE ≠ MUTATION=0 (that only freezes DNA).

### MUTATE

One `ParasiteMutOp` via isolated performance RNG. Does not change relationship
or detector. Works while frozen/dormant; ignored mid-collapse or under silence.
Active voice keeps launch params.

### COLLAPSE

24 beats · CLING → FEVER → WITHDRAW → DORMANT (6 each). Not HUNGER fade, not
SILENCE, not `state=WITHDRAWN`. No stimulus → no response in every phase.
Ends in stable DORMANT until RESEED. Overrides FREEZE.

### DORMANT

Listening, no answers. Dry still passes at MIX&lt;1. ≠ SILENCE. No auto-wake.

### RESEED

New SEED + DNA; clear pending; exit DORMANT; → LURKING. Macros unchanged.
Preserves FREEZE latch. Allowed under SILENCE.

### SILENCE

~4 ms mute of entire plugin output. Analyzer continues. No response backlog on
unsilence. Relationship may evolve while silent unless FREEZE is latched.
DNA evolution pauses. MUTATE / new COLLAPSE ignored while silent.

## Architecture

```text
Host / APVTS
    ↓
PerformanceCommand
    ↓
SignalParasitePerformanceController
    ↓
RelationshipModel / ParasiteDNA / ResponseScheduler
    ↓
ONE ParasiteVoice
    ↓
MIX / OUTPUT / silenceGain
```

Idle (no commands): Stage 2 musical behavior is regression-protected.

## Editor

Shared sizing (`PflGenericEditorSizing.h`): 11 automatable params → **420×484**.
Normal desktop: no scrolling required. Footer: `PFL Signal Parasite - Stage 3`.

## Renders

`renders/signal-parasite/stage3/` — performance journey and verb diagnostics.
`renders/signal-parasite/stage2/` — relationship fixtures (accepted).

## Ableton acceptance (Stage 3) — PASS

Performance verbs accepted in Ableton. Insert and Return (MIX=1) both valid.

## Out of scope / parked

Pitch/tonal listening, FFT, audio memory, second voice, MIDI/studio.
**Do not auto-start Stage 4 or tonal work.** Suite UI / release polish is next.

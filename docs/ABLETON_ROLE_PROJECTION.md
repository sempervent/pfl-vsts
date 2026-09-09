# Broken Conductor — Ableton Live 11 multi-instance role projection

Stage 4 routes roles by **duplicating** the plug-in, not by MIDI channel splitting.

Live 11 does not usefully split one instance’s MIDI channels into separate internal destinations. Do not use IAC or helper processes.

## Setup (acceptance)

| Track | Plugin | Output Role | MIDI From → | Target |
|-------|--------|-------------|-------------|--------|
| BC-F SOURCE | PFL Broken Conductor | **FOUNDATION** | — | — |
| BC-F TARGET | (poly instrument) | — | BC-F SOURCE | hear Foundation |
| BC-P SOURCE | PFL Broken Conductor | **PULSE** | — | — |
| BC-P TARGET | instrument | — | BC-P SOURCE | hear Pulse |
| BC-W SOURCE | PFL Broken Conductor | **WANDERER** | — | — |
| BC-W TARGET | instrument | — | BC-W SOURCE | hear Wanderer |
| BC-A SOURCE | PFL Broken Conductor | **ACCENT** | — | — |
| BC-A TARGET | instrument | — | BC-A SOURCE | hear Accent |

Optional fifth SOURCE with **ENSEMBLE** for A/B reconstruction.

## Synchronize musical state (required)

Every SOURCE must share identical:

- **Seed**
- **Density**
- **Mutation**
- transport / tempo

Mismatched Density/Mutation ⇒ **different musical universes** (expected). There is no cross-instance sync inside the plug-in.

### Fan-out DENSITY / MUTATION (Live 11)

Practical options (pick one):

1. **MIDI Map** — map one controller knob to Density on all four SOURCE devices (same for Mutation).
2. **Rack** — group the four SOURCE tracks’ devices into a Rack if your Live workflow allows macro mapping onto multiple device parameters of the same name (verify in your Live 11 build; behavior varies).
3. **Manual** — set all four to Density **0.50** / Mutation **0.35** for the initial acceptance pass.

## OUTPUT ROLE

Configuration / routing selector (persists with the project). Default **ENSEMBLE** = Stage 3 behavior. Prefer not to automate it; changing role flushes sounding projected notes safely.

## Acceptance checklist

1. Role separation — each target hears only its role.
2. Reconstruction — ENSEMBLE recording matches the four projected parts combined.
3. Density 0.20 → 0.80 on **all** sources — coherent busyness, Accent still rare.
4. Mutation 0.10 → 1.00 on **all** sources — roles stay identifiable.
5. Deliberate desync — change Density on one instance only → divergence; restore + restart → lock again.

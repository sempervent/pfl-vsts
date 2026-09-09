# PFL Broken Conductor

Focused design document for the generative MIDI composer.

## Purpose

A deterministic, host-synchronized generative MIDI composer that develops musical ideas using memory, voice identity, rhythm, rests, Phrase DNA, tonal gravity, controlled mutation, and (later) performance intervention.

It is a **MIDI generator / musical source**, not an audio effect and not a processor for Drone Organism.

## Stages

| Stage | Status |
|-------|--------|
| **1** | Engine + deterministic MIDI tests (`broken-conductor-stage1`) |
| **1B** | Ableton Live host acceptance (Instrument shell + silent stereo + MIDI out) |
| **2+** | Not started |

## Architecture

```text
Host tempo / transport / PPQ
        → ConductorEngine (unchanged Stage 1 musical brain)
        → MidiTraceEvent
        → juce::MidiBuffer (MIDI out)
        → silent stereo audio bus (host shell only)
```

```text
src/generative/ConductorEngine.h   # Stage 1 brain — do not rewrite for host fixes
src/plugins/BrokenConductor/       # Host wrapper / buses / category
```

Drone Organism still uses `Composer` v3. Broken Conductor does not call `Composer`.

## Host plug-in configuration (Stage 1B)

| Flag | Value | Rationale |
|------|-------|-----------|
| `IS_SYNTH` | `TRUE` | Ableton loads it as an **Instrument** |
| `IS_MIDI_EFFECT` | `FALSE` | Live does not treat third-party VST3 as native MIDI Effects |
| `NEEDS_MIDI_INPUT` | `TRUE` | Declared; Stage 1 ignores input notes |
| `NEEDS_MIDI_OUTPUT` | `TRUE` | Generated MIDI |
| `VST3_CATEGORIES` | `Instrument Synth` | Live Instrument browser |
| `AU_MAIN_TYPE` | `kAudioUnitType_MusicDevice` | AU instrument (`aumu`) |
| `FORMATS` | `AU VST3` | Standalone omitted (does not prove DAW routing) |
| Audio buses | Stereo **output**, no inputs | Cleared to silence every block |
| Audio synthesis | **None** | Silent outs only |

### Why Stage 1 MIDI-effect failed in Ableton

Stage 1 built a pure MIDI-effect VST3 (`IS_MIDI_EFFECT`, `Fx`, **zero audio buses**). Ableton Live 11 does not reliably instantiate that configuration. Placing Broken Conductor **after** Drone Organism also puts it in the **audio-effect** portion of the track (wrong domain). Both contribute; empty-bus `Fx` explains **“This VST3 plug-in could not be opened.”**

## Ableton Live 11 topology (correct)

Broken Conductor is **not** an Ableton native MIDI Effect. Use **two MIDI tracks**:

```text
TRACK: BC SOURCE
  Device chain Instrument slot:
    PFL Broken Conductor   ← only device (or first as Instrument)
  (Audio outs may be silent / unused)

TRACK: BC TARGET
  In/Out section:
    MIDI From:  BC SOURCE
    (lower chooser if shown): PFL Broken Conductor
    Monitor:    In
  Instrument:
    Operator (or any stock Ableton instrument)
```

### Wrong (do not do this)

```text
Same MIDI track:
  Instrument: PFL Drone Organism
      ↓
  Audio Effects: PFL Broken Conductor   ← WRONG
```

Broken Conductor does not process Drone Organism audio and cannot feed MIDI “backwards” into an instrument already converting MIDI→audio.

## Manual Ableton acceptance test (VST3)

1. Preferences → Plug-Ins → **Rescan**; prefer **VST3**.
2. New Live Set. Create MIDI track **BC SOURCE**.
3. Load **PFL Broken Conductor** as the track **Instrument** (not after another instrument).
4. Confirm the UI opens (no “could not be opened”).
5. Create MIDI track **BC TARGET**. Add **Operator** (or Analog).
6. On **BC TARGET** In/Out: **MIDI From** = **BC SOURCE**; if a second menu lists devices, choose **PFL Broken Conductor**.
7. Set **Monitor** to **In**.
8. Transport **Play** (~72–120 BPM).
9. Confirm Operator receives notes (channel 1).
10. **Stop** — notes should release (no hang).

Creative-director confirmation of steps 3–9 is required to mark host items fully verified in `PROJECT_STATE.md`.

## Current musical behavior (unchanged from Stage 1)

- **One voice:** Foundation  
- **Scale:** D minor pentatonic  
- **Register:** MIDI 26–50, start D2  
- **Grid:** integer beats  
- **Durations:** {1, 2, 4}  
- **Rests / Phrase DNA / velocity 64–96 / channel 1** as Stage 1  

## Transport semantics

| Situation | Behavior |
|-----------|----------|
| Host Stop | Panic note-offs + AllNotesOff |
| Play from ≤ beat 0.25 | `reseed(SEED)` |
| Seek | Panic → silent reconstruct → optional re-articulate |
| SEED change | Panic, then reseed |

Large seeks may hitch (synchronous reconstruct). Abrupt plugin unload may not deliver final MIDI (Live lifecycle).

## Parameters

`seed`, `density`, `mutation` — same Stage 1 meanings.

## Determinism guarantees

Unchanged Stage 1 suite (`broken_conductor_tests`).

## Validation ladder

```text
compile
  ≠ artifact exists
  ≠ scanner lists plugin
  ≠ host instantiates + UI opens
  ≠ MIDI routable to another track
  ≠ target instrument sounds
```

Unit tests alone do **not** claim Ableton acceptance.

## Known limitations

- Stage 1 musical scope (single voice, no performance layer)
- Host acceptance depends on Live rescan + correct two-track routing
- AU MusicDevice may not expose MIDI-out routing as well as VST3 in Live — prefer VST3 for MIDI generation
- No `pluginval` / `vst3validator` installed in the build environment yet

## Future stages

Stage 2+ unchanged (rhythmic language, multi-voice, performance layer, etc.). Do not start without an explicit task.

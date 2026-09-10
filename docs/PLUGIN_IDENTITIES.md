# Plugin Identities

Authoritative unique IDs for PFL plugins. Assign new codes deliberately before adding a plugin.

| Human name | CMake target | PLUGIN_CODE | Manufacturer | Bundle ID | Formats |
|------------|--------------|-------------|--------------|-----------|---------|
| PFL Drone Organism | `DroneOrganism` | `Dro1` | `PflG` | `com.pfl.droneorganism` | AU VST3 Standalone |
| PFL Broken Conductor | `BrokenConductor` | `Brk1` | `PflG` | `com.pfl.brokenconductor` | AU VST3 |
| PFL Ruin Engine | `RuinEngine` | `Rui1` | `PflG` | `com.pfl.ruinengine` | AU VST3 |
| PFL Memory Eater | `MemoryEater` | `Mem1` | `PflG` | `com.pfl.memoryeater` | AU VST3 |
| PFL Pulse Colony | `PulseColony` | `Pls1` | `PflG` | `com.pfl.pulsecolony` | AU VST3 |

## VST3 CIDs (from installed `moduleinfo.json`)

Derived from JUCE manufacturer + plugin code (hex of 4-char codes in the trailing bytes).

| Plugin | Processor CID | Controller CID |
|--------|---------------|----------------|
| Drone Organism | `ABCDEF019182FAEB50666C4744726F31` (`…Dro1`) | `ABCDEF011234ABCD50666C4744726F31` |
| Broken Conductor | `ABCDEF019182FAEB50666C4742726B31` (`…Brk1`) | `ABCDEF011234ABCD50666C4742726B31` |

These must remain unique across the suite.

## Ableton Live 11 note (Broken Conductor)

Live log evidence (2026-09-09):

```text
VST3: plugin processor successfully loaded: PFL Broken Conductor
error: Vst3: plugin has an effect category, but no valid audio input bus
error: VST3: No valid input bus could be found
error: VST3: Failed: PFL Broken Conductor
```

MIDI-out VST3s are rejected by Live without a valid **audio input** bus, even when scanned as `instr`. Stage 1C adds stereo in+out (input ignored; output silent). Host instantiation verified 2026-09-09 (`broken-conductor-stage1c-ableton-verified`).

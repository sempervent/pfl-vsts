# Decisions

## 2026-09-08 — Framework: JUCE 8.0.15 + CMake + Ninja

### Context

Environment audit:

- macOS Apple Silicon, Xcode present, Ableton Live 11 Suite present
- `cmaj` / Cmajor toolchain **not** installed
- CMake and Ninja were missing; installed via Homebrew for the project

### Decision

Use **JUCE 8.0.15** fetched by CMake `FetchContent`, generator **Ninja**, C++20.

### Why not Cmajor first

Preferred progression listed Cmajor first, but the installed toolchain does not make Cmajor practical here (binary/CLI absent). Installing and learning an additional language runtime would delay a loadable AU/VST3. JUCE directly yields AU + VST3 + Standalone with host transport APIs we need.

### Why not JUCE 9.x yet

JUCE 9.0.2 exists. Pinned to **8.0.15** for a well-exercised CMake AudioPlugin example path and fewer unknowns on first bring-up. Revisit 9.x after Phase 1–2 are musically stable.

### Consequences

- Reproducible configure/build via `scripts/`
- Private prototype development is compatible with JUCE under AGPLv3 terms for non-distributed use; public/commercial distribution requires license review (`docs/LICENSING.md`)
- Composition engine stays in portable C++ under `src/generative/` for later MIDI reuse

---

## 2026-09-08 — Phase 3 primary intervention: Phrase DNA

### Problem

Pitch events are sparse (good for drone stillness) but lack recognizable ancestry. When pitches do move, sequences do not establish motifs — local random-walk wandering without lineage.

### Evidence

- No creative-director FEEDBACK.md / approved generative patches yet
- `PROJECT_STATE` known weakness: sparse changes; identity questions unanswered
- Phase 2 traces (64 bars, dens=0.45, mut=0.35): seed 2002 → 6 note changes; seed 3003 → 10; no phrase-level structure
- Pitch paths revisit notes only by chance; MusicalMemory only penalizes, does not propose motifs
- Constant novelty / busy-ness are **not** dominant (opposite: low event rate)
- Increasing raw event probability forbidden as sole fix for “too static”

### Chosen intervention

**Phrase DNA** (composer algorithm version **3**)

### Why

Creates A → A′ → A″ lineage so sparse events can still feel related, without Weather (needs clearer chapters/activity) or Weirdness Budget (targets excess novelty we do not have) or FREEZE/MUTATE (performance layer once organism is already strong).

### Alternatives rejected for now

- Weirdness Budget — novelty is rare, not excessive
- Weather — premature without phrase-level identity; risk of preset-like arcs
- Performance FREEZE/MUTATE — no approved seeds yet; autonomy still needs identity work
- Simply raising mutation/density — activity ≠ music

### Expected musical effect

Recognizable degree-step motifs that evolve by single-element mutation every ~8–32 bars; pitch changes often follow DNA while free walk remains available; stillness preserved.

---

## 2026-09-08 — Phase 4: Performance layer above Composer

### Context

Phase 3 established Phrase DNA (v3). Phase 4 turns the organism into a steerable live instrument without redesigning Composer or adding another autonomous generative subsystem.

### Decisions

1. **PerformanceController** owns FREEZE/MUTATE/COLLAPSE/RESEED/SILENCE; Composer gains only `setCompositionLocked` + `applyManualMutation`.
2. **Composer algorithm version stays 3** — autonomous output unchanged when no performance commands fire. Introduce **performance-engine v1** instead.
3. **FREEZE** = composition lock (not audio freeze); discarded evolution, not queued catch-up.
4. **MUTATE** ≠ MUTATION param; uses isolated `manualMutation` RNG; one DNA element; works while frozen.
5. **COLLAPSE** = 8-bar staged trajectory ending in COLLAPSED residue; overrides FREEZE; MUTATE ignored during COLLAPSING.
6. **RESEED** = deterministic seed derivation + `applySeedAtBar` (new epoch, no bar-1 rewind); seed always written to SEED.
7. **SILENCE** = highest priority; ~5 ms ramp; pauses composition.
8. **Project restore**: persistent macros/seed; transient freeze/collapse/silence reset safely.
9. **No hardware MIDI maps** in this phase — control-ready only.

### Consequences

See `docs/PERFORMANCE.md`. Ready for external controller mapping after listening validation.


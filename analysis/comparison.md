# Phase 3 Comparison — Phrase DNA (composer v3)

## Setup

Identical macros: DENSITY 0.45, MUTATION 0.35, DRIFT 0.35, DIRT 0.45, SPACE 0.55, OUTPUT 0.65, 72 BPM, 4/4.

| Role | Algorithm | Seeds |
|------|-----------|-------|
| Baseline | v2 (Phase 2 random walk only) | 2002, 3003 |
| Candidate | v3 (Phrase DNA) | 2002, 3003, 4242 |

## Objective metrics (64 bars)

| Seed | Algo | noteChanges | enters | phraseMutations | notes |
|------|------|-------------|--------|-----------------|-------|
| 2002 | v2 | 6 | 2 | n/a | Phase 2 trace |
| 2002 | v3 | 4 | 2 | 2 | DNA evolves; slightly fewer free changes |
| 3003 | v2 | 10 | 2 | n/a | Phase 2 trace |
| 3003 | v3 | 7 | 2 | 3 | maxNoteReuse=2 |

## Objective metrics (128 bars, v3 only)

| Seed | noteChanges | uniqueNotes | maxNoteReuse | phraseGens | phraseMutations |
|------|-------------|-------------|--------------|------------|-----------------|
| 2002 | 14 | 9 | 2 | 7 | 6 |
| 3003 | 19 | 14 | 2 | 7 | 6 |

### Example lineage (seed 2002)

```text
bar 0  born   DNA:-1,0,1,-2,-2,0
bar 20 mutate idx5 0→-1
bar 45 mutate idx0 -1→0
...
bar 124 mutate idx5 -2→0  → DNA:0,1,1,-2,-2,0
```

Mutations change **one** step at a time (ancestry preserved).

## Interpretation (technical, not aesthetic)

- Event rate did **not** increase; v3 is slightly quieter in noteChanges at 64 bars.
- Phrase engine produces multi-generation DNA with bounded single-index edits.
- This matches the intent: identity via lineage, not via more random events.

Aesthetic success is for the creative director to judge from the renders.

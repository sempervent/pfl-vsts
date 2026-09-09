# Changelog

## Unreleased

### Phase 1

- Four dual-oscillator drone voices with long envelopes, filter, light saturation
- DRIFT and DENSITY (voice gating) wired; DIRT pre-shapes filter/sat
- Offline WAV render script (`scripts/render.sh`)

### Phase 0

- JUCE 8.0.15 + CMake/Ninja project scaffold
- Drone Organism AU / VST3 / Standalone shell
- Public parameter stubs (seed, density, drift, dirt, space, mutation, output)
- DC blocker + safety limiter on output path
- Quiet proof tone (replaced in Phase 1)
- DSP smoke tests

#pragma once

#include "DeterministicRNG.h"
#include "MusicalMemory.h"
#include "Scale.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pfl::generative
{

struct WalkWeights
{
    float stay = 0.20f;
    float down1 = 0.25f;
    float up1 = 0.25f;
    float down2 = 0.10f;
    float up2 = 0.10f;
    float octave = 0.05f;
    float mutation = 0.05f;
};

struct PitchState
{
    int degree = 0;
    int octave = 2; // D2 region default
    int midiNote = 38;
    bool chromatic = false;
};

/** Weighted random walk on D minor pentatonic with gravity + memory. */
class RandomWalk
{
public:
    void setWeights (const WalkWeights& w) noexcept { weights_ = w; }

    PitchState step (const PitchState& current,
                     DeterministicRNG& rng,
                     const MusicalMemory& memory,
                     float mutationAmount01,
                     float chromaticBoost = 1.0f) const noexcept
    {
        WalkWeights w = weights_;
        // MUTATION increases octave/mutation share, reduces stay
        const float m = std::clamp (mutationAmount01, 0.0f, 1.0f);
        w.stay *= (1.0f - 0.55f * m);
        w.mutation *= (0.35f + 1.8f * m) * chromaticBoost;
        w.octave *= (0.6f + 1.2f * m);
        normalize (w);

        const float r = rng.nextFloat();
        float acc = 0.0f;

        auto pick = [&] (float weight, auto fn) -> bool {
            acc += weight;
            if (r <= acc)
            {
                fn();
                return true;
            }
            return false;
        };

        PitchState next = current;
        next.chromatic = false;

        bool chosen = false;
        chosen = pick (w.stay, [&] { /* stay */ });
        if (! chosen)
            chosen = pick (w.down1, [&] { next.degree -= 1; });
        if (! chosen)
            chosen = pick (w.up1, [&] { next.degree += 1; });
        if (! chosen)
            chosen = pick (w.down2, [&] { next.degree -= 2; });
        if (! chosen)
            chosen = pick (w.up2, [&] { next.degree += 2; });
        if (! chosen)
            chosen = pick (w.octave, [&] {
                next.octave += (rng.nextFloat() < 0.5f ? -1 : 1);
            });
        if (! chosen)
        {
            // Chromatic mutation: ±1 semitone from current MIDI, then clamp register
            next.chromatic = true;
            const int delta = rng.nextFloat() < 0.5f ? -1 : 1;
            next.midiNote = current.midiNote + delta;
            Scale::fromMidi (next.midiNote, next.degree, next.octave);
            // Keep chromatic midi; degree is nearest for future walks
            next.midiNote = std::clamp (next.midiNote, 26, 74); // ~D1..D5
            applyMemoryAndGravity (next, memory, rng, true);
            return next;
        }

        // Normalize degree/octave
        while (next.degree < 0)
        {
            next.degree += Scale::kNumDegrees;
            --next.octave;
        }
        while (next.degree >= Scale::kNumDegrees)
        {
            next.degree -= Scale::kNumDegrees;
            ++next.octave;
        }

        next.octave = std::clamp (next.octave, 1, 4);
        next.midiNote = Scale::toMidi (next.degree, next.octave);
        applyMemoryAndGravity (next, memory, rng, false);
        return next;
    }

private:
    static void normalize (WalkWeights& w) noexcept
    {
        float s = w.stay + w.down1 + w.up1 + w.down2 + w.up2 + w.octave + w.mutation;
        if (s <= 1.0e-6f)
            return;
        w.stay /= s;
        w.down1 /= s;
        w.up1 /= s;
        w.down2 /= s;
        w.up2 /= s;
        w.octave /= s;
        w.mutation /= s;
    }

    static void applyMemoryAndGravity (PitchState& next,
                                       const MusicalMemory& memory,
                                       DeterministicRNG& rng,
                                       bool chromatic) noexcept
    {
        // Soft rejection sampling: try a few alternates if heavily penalized
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const float pen = memory.penaltyMultiplier (next.midiNote);
            const float grav = chromatic ? 1.0f : Scale::gravityWeight (next.degree);
            const float accept = std::clamp (pen * grav, 0.05f, 1.5f);
            if (rng.nextFloat() < accept / 1.5f)
                break;

            // Nudge toward root/fifth in same octave
            if (! chromatic)
            {
                const int prefer = (rng.nextFloat() < 0.55f) ? 0 : 3;
                next.degree = prefer;
                next.midiNote = Scale::toMidi (next.degree, next.octave);
            }
            else
            {
                next.midiNote = Scale::toMidi (0, next.octave); // resolve toward root
                next.chromatic = false;
                Scale::fromMidi (next.midiNote, next.degree, next.octave);
            }
        }
    }

    WalkWeights weights_{};
};

} // namespace pfl::generative

#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace pfl::generative
{

/** D minor pentatonic (root D = MIDI class 2). Offsets: 0,3,5,7,10. */
class Scale
{
public:
    static constexpr int kNumDegrees = 5;
    static constexpr std::array<int, kNumDegrees> kOffsets { 0, 3, 5, 7, 10 };
    static constexpr int kRootMidiClass = 2; // D

    /** MIDI note for degree + octave. Degree may be outside 0..4 (wraps). */
    static int toMidi (int degree, int octave) noexcept
    {
        // octave: MIDI octave number where C4 = 60 → octave 4
        while (degree < 0)
        {
            degree += kNumDegrees;
            --octave;
        }
        while (degree >= kNumDegrees)
        {
            degree -= kNumDegrees;
            ++octave;
        }
        return (octave + 1) * 12 + kRootMidiClass + kOffsets[static_cast<size_t> (degree)];
    }

    static int degreeOfOffset (int semitoneOffset) noexcept
    {
        const int o = ((semitoneOffset % 12) + 12) % 12;
        for (int i = 0; i < kNumDegrees; ++i)
            if (kOffsets[static_cast<size_t> (i)] == o)
                return i;
        return -1; // chromatic / not in scale
    }

    /** Nearest scale degree in same octave neighborhood. */
    static void fromMidi (int midiNote, int& degreeOut, int& octaveOut) noexcept
    {
        // MIDI: note = (oct+1)*12 + pc
        int pc = ((midiNote % 12) + 12) % 12;
        octaveOut = (midiNote / 12) - 1;
        int bestDeg = 0;
        int bestDist = 99;
        for (int i = 0; i < kNumDegrees; ++i)
        {
            const int d = std::abs (pc - kOffsets[static_cast<size_t> (i)]);
            const int wrap = 12 - d;
            const int dist = d < wrap ? d : wrap;
            if (dist < bestDist)
            {
                bestDist = dist;
                bestDeg = i;
            }
        }
        degreeOut = bestDeg;
    }

    /** Gravity weight by degree index: root, m3, 4, 5, m7 → 0,3,5,7,10 */
    static float gravityWeight (int degree) noexcept
    {
        degree = ((degree % kNumDegrees) + kNumDegrees) % kNumDegrees;
        switch (degree)
        {
            case 0: return 1.35f; // root
            case 1: return 1.05f; // minor third
            case 2: return 0.75f; // fourth
            case 3: return 1.15f; // fifth
            case 4: return 0.70f; // minor seventh
            default: return 1.0f;
        }
    }
};

} // namespace pfl::generative

#pragma once

#include "Filter.h"
#include "ParamSmoother.h"
#include "Saturator.h"

#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pfl::dsp
{

/**
 * DIRT macro bus: shaped pre-gain, saturation, noise, and filter aggression.
 * Parameter curve is nonlinear so mid-range is audibly distinct from extremes.
 */
class DirtBus
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        filterL_.prepare (sampleRate);
        filterR_.prepare (sampleRate);
        noiseRng_.reseed (0xD12Au);
        dirtSmooth_.prepare (sampleRate, 0.04f);
        dirtSmooth_.setCurrentAndTarget (0.0f);
        reset();
    }

    void reset() noexcept
    {
        filterL_.reset();
        filterR_.reset();
    }

    void setDirt (float dirt01) noexcept
    {
        dirtSmooth_.setTarget (std::clamp (dirt01, 0.0f, 1.0f));
    }

    void setNoiseSeed (uint64_t seed) noexcept
    {
        noiseRng_ = pfl::generative::DeterministicRNG::derived (seed, 0x4E4F4953ull); // "NOIS"
    }

    void processSample (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float d = dirtSmooth_.getNext();
        const float shaped = d * d * (3.0f - 2.0f * d); // smoothstep
        const float midBoost = std::pow (d, 0.65f);

        const float preGain = 0.85f + 2.4f * midBoost;
        const float drive = 0.08f + 0.92f * shaped;
        saturator_.setDrive (drive);

        const float cutoff = 4200.0f * std::pow (0.18f, shaped) + 180.0f;
        const float res = 0.05f + 0.55f * shaped;
        filterL_.setCutoffHz (cutoff);
        filterR_.setCutoffHz (cutoff);
        filterL_.setResonance (res);
        filterR_.setResonance (res);

        float L = saturator_.processSample (inL * preGain);
        float R = saturator_.processSample (inR * preGain);

        const float noiseAmt = 0.002f + 0.09f * std::pow (std::max (0.0f, d - 0.15f) / 0.85f, 1.4f);
        L += (noiseRng_.nextFloat() * 2.0f - 1.0f) * noiseAmt;
        R += (noiseRng_.nextFloat() * 2.0f - 1.0f) * noiseAmt;

        outL = filterL_.processSample (L) * (0.75f / (0.75f + 0.35f * midBoost));
        outR = filterR_.processSample (R) * (0.75f / (0.75f + 0.35f * midBoost));
    }

private:
    double sampleRate_ = 44100.0;
    Filter filterL_;
    Filter filterR_;
    Saturator saturator_;
    pfl::generative::DeterministicRNG noiseRng_;
    ParamSmoother dirtSmooth_;
};

} // namespace pfl::dsp

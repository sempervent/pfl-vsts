#pragma once

#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pfl::dsp
{

/**
 * Slow pitch instability in cents.
 * Uses deterministic RNG for random-walk targets; timing is sample-rate based (seconds).
 */
class Drift
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        setSmoothingTimeMs (120.0f);
        reset();
    }

    void reset() noexcept
    {
        currentCents_ = 0.0f;
        targetCents_ = 0.0f;
        lfoPhase_ = 0.0;
        samplesUntilRetarget_ = 0;
    }

    void setAmount (float amount01) noexcept
    {
        amount_ = std::clamp (amount01, 0.0f, 1.0f);
    }

    void setVoiceOffsetCents (float cents) noexcept
    {
        voiceOffsetCents_ = cents;
    }

    void setRng (pfl::generative::DeterministicRNG rng) noexcept
    {
        rng_ = rng;
    }

    /** Advance one sample; returns multiplicative frequency ratio. */
    float processRatio() noexcept
    {
        // Very slow LFO (~0.02–0.18 Hz)
        const double lfoHz = 0.02 + 0.16 * static_cast<double> (amount_);
        lfoPhase_ += lfoHz / sampleRate_;
        if (lfoPhase_ >= 1.0)
            lfoPhase_ -= 1.0;
        const float lfo = static_cast<float> (std::sin (lfoPhase_ * 6.283185307179586));

        if (--samplesUntilRetarget_ <= 0)
        {
            // Retarget every ~1.5–6 seconds depending on amount (rate in Hz of updates)
            const float intervalSec = 6.0f - 4.5f * amount_;
            samplesUntilRetarget_ = std::max (1, static_cast<int> (intervalSec * sampleRate_));

            const float maxJump = 2.0f + 48.0f * amount_; // cents
            walkTarget_ = rng_.nextFloat (-maxJump, maxJump);
        }

        // Smooth walk toward random target
        walk_ += (walkTarget_ - walk_) * (0.000015f + 0.00012f * amount_);

        const float lfoDepth = 2.0f + 30.0f * amount_;
        targetCents_ = voiceOffsetCents_ + walk_ + lfo * lfoDepth;
        // Absolute bound so pitch structure remains recognizable
        const float absMax = 8.0f + 70.0f * amount_;
        targetCents_ = std::clamp (targetCents_, -absMax, absMax);

        currentCents_ += (targetCents_ - currentCents_) * smoothCoeff_;
        return std::pow (2.0f, currentCents_ / 1200.0f);
    }

    float currentCents() const noexcept { return currentCents_; }

private:
    void setSmoothingTimeMs (float ms) noexcept
    {
        const float samples = 0.001f * ms * static_cast<float> (sampleRate_);
        smoothCoeff_ = samples > 0.0f ? 1.0f - std::exp (-1.0f / samples) : 1.0f;
    }

    double sampleRate_ = 44100.0;
    double lfoPhase_ = 0.0;
    float amount_ = 0.25f;
    float voiceOffsetCents_ = 0.0f;
    float walk_ = 0.0f;
    float walkTarget_ = 0.0f;
    float targetCents_ = 0.0f;
    float currentCents_ = 0.0f;
    float smoothCoeff_ = 0.01f;
    int samplesUntilRetarget_ = 0;
    pfl::generative::DeterministicRNG rng_;
};

} // namespace pfl::dsp

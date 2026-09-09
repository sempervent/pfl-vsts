#pragma once

#include <algorithm>
#include <cmath>

namespace pfl::dsp
{

/** Slow pitch instability in cents, smoothed to avoid clicks. */
class Drift
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        setSmoothingTimeMs (80.0f);
        currentCents_ = 0.0f;
        targetCents_ = 0.0f;
        walk_ = 0.0f;
        phase_ = 0.0;
    }

    void reset() noexcept
    {
        currentCents_ = 0.0f;
        targetCents_ = 0.0f;
        walk_ = 0.0f;
        phase_ = 0.0;
    }

    void setAmount (float amount01) noexcept
    {
        amount_ = std::clamp (amount01, 0.0f, 1.0f);
    }

    void setVoiceOffsetCents (float cents) noexcept
    {
        voiceOffsetCents_ = cents;
    }

    /** Advance one sample; returns multiplicative frequency ratio. */
    float processRatio() noexcept
    {
        // Very slow LFO (~0.05–0.2 Hz scaled by amount)
        const double lfoHz = 0.03 + 0.12 * static_cast<double> (amount_);
        phase_ += lfoHz / sampleRate_;
        if (phase_ >= 1.0)
            phase_ -= 1.0;

        const float lfo = static_cast<float> (std::sin (phase_ * 6.283185307179586));

        // Soft random walk toward new target infrequently via continuous leaky integration of LFO noise proxy
        const float maxWalk = 8.0f + 42.0f * amount_; // cents
        walk_ += (lfo * 0.15f - walk_) * (0.00002f + 0.00015f * amount_);
        walk_ = std::clamp (walk_, -maxWalk, maxWalk);

        targetCents_ = voiceOffsetCents_ + walk_ + lfo * (3.0f + 25.0f * amount_);
        currentCents_ += (targetCents_ - currentCents_) * smoothCoeff_;

        return std::pow (2.0f, currentCents_ / 1200.0f);
    }

private:
    void setSmoothingTimeMs (float ms) noexcept
    {
        const float samples = 0.001f * ms * static_cast<float> (sampleRate_);
        smoothCoeff_ = samples > 0.0f ? 1.0f - std::exp (-1.0f / samples) : 1.0f;
    }

    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    float amount_ = 0.25f;
    float voiceOffsetCents_ = 0.0f;
    float walk_ = 0.0f;
    float targetCents_ = 0.0f;
    float currentCents_ = 0.0f;
    float smoothCoeff_ = 0.01f;
};

} // namespace pfl::dsp

#pragma once

#include <algorithm>
#include <cmath>

namespace pfl::dsp
{

/**
 * Final output protection: soft saturation + hard ceiling.
 * Musical DSP may be violent; this stage must never be bypassed.
 */
class SafetyLimiter
{
public:
    void reset() noexcept
    {
        envelope_ = 0.0f;
    }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = static_cast<float> (sampleRate);
        const float attackMs = 0.5f;
        const float releaseMs = 50.0f;
        attackCoeff_ = std::exp (-1.0f / (0.001f * attackMs * sampleRate_));
        releaseCoeff_ = std::exp (-1.0f / (0.001f * releaseMs * sampleRate_));
        reset();
    }

    float processSample (float x) noexcept
    {
        if (! std::isfinite (x))
            x = 0.0f;

        // Soft musical clamp before metering
        x = softClip (x);

        const float absX = std::abs (x);
        if (absX > envelope_)
            envelope_ = attackCoeff_ * envelope_ + (1.0f - attackCoeff_) * absX;
        else
            envelope_ = releaseCoeff_ * envelope_ + (1.0f - releaseCoeff_) * absX;

        float gain = 1.0f;
        if (envelope_ > ceiling_)
            gain = ceiling_ / envelope_;

        float y = x * gain;

        // Absolute hard ceiling — last line of defense
        y = std::clamp (y, -hardCeiling_, hardCeiling_);

        if (! std::isfinite (y))
            y = 0.0f;

        return y;
    }

    void processBlock (float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample (data[i]);
    }

private:
    static float softClip (float x) noexcept
    {
        // Gentle tanh-style saturation around ~0.9
        constexpr float drive = 1.2f;
        return std::tanh (x * drive) / std::tanh (drive);
    }

    float sampleRate_ = 44100.0f;
    float envelope_ = 0.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    static constexpr float ceiling_ = 0.95f;
    static constexpr float hardCeiling_ = 0.99f;
};

} // namespace pfl::dsp

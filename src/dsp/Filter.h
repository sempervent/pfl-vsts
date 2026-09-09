#pragma once

#include <algorithm>
#include <cmath>

namespace pfl::dsp
{

/** One-pole lowpass with optional mild resonance via feedback. */
class Filter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        updateCoeff();
        reset();
    }

    void reset() noexcept
    {
        lp_ = 0.0f;
        bp_ = 0.0f;
    }

    void setCutoffHz (float hz) noexcept
    {
        cutoffHz_ = std::clamp (hz, 20.0f, static_cast<float> (sampleRate_ * 0.45));
        updateCoeff();
    }

    void setResonance (float res01) noexcept
    {
        resonance_ = std::clamp (res01, 0.0f, 0.95f);
    }

    float processSample (float x) noexcept
    {
        // State-variable-ish 1-pole with feedback
        const float notch = x - resonance_ * bp_;
        lp_ += g_ * (notch - lp_);
        bp_ += g_ * (lp_ - bp_); // mild band emphasis path unused for output
        return lp_;
    }

private:
    void updateCoeff() noexcept
    {
        const float wc = cutoffHz_ / static_cast<float> (sampleRate_);
        g_ = 1.0f - std::exp (-2.0f * 3.14159265f * wc);
    }

    double sampleRate_ = 44100.0;
    float cutoffHz_ = 1200.0f;
    float resonance_ = 0.1f;
    float g_ = 0.1f;
    float lp_ = 0.0f;
    float bp_ = 0.0f;
};

} // namespace pfl::dsp

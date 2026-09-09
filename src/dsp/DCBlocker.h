#pragma once

#include <cmath>

namespace pfl::dsp
{

/** One-pole DC blocker. Real-time safe. */
class DCBlocker
{
public:
    void reset() noexcept
    {
        x1_ = 0.0f;
        y1_ = 0.0f;
    }

    void prepare (double /*sampleRate*/) noexcept
    {
        reset();
    }

    float processSample (float x) noexcept
    {
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        const float y = x - x1_ + R_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

    void processBlock (float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample (data[i]);
    }

private:
    static constexpr float R_ = 0.995f;
    float x1_ = 0.0f;
    float y1_ = 0.0f;
};

} // namespace pfl::dsp

#pragma once

#include <algorithm>
#include <cmath>

namespace pfl::dsp
{

class ParamSmoother
{
public:
    void prepare (double sampleRate, float timeSec = 0.05f) noexcept
    {
        sampleRate_ = sampleRate;
        setTime (timeSec);
        current_ = target_;
    }

    void setTime (float timeSec) noexcept
    {
        const float samples = std::max (1.0f, timeSec * static_cast<float> (sampleRate_));
        coeff_ = 1.0f - std::exp (-1.0f / samples);
    }

    void setCurrentAndTarget (float v) noexcept
    {
        current_ = target_ = v;
    }

    void setTarget (float v) noexcept { target_ = v; }

    float getNext() noexcept
    {
        current_ += (target_ - current_) * coeff_;
        return current_;
    }

    float current() const noexcept { return current_; }

private:
    double sampleRate_ = 44100.0;
    float current_ = 0.0f;
    float target_ = 0.0f;
    float coeff_ = 0.01f;
};

} // namespace pfl::dsp

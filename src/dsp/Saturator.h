#pragma once

#include <algorithm>
#include <cmath>

namespace pfl::dsp
{

class Saturator
{
public:
    void setDrive (float drive01) noexcept
    {
        drive_ = 0.5f + 4.0f * std::clamp (drive01, 0.0f, 1.0f);
    }

    float processSample (float x) noexcept
    {
        return std::tanh (x * drive_) / std::tanh (drive_);
    }

private:
    float drive_ = 1.0f;
};

} // namespace pfl::dsp

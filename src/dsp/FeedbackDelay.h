#pragma once

#include "ParamSmoother.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pfl::dsp
{

/**
 * Stereo feedback delay with crossfeed and saturating feedback.
 * Feedback is hard-capped; delay time / mix are smoothed.
 */
class FeedbackDelay
{
public:
    void prepare (double sampleRate, float maxDelaySeconds = 2.5f)
    {
        sampleRate_ = sampleRate;
        const int maxSamples = std::max (1, static_cast<int> (sampleRate * maxDelaySeconds) + 4);
        bufferL_.assign (static_cast<size_t> (maxSamples), 0.0f);
        bufferR_.assign (static_cast<size_t> (maxSamples), 0.0f);
        writeIndex_ = 0;
        maxSamples_ = maxSamples;

        delaySamplesL_.prepare (sampleRate, 0.08f);
        delaySamplesR_.prepare (sampleRate, 0.08f);
        feedback_.prepare (sampleRate, 0.05f);
        mix_.prepare (sampleRate, 0.05f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill (bufferR_.begin(), bufferR_.end(), 0.0f);
        writeIndex_ = 0;
        lpL_ = 0.0f;
        lpR_ = 0.0f;
    }

    /** space01 maps musically to delay time, feedback, and wet mix. */
    void setSpace (float space01) noexcept
    {
        space01 = std::clamp (space01, 0.0f, 1.0f);
        const float delaySecL = 0.08f + space01 * space01 * 0.82f;
        const float delaySecR = delaySecL * (1.0f + 0.17f * space01);
        delaySamplesL_.setTarget (delaySecL * static_cast<float> (sampleRate_));
        delaySamplesR_.setTarget (delaySecR * static_cast<float> (sampleRate_));

        const float fb = 0.15f + 0.62f * std::pow (space01, 0.85f);
        feedback_.setTarget (std::min (fb, 0.82f));
        mix_.setTarget (std::pow (space01, 0.7f));
    }

    void processSample (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float dL = std::clamp (delaySamplesL_.getNext(), 1.0f, static_cast<float> (maxSamples_ - 2));
        const float dR = std::clamp (delaySamplesR_.getNext(), 1.0f, static_cast<float> (maxSamples_ - 2));
        const float fb = feedback_.getNext();
        const float wet = mix_.getNext();
        const float dry = 1.0f - wet * 0.85f;

        const float delayedL = readHermite (bufferL_, dL);
        const float delayedR = readHermite (bufferR_, dR);

        constexpr float g = 0.28f;
        lpL_ += g * (delayedL - lpL_);
        lpR_ += g * (delayedR - lpR_);

        float fbInL = inL + fb * (0.82f * lpL_ + 0.18f * lpR_);
        float fbInR = inR + fb * (0.82f * lpR_ + 0.18f * lpL_);
        fbInL = std::tanh (fbInL);
        fbInR = std::tanh (fbInR);

        bufferL_[static_cast<size_t> (writeIndex_)] = fbInL;
        bufferR_[static_cast<size_t> (writeIndex_)] = fbInR;
        writeIndex_ = (writeIndex_ + 1) % maxSamples_;

        outL = dry * inL + wet * delayedL;
        outR = dry * inR + wet * delayedR;
    }

private:
    float readHermite (const std::vector<float>& buf, float delaySamples) const noexcept
    {
        float readPos = static_cast<float> (writeIndex_) - delaySamples;
        while (readPos < 0.0f)
            readPos += static_cast<float> (maxSamples_);

        const int i1 = static_cast<int> (readPos) % maxSamples_;
        const int i0 = (i1 - 1 + maxSamples_) % maxSamples_;
        const int i2 = (i1 + 1) % maxSamples_;
        const int i3 = (i1 + 2) % maxSamples_;
        const float t = readPos - std::floor (readPos);

        const float y0 = buf[static_cast<size_t> (i0)];
        const float y1 = buf[static_cast<size_t> (i1)];
        const float y2 = buf[static_cast<size_t> (i2)];
        const float y3 = buf[static_cast<size_t> (i3)];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    double sampleRate_ = 44100.0;
    int maxSamples_ = 1;
    int writeIndex_ = 0;
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    float lpL_ = 0.0f;
    float lpR_ = 0.0f;

    ParamSmoother delaySamplesL_;
    ParamSmoother delaySamplesR_;
    ParamSmoother feedback_;
    ParamSmoother mix_;
};

} // namespace pfl::dsp

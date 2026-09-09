#pragma once

#include <cmath>

namespace pfl::dsp
{

enum class Waveform
{
    Saw = 0,
    Triangle,
    Blend // saw→triangle morph via blend amount
};

/** Naive band-unlimited oscillator — acceptable for dark drones; anti-alias later if needed. */
class Oscillator
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        updatePhaseDelta();
    }

    void reset() noexcept
    {
        phase_ = 0.0;
    }

    void setFrequencyHz (double hz) noexcept
    {
        frequencyHz_ = hz > 0.0 ? hz : 0.0;
        updatePhaseDelta();
    }

    void setWaveform (Waveform w) noexcept { waveform_ = w; }

    /** 0 = saw, 1 = triangle when waveform is Blend. */
    void setBlend (float blend01) noexcept
    {
        blend_ = blend01 < 0.0f ? 0.0f : (blend01 > 1.0f ? 1.0f : blend01);
    }

    float processSample() noexcept
    {
        const float saw = static_cast<float> (phase_ * 2.0 - 1.0);
        const float tri = static_cast<float> (phase_ < 0.5
                                                 ? (phase_ * 4.0 - 1.0)
                                                 : (3.0 - phase_ * 4.0));

        float out = 0.0f;
        switch (waveform_)
        {
            case Waveform::Saw:      out = saw; break;
            case Waveform::Triangle: out = tri; break;
            case Waveform::Blend:    out = saw * (1.0f - blend_) + tri * blend_; break;
        }

        phase_ += phaseDelta_;
        if (phase_ >= 1.0)
            phase_ -= 1.0;

        return out;
    }

private:
    void updatePhaseDelta() noexcept
    {
        phaseDelta_ = sampleRate_ > 0.0 ? (frequencyHz_ / sampleRate_) : 0.0;
    }

    double sampleRate_ = 44100.0;
    double frequencyHz_ = 110.0;
    double phase_ = 0.0;
    double phaseDelta_ = 0.0;
    Waveform waveform_ = Waveform::Blend;
    float blend_ = 0.35f;
};

} // namespace pfl::dsp

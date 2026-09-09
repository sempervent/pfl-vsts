#pragma once

#include "Drift.h"
#include "Filter.h"
#include "Oscillator.h"
#include "Saturator.h"

#include <algorithm>
#include <cmath>

namespace pfl::dsp
{

struct VoiceParams
{
    float baseMidiNote = 50.0f; // D3 approx
    float level = 0.25f;
    float attackSec = 2.5f;
    float releaseSec = 4.0f;
    float osc2DetuneCents = 7.0f;
    float oscBlend = 0.4f;
    float filterCutoffHz = 900.0f;
    float filterRes = 0.15f;
    float satDrive = 0.2f;
    float driftAmount = 0.25f;
    float driftVoiceOffsetCents = 0.0f;
    bool gate = true;
};

/** Two-oscillator drone voice with long envelope, filter, light saturation, drift. */
class Voice
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        osc1_.prepare (sampleRate);
        osc2_.prepare (sampleRate);
        osc1_.setWaveform (Waveform::Blend);
        osc2_.setWaveform (Waveform::Blend);
        filter_.prepare (sampleRate);
        drift_.prepare (sampleRate);
        env_ = 0.0f;
        reset();
    }

    void reset() noexcept
    {
        osc1_.reset();
        osc2_.reset();
        filter_.reset();
        drift_.reset();
        env_ = 0.0f;
    }

    void setParams (const VoiceParams& p) noexcept
    {
        params_ = p;
        osc1_.setBlend (p.oscBlend);
        osc2_.setBlend (std::clamp (p.oscBlend + 0.15f, 0.0f, 1.0f));
        filter_.setCutoffHz (p.filterCutoffHz);
        filter_.setResonance (p.filterRes);
        saturator_.setDrive (p.satDrive);
        drift_.setAmount (p.driftAmount);
        drift_.setVoiceOffsetCents (p.driftVoiceOffsetCents);

        attackInc_ = p.attackSec > 0.0f ? (1.0f / (p.attackSec * static_cast<float> (sampleRate_))) : 1.0f;
        releaseInc_ = p.releaseSec > 0.0f ? (1.0f / (p.releaseSec * static_cast<float> (sampleRate_))) : 1.0f;
    }

    float processSample() noexcept
    {
        if (params_.gate)
            env_ = std::min (1.0f, env_ + attackInc_);
        else
            env_ = std::max (0.0f, env_ - releaseInc_);

        if (env_ <= 1.0e-5f && ! params_.gate)
            return 0.0f;

        const float ratio = drift_.processRatio();
        const float freq1 = midiToHz (params_.baseMidiNote) * ratio;
        const float freq2 = midiToHz (params_.baseMidiNote + params_.osc2DetuneCents / 100.0f) * ratio;

        osc1_.setFrequencyHz (freq1);
        osc2_.setFrequencyHz (freq2);

        float s = 0.5f * (osc1_.processSample() + osc2_.processSample());
        s = filter_.processSample (s);
        s = saturator_.processSample (s * 0.8f);
        return s * env_ * params_.level;
    }

    float envelope() const noexcept { return env_; }

private:
    static float midiToHz (float note) noexcept
    {
        return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
    }

    double sampleRate_ = 44100.0;
    VoiceParams params_{};
    Oscillator osc1_;
    Oscillator osc2_;
    Filter filter_;
    Saturator saturator_;
    Drift drift_;
    float env_ = 0.0f;
    float attackInc_ = 0.0f;
    float releaseInc_ = 0.0f;
};

} // namespace pfl::dsp

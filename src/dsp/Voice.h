#pragma once

#include "Drift.h"
#include "Oscillator.h"

#include <algorithm>
#include <cmath>

namespace pfl::dsp
{

struct VoiceParams
{
    float baseMidiNote = 50.0f;
    float level = 0.25f;
    float attackSec = 2.0f;
    float releaseSec = 3.5f;
    float osc2DetuneCents = 7.0f;
    float oscBlend = 0.4f;
    float pan = 0.0f; // -1..+1
    float driftAmount = 0.25f;
    float driftVoiceOffsetCents = 0.0f;
    bool gate = true;
};

/** Dual-oscillator drone voice. Pitch is external; no dirt/space here. */
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
        drift_.prepare (sampleRate);
        env_ = 0.0f;
        reset();
    }

    void reset() noexcept
    {
        osc1_.reset();
        osc2_.reset();
        drift_.reset();
        env_ = 0.0f;
    }

    void setParams (const VoiceParams& p) noexcept
    {
        params_ = p;
        osc1_.setBlend (p.oscBlend);
        osc2_.setBlend (std::clamp (p.oscBlend + 0.18f, 0.0f, 1.0f));
        drift_.setAmount (p.driftAmount);
        drift_.setVoiceOffsetCents (p.driftVoiceOffsetCents);

        attackInc_ = p.attackSec > 0.0f ? (1.0f / (p.attackSec * static_cast<float> (sampleRate_))) : 1.0f;
        releaseInc_ = p.releaseSec > 0.0f ? (1.0f / (p.releaseSec * static_cast<float> (sampleRate_))) : 1.0f;
    }

    void setDriftRng (pfl::generative::DeterministicRNG rng) noexcept
    {
        drift_.setRng (rng);
    }

    void processSample (float& outL, float& outR) noexcept
    {
        if (params_.gate)
            env_ = std::min (1.0f, env_ + attackInc_);
        else
            env_ = std::max (0.0f, env_ - releaseInc_);

        if (env_ <= 1.0e-5f && ! params_.gate)
        {
            outL = outR = 0.0f;
            return;
        }

        const float ratio = drift_.processRatio();
        const float freq1 = midiToHz (params_.baseMidiNote) * ratio;
        const float freq2 = midiToHz (params_.baseMidiNote + params_.osc2DetuneCents / 100.0f) * ratio;

        osc1_.setFrequencyHz (static_cast<double> (freq1));
        osc2_.setFrequencyHz (static_cast<double> (freq2));

        const float s = 0.5f * (osc1_.processSample() + osc2_.processSample()) * env_ * params_.level;

        // Constant-power pan
        const float pan = std::clamp (params_.pan, -1.0f, 1.0f);
        const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
        outL = s * std::cos (angle);
        outR = s * std::sin (angle);
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
    Drift drift_;
    float env_ = 0.0f;
    float attackInc_ = 0.0f;
    float releaseInc_ = 0.0f;
};

} // namespace pfl::dsp

#pragma once

#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pfl::dsp
{

/**
 * Wet-path attenuation windows for FRACTURED temporal identity.
 * Musical-time sparse gestures; dry path never touched.
 */
class RuinFractureEnvelope
{
public:
    void reset (uint64_t masterSeed) noexcept
    {
        rng_ = pfl::generative::DeterministicRNG::derived (masterSeed, 0x46524354ull); // FRCT
        active_ = false;
        gainL_ = gainR_ = 1.0f;
        lastSchedEval_ = -1;
        gestureStartPpq_ = 0.0;
        gestureEndPpq_ = 0.0;
        depth_ = 0.0f;
        edgeBeats_ = 0.02f;
        stereoSplit_ = false;
    }

    void rebuildTo (int endEval, float fractureAmount, float inst) noexcept
    {
        reset (/*seed kept via rng_ reseed below*/ 0);
        // Caller must call reset(seed) first; this only clears gestures.
        (void) endEval;
        (void) fractureAmount;
        (void) inst;
        active_ = false;
        gainL_ = gainR_ = 1.0f;
        lastSchedEval_ = -1;
    }

    void setSeed (uint64_t masterSeed) noexcept
    {
        rng_ = pfl::generative::DeterministicRNG::derived (masterSeed == 0 ? 1ull : masterSeed, 0x46524354ull);
        active_ = false;
        gainL_ = gainR_ = 1.0f;
        lastSchedEval_ = -1;
    }

    /** Call on 4-beat eval boundaries when fracture is eligible. */
    void maybeSchedule (int evalIndex, double evalPpq, float fractureAmount, float inst) noexcept
    {
        if (evalIndex == lastSchedEval_)
            return;
        lastSchedEval_ = evalIndex;

        if (fractureAmount < 0.05f || inst < 1.0e-4f || active_)
            return;

        const float p = fractureAmount * inst * 0.35f;
        if (rng_.nextFloat() >= p)
            return;

        // Duration: 1/16, 1/8, 1/4, 1/2 beat
        static constexpr float kDur[] = { 0.0625f, 0.125f, 0.25f, 0.5f };
        const int di = static_cast<int> (rng_.nextFloat() * 4.0f) % 4;
        const float dur = kDur[di];
        // Onset offset within the 4-beat window
        const float offset = rng_.nextFloat() * 3.5f;
        gestureStartPpq_ = evalPpq + static_cast<double> (offset);
        gestureEndPpq_ = gestureStartPpq_ + static_cast<double> (dur);
        depth_ = 0.35f + 0.55f * fractureAmount * (0.5f + 0.5f * inst);
        edgeBeats_ = std::clamp (0.02f + 0.04f * (1.0f - inst), 0.01f, dur * 0.35f);
        stereoSplit_ = (rng_.nextFloat() < 0.30f * fractureAmount);
        active_ = true;
    }

    void advance (double ppq) noexcept
    {
        if (! active_)
        {
            gainL_ = gainR_ = 1.0f;
            return;
        }

        if (ppq < gestureStartPpq_)
        {
            gainL_ = gainR_ = 1.0f;
            return;
        }
        if (ppq >= gestureEndPpq_)
        {
            active_ = false;
            gainL_ = gainR_ = 1.0f;
            return;
        }

        const double len = gestureEndPpq_ - gestureStartPpq_;
        const double t = (ppq - gestureStartPpq_) / std::max (1.0e-9, len);
        const double edge = static_cast<double> (edgeBeats_) / std::max (1.0e-9, len);
        float env = 1.0f;
        if (t < edge)
            env = static_cast<float> (t / edge);
        else if (t > 1.0 - edge)
            env = static_cast<float> ((1.0 - t) / edge);
        env = std::clamp (env, 0.0f, 1.0f);
        // Hann-ish soften
        env = 0.5f - 0.5f * std::cos (env * 3.14159265f);

        const float g = 1.0f - depth_ * env;
        gainL_ = g;
        gainR_ = stereoSplit_ ? (1.0f - depth_ * env * 0.55f) : g;
    }

    float gainL() const noexcept { return gainL_; }
    float gainR() const noexcept { return gainR_; }
    bool active() const noexcept { return active_; }

private:
    pfl::generative::DeterministicRNG rng_;
    bool active_ = false;
    bool stereoSplit_ = false;
    int lastSchedEval_ = -1;
    double gestureStartPpq_ = 0.0;
    double gestureEndPpq_ = 0.0;
    float depth_ = 0.0f;
    float edgeBeats_ = 0.02f;
    float gainL_ = 1.0f;
    float gainR_ = 1.0f;
};

} // namespace pfl::dsp

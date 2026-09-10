#pragma once

#include "DCBlocker.h"
#include "FeedbackDelay.h"
#include "Filter.h"
#include "ParamSmoother.h"
#include "SafetyLimiter.h"
#include "Saturator.h"

#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pfl::dsp
{

/**
 * Ruin Engine Stage 1 core: deterministic evolving degradation of external audio.
 * Musical-time structural decisions are buffer-independent (4-beat eval grid).
 */
class RuinEngine
{
public:
    static constexpr int kAlgorithmVersion = 1;
    static constexpr float kMaxFeedback = 0.72f;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        filterL_.prepare (sampleRate_);
        filterR_.prepare (sampleRate_);
        delay_.prepare (sampleRate_, 2.5f);
        dcL_.prepare (sampleRate_);
        dcR_.prepare (sampleRate_);
        limL_.prepare (sampleRate_);
        limR_.prepare (sampleRate_);

        mixSmooth_.prepare (sampleRate_, 0.05f);
        ageSmooth_.prepare (sampleRate_, 0.08f);
        instSmooth_.prepare (sampleRate_, 0.08f);
        outSmooth_.prepare (sampleRate_, 0.05f);
        cutoffSmooth_.prepare (sampleRate_, 0.04f);
        driveSmooth_.prepare (sampleRate_, 0.04f);
        delaySecSmooth_.prepare (sampleRate_, 0.08f);
        fbSmooth_.prepare (sampleRate_, 0.05f);
        noiseSmooth_.prepare (sampleRate_, 0.05f);

        delay_.setInternalWet (1.0f);
        delay_.setFullWetMode (true);
        rebuildRng();
        reset();
        applyAgeCurves (0.0f, 0.0f, true);
    }

    void reset() noexcept
    {
        filterL_.reset();
        filterR_.reset();
        delay_.reset();
        dcL_.reset();
        dcR_.reset();
        limL_.reset();
        limR_.reset();
        saturator_ = Saturator{};
        lfoPhase_ = 0.0;
        walk_ = 0.0f;
        walkTarget_ = 0.0f;
        samplesUntilWalk_ = 0;
        lastEvalIndex_ = -1;
        barsUntilRetarget_ = 8;
        tone_ = grit_ = wobble_ = smear_ = 0.0f;
        toneT_ = gritT_ = wobbleT_ = smearT_ = 0.0f;
        evolutionPaused_ = false;
    }

    void setSeed (uint64_t seed) noexcept
    {
        if (seed == masterSeed_)
            return;
        masterSeed_ = seed == 0 ? 1ull : seed;
        rebuildRng();
        reset();
        applyAgeCurves (ageSmooth_.current(), instSmooth_.current(), true);
    }

    uint64_t seed() const noexcept { return masterSeed_; }

    void setMix (float v) noexcept { mixSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setAge (float v) noexcept { ageSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setInstability (float v) noexcept { instSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setOutput (float v) noexcept { outSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }

    /** Snap macro smoothers to their targets (prepare / offline setup). */
    void snapMacros() noexcept
    {
        mixSmooth_.setCurrentAndTarget (mixSmooth_.target());
        ageSmooth_.setCurrentAndTarget (ageSmooth_.target());
        instSmooth_.setCurrentAndTarget (instSmooth_.target());
        outSmooth_.setCurrentAndTarget (outSmooth_.target());
        applyAgeCurves (ageSmooth_.current(), instSmooth_.current(), true);
    }

    /** Pause structural evolution (bypass / transport stop). No catch-up on resume. */
    void setEvolutionPaused (bool paused) noexcept
    {
        if (paused && ! evolutionPaused_)
            discardPendingEvals_ = true;
        evolutionPaused_ = paused;
    }

    bool evolutionPaused() const noexcept { return evolutionPaused_; }

    float tone() const noexcept { return tone_; }
    float grit() const noexcept { return grit_; }
    float wobble() const noexcept { return wobble_; }
    float smear() const noexcept { return smear_; }

    /**
     * Process interleaved stereo (or mono duplicated by caller).
     * ppqStart is musical position at first sample; bpm > 0.
     * When transportPlaying is false, structural evolution pauses.
     */
    void process (float* left, float* right, int numSamples,
                  bool transportPlaying, double ppqStart, double bpm) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;

        setEvolutionPaused (! transportPlaying || bypassed_);

        const double safeBpm = bpm > 1.0 ? bpm : 120.0;
        const double beatsPerSample = (safeBpm / 60.0) / sampleRate_;

        for (int i = 0; i < numSamples; ++i)
        {
            const double ppq = ppqStart + static_cast<double> (i) * beatsPerSample;
            advanceStructure (ppq);

            const float age = ageSmooth_.getNext();
            const float inst = instSmooth_.getNext();
            const float mix = mixSmooth_.getNext();
            const float outG = outSmooth_.getNext();

            // Morph profile toward targets
            const float morph = 0.0008f + 0.0025f * inst;
            tone_ += (toneT_ - tone_) * morph;
            grit_ += (gritT_ - grit_) * morph;
            wobble_ += (wobbleT_ - wobble_) * morph;
            smear_ += (smearT_ - smear_) * morph;

            applyAgeCurves (age, inst, false);
            updateMicroMotion (age, inst);

            const float cutoff = cutoffSmooth_.getNext();
            const float drive = driveSmooth_.getNext();
            const float delaySec = delaySecSmooth_.getNext();
            const float fb = fbSmooth_.getNext();
            const float noiseAmt = noiseSmooth_.getNext();

            float inL = left[i];
            float inR = right[i];
            if (! std::isfinite (inL)) inL = 0.0f;
            if (! std::isfinite (inR)) inR = 0.0f;

            const float dryL = inL;
            const float dryR = inR;

            float wetL = dryL;
            float wetR = dryR;

            // AGE≈0: wet path is identity (true transparency). Still advance smoothers above.
            if (age > 1.0e-4f)
            {
                filterL_.setCutoffHz (cutoff);
                filterR_.setCutoffHz (cutoff * 1.03f);
                filterL_.setResonance (0.05f + 0.35f * grit_);
                filterR_.setResonance (0.05f + 0.35f * grit_);
                saturator_.setDrive (drive);
                delay_.setDelaySeconds (delaySec, delaySec * (1.0f + 0.12f * smear_));
                delay_.setFeedbackAmount (fb);

                float wL = filterL_.processSample (inL);
                float wR = filterR_.processSample (inR);
                wL = saturator_.processSample (wL);
                wR = saturator_.processSample (wR);

                if (noiseAmt > 1.0e-6f)
                {
                    // Gate noise by input energy so fresh silence does not self-noise.
                    const float energy = std::min (1.0f, (std::abs (inL) + std::abs (inR)) * 6.0f);
                    if (energy > 1.0e-5f)
                    {
                        wL += (noiseRng_.nextFloat() * 2.0f - 1.0f) * noiseAmt * energy;
                        wR += (noiseRng_.nextFloat() * 2.0f - 1.0f) * noiseAmt * energy;
                    }
                }

                float dL = 0.0f, dR = 0.0f;
                delay_.processSample (wL, wR, dL, dR);
                wetL = dcL_.processSample (dL);
                wetR = dcR_.processSample (dR);
                wetL = limL_.processSample (wetL);
                wetR = limR_.processSample (wetR);
            }

            float outL = dryL * (1.0f - mix) + wetL * mix;
            float outR = dryR * (1.0f - mix) + wetR * mix;
            outL = std::clamp (outL * outG, -0.99f, 0.99f);
            outR = std::clamp (outR * outG, -0.99f, 0.99f);
            if (! std::isfinite (outL)) outL = 0.0f;
            if (! std::isfinite (outR)) outR = 0.0f;

            left[i] = outL;
            right[i] = outR;
        }
    }

    void setBypassed (bool b) noexcept
    {
        bypassed_ = b;
        if (b)
            setEvolutionPaused (true);
    }

private:
    void rebuildRng() noexcept
    {
        structureRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x53545255ull); // STRU
        profileRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x50524F46ull);   // PROF
        instabilityRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x494E5354ull); // INST
        noiseRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x4E4F4953ull);     // NOIS
    }

    void advanceStructure (double ppq) noexcept
    {
        // 4-beat evaluation grid — absolute musical index (seed + timeline deterministic)
        const int evalIndex = static_cast<int> (std::floor (ppq / 4.0));
        if (evalIndex == lastEvalIndex_)
            return;

        if (evolutionPaused_ || discardPendingEvals_)
        {
            lastEvalIndex_ = evalIndex;
            discardPendingEvals_ = false;
            return;
        }

        // Seek / first block / large jump: rebuild structural state from eval 0..evalIndex
        // so SEED + absolute PPQ reproduce the same profile regardless of insert time.
        if (lastEvalIndex_ < 0 || evalIndex < lastEvalIndex_ || evalIndex > lastEvalIndex_ + 64)
        {
            rebuildStructuralTo (evalIndex);
            return;
        }

        for (int idx = lastEvalIndex_ + 1; idx <= evalIndex; ++idx)
            evaluateAt (idx);
        lastEvalIndex_ = evalIndex;
    }

    void rebuildStructuralTo (int evalIndex) noexcept
    {
        rebuildRng();
        tone_ = grit_ = wobble_ = smear_ = 0.0f;
        toneT_ = gritT_ = wobbleT_ = smearT_ = 0.0f;
        barsUntilRetarget_ = 8;
        applyAgeCurves (ageSmooth_.current(), instSmooth_.current(), true);
        const int end = std::max (0, evalIndex);
        for (int idx = 0; idx <= end; ++idx)
            evaluateAt (idx);
        lastEvalIndex_ = end;
        // Snap current profile to targets after catch-up (avoid long morph from zero)
        tone_ = toneT_;
        grit_ = gritT_;
        wobble_ = wobbleT_;
        smear_ = smearT_;
    }

    void evaluateAt (int /*evalIndex*/) noexcept
    {
        const float inst = instSmooth_.current();
        if (inst < 1.0e-4f)
            return;

        if (barsUntilRetarget_ > 0)
        {
            --barsUntilRetarget_;
            return;
        }

        // Stay bias even at high instability
        const float stay = 0.55f - 0.35f * inst;
        if (structureRng_.nextFloat() < stay)
        {
            barsUntilRetarget_ = rollLifespan (inst);
            return;
        }

        nudgeProfile (inst);
    }

    int rollLifespan (float inst) noexcept
    {
        // Eval units (4 beats each): low inst ~8–16, high ~2–4
        const float span = 2.0f + 10.0f * (1.0f - inst);
        return 2 + static_cast<int> (structureRng_.nextFloat() * span);
    }

    void nudgeProfile (float inst) noexcept
    {
        const float maxDelta = 0.04f + 0.12f * inst;
        const int which = static_cast<int> (profileRng_.nextFloat() * 4.0f) % 4;
        auto nudge = [&] (float& t)
        {
            t = std::clamp (t + profileRng_.nextFloat (-maxDelta, maxDelta), 0.0f, 1.0f);
        };
        if (which == 0) nudge (toneT_);
        else if (which == 1) nudge (gritT_);
        else if (which == 2) nudge (wobbleT_);
        else nudge (smearT_);

        barsUntilRetarget_ = rollLifespan (inst);
    }

    void applyAgeCurves (float age, float inst, bool snap) noexcept
    {
        // Smoothstep AGE so low values stay nearly clean
        const float a = age * age * (3.0f - 2.0f * age);

        // Base coherent targets from AGE; profile nudges sit on top
        const float toneBase = a;
        const float gritBase = a;
        const float wobbleBase = a * (0.35f + 0.65f * inst);
        const float smearBase = a;

        if (snap)
        {
            toneT_ = tone_ = toneBase;
            gritT_ = grit_ = gritBase;
            wobbleT_ = wobble_ = wobbleBase;
            smearT_ = smear_ = smearBase;
        }
        // else: structural targets (toneT_…) only change via nudgeProfile —
        // do not continuously pull them back to AGE (preserves timeline determinism).

        const float t = std::clamp (0.62f * toneBase + 0.38f * tone_, 0.0f, 1.0f);
        const float g = std::clamp (0.62f * gritBase + 0.38f * grit_, 0.0f, 1.0f);
        const float s = std::clamp (0.62f * smearBase + 0.38f * smear_, 0.0f, 1.0f);

        // AGE=0 → transparent wet: open filter, no drive, no noise, no feedback
        const float cutoff = 18000.0f * std::pow (0.08f, t) + 220.0f;
        const float drive = a < 1.0e-4f ? 0.0f : (0.05f + 0.85f * g);
        const float noise = a < 0.15f ? 0.0f : (0.002f + 0.06f * std::pow ((a - 0.15f) / 0.85f, 1.3f));
        const float delaySec = 0.03f + 0.55f * s * s;
        const float fb = a < 1.0e-4f ? 0.0f : std::min (kMaxFeedback, 0.12f + 0.55f * s);

        if (snap)
        {
            cutoffSmooth_.setCurrentAndTarget (cutoff);
            driveSmooth_.setCurrentAndTarget (drive);
            noiseSmooth_.setCurrentAndTarget (noise);
            delaySecSmooth_.setCurrentAndTarget (delaySec);
            fbSmooth_.setCurrentAndTarget (fb);
            mixSmooth_.setCurrentAndTarget (mixSmooth_.current());
            ageSmooth_.setCurrentAndTarget (age);
            instSmooth_.setCurrentAndTarget (inst);
            outSmooth_.setCurrentAndTarget (outSmooth_.current());
        }
        else
        {
            cutoffSmooth_.setTarget (cutoff * microCutoffMul_);
            driveSmooth_.setTarget (drive);
            noiseSmooth_.setTarget (noise);
            delaySecSmooth_.setTarget (delaySec * microDelayMul_);
            fbSmooth_.setTarget (fb);
        }
    }

    void updateMicroMotion (float age, float inst) noexcept
    {
        const float depth = age * inst * wobble_;
        if (depth < 1.0e-5f)
        {
            microCutoffMul_ = 1.0f;
            microDelayMul_ = 1.0f;
            return;
        }

        const double lfoHz = 0.03 + 0.12 * static_cast<double> (inst);
        lfoPhase_ += lfoHz / sampleRate_;
        if (lfoPhase_ >= 1.0)
            lfoPhase_ -= 1.0;
        const float lfo = static_cast<float> (std::sin (lfoPhase_ * 6.283185307179586));

        if (--samplesUntilWalk_ <= 0)
        {
            const float intervalSec = 5.0f - 3.5f * inst;
            samplesUntilWalk_ = std::max (1, static_cast<int> (intervalSec * sampleRate_));
            walkTarget_ = instabilityRng_.nextFloat (-1.0f, 1.0f);
        }
        walk_ += (walkTarget_ - walk_) * (0.00002f + 0.0001f * inst);

        const float mod = (0.04f * lfo + 0.03f * walk_) * depth;
        microCutoffMul_ = std::clamp (1.0f + mod, 0.85f, 1.15f);
        microDelayMul_ = std::clamp (1.0f + 0.7f * mod, 0.9f, 1.1f);
    }

    double sampleRate_ = 44100.0;
    uint64_t masterSeed_ = 2002;
    bool evolutionPaused_ = false;
    bool discardPendingEvals_ = false;
    bool bypassed_ = false;

    Filter filterL_, filterR_;
    Saturator saturator_;
    FeedbackDelay delay_;
    DCBlocker dcL_, dcR_;
    SafetyLimiter limL_, limR_;

    ParamSmoother mixSmooth_, ageSmooth_, instSmooth_, outSmooth_;
    ParamSmoother cutoffSmooth_, driveSmooth_, delaySecSmooth_, fbSmooth_, noiseSmooth_;

    pfl::generative::DeterministicRNG structureRng_, profileRng_, instabilityRng_, noiseRng_;

    float tone_ = 0, grit_ = 0, wobble_ = 0, smear_ = 0;
    float toneT_ = 0, gritT_ = 0, wobbleT_ = 0, smearT_ = 0;
    int barsUntilRetarget_ = 8;
    int lastEvalIndex_ = -1;

    double lfoPhase_ = 0.0;
    float walk_ = 0.0f;
    float walkTarget_ = 0.0f;
    int samplesUntilWalk_ = 0;
    float microCutoffMul_ = 1.0f;
    float microDelayMul_ = 1.0f;
};

} // namespace pfl::dsp

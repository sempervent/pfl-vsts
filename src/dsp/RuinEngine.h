#pragma once

#include "DCBlocker.h"
#include "FeedbackDelay.h"
#include "Filter.h"
#include "ParamSmoother.h"
#include "RuinFracture.h"
#include "RuinStateMachine.h"
#include "SafetyLimiter.h"
#include "Saturator.h"
#include "WearAccumulator.h"

#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pfl::dsp
{

/**
 * Ruin Engine Stage 3: Stage 2 states + bounded WearState processing history.
 * Wear is exposure memory (not audio memory). Musical-time structural decisions
 * remain buffer-independent on a 4-beat eval grid.
 */
class RuinEngine
{
public:
    static constexpr int kAlgorithmVersion = 3;
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
        // Preserve WearState across prepare (buffer-size / SR changes must not erase scars).
        const auto savedWear = wear_.wear();
        const auto savedFloor = wear_.scarFloor();
        rebuildRng();
        reset();
        wear_.setWearAndFloor (savedWear, savedFloor);
        syncTargetsFromState (true);
        mapDspTargets (0.0f, 0.0f, true);
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
        evolutionPaused_ = false;
        discardPendingEvals_ = false;
        tone_ = grit_ = wobble_ = smear_ = 0.0f;
        toneT_ = gritT_ = wobbleT_ = smearT_ = 0.0f;
        fractureAmt_ = fractureAmtT_ = 0.0f;
        stateMachine_.reset (masterSeed_);
        fracture_.setSeed (masterSeed_);
        // WearState is not cleared here — physical processing history survives DSP reset.
        activitySum_ = 0.0;
        activityCount_ = 0;
        syncTargetsFromState (true);
    }

    void setSeed (uint64_t seed) noexcept
    {
        if (seed == masterSeed_)
            return;
        // SEED changes generative personality; preserve accumulated WearState.
        const auto savedWear = wear_.wear();
        const auto savedFloor = wear_.scarFloor();
        masterSeed_ = seed == 0 ? 1ull : seed;
        rebuildRng();
        reset();
        wear_.setWearAndFloor (savedWear, savedFloor);
        syncTargetsFromState (true);
        mapDspTargets (ageSmooth_.current(), instSmooth_.current(), true);
    }

    /** Diagnostic / test only — not a public control. */
    void resetWearFresh() noexcept
    {
        wear_.resetFresh();
        syncTargetsFromState (true);
        mapDspTargets (ageSmooth_.current(), instSmooth_.current(), true);
    }

    WearState wearState() const noexcept { return wear_.wear(); }
    WearState scarFloor() const noexcept { return wear_.scarFloor(); }

    void setWearState (WearState w, WearState floor = {}) noexcept
    {
        wear_.setWearAndFloor (w, floor);
        syncTargetsFromState (true);
        mapDspTargets (ageSmooth_.current(), instSmooth_.current(), true);
    }

    /** Force all wear dimensions to 1.0 for safety soak tests. */
    void setWearMaxDiagnostic() noexcept
    {
        WearState w { 1.0f, 1.0f, 1.0f };
        wear_.setWearAndFloor (w, WearState { 0.55f, 0.55f, 0.60f });
        syncTargetsFromState (true);
        mapDspTargets (ageSmooth_.current(), instSmooth_.current(), true);
    }

    uint64_t seed() const noexcept { return masterSeed_; }

    void setMix (float v) noexcept { mixSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setAge (float v) noexcept { ageSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setInstability (float v) noexcept { instSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setOutput (float v) noexcept { outSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }

    void snapMacros() noexcept
    {
        mixSmooth_.setCurrentAndTarget (mixSmooth_.target());
        ageSmooth_.setCurrentAndTarget (ageSmooth_.target());
        instSmooth_.setCurrentAndTarget (instSmooth_.target());
        outSmooth_.setCurrentAndTarget (outSmooth_.target());
        syncTargetsFromState (true);
        mapDspTargets (ageSmooth_.current(), instSmooth_.current(), true);
    }

    void setEvolutionPaused (bool paused) noexcept
    {
        if (paused && ! evolutionPaused_)
            discardPendingEvals_ = true;
        evolutionPaused_ = paused;
    }

    bool evolutionPaused() const noexcept { return evolutionPaused_; }

    void setBypassed (bool b) noexcept
    {
        if (b)
        {
            // Always discard pending evals on bypass entry so unmute cannot
            // catch up wear for PPQ that advanced while process() was skipped.
            discardPendingEvals_ = true;
            evolutionPaused_ = true;
            wear_.setPaused (true);
        }
        else
        {
            wear_.setPaused (false);
        }
        bypassed_ = b;
    }

    /** Offline / diagnostic only — not a public plugin parameter. */
    void forceProcessingState (bool on, RuinProcessingState s) noexcept
    {
        stateMachine_.setForcedState (on, s);
        syncTargetsFromState (true);
        mapDspTargets (ageSmooth_.current(), instSmooth_.current(), true);
    }

    float tone() const noexcept { return tone_; }
    float grit() const noexcept { return grit_; }
    float wobble() const noexcept { return wobble_; }
    float smear() const noexcept { return smear_; }

    RuinProcessingState processingState() const noexcept { return stateMachine_.state(); }
    RuinProcessingState previousProcessingState() const noexcept { return stateMachine_.previousState(); }
    float damagePressure() const noexcept { return stateMachine_.damagePressure(); }
    float recoveryPressure() const noexcept { return stateMachine_.recoveryPressure(); }
    float fractureAmount() const noexcept { return fractureAmt_; }

    void process (float* left, float* right, int numSamples,
                  bool transportPlaying, double ppqStart, double bpm) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;

        setEvolutionPaused (! transportPlaying || bypassed_);
        wear_.setPaused (! transportPlaying || bypassed_);

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

            const float morph = 0.0008f + 0.0025f * inst;
            tone_ += (toneT_ - tone_) * morph;
            grit_ += (gritT_ - grit_) * morph;
            wobble_ += (wobbleT_ - wobble_) * morph;
            smear_ += (smearT_ - smear_) * morph;
            fractureAmt_ += (fractureAmtT_ - fractureAmt_) * morph;

            mapDspTargets (age, inst, false);
            updateMicroMotion (age, inst);
            fracture_.advance (ppq);

            const float cutoff = cutoffSmooth_.getNext();
            const float drive = driveSmooth_.getNext();
            const float delaySec = delaySecSmooth_.getNext();
            const float fb = fbSmooth_.getNext();
            const float noiseAmt = noiseSmooth_.getNext();

            float inL = left[i];
            float inR = right[i];
            if (! std::isfinite (inL)) inL = 0.0f;
            if (! std::isfinite (inR)) inR = 0.0f;

            // Soft input activity for the current eval window (near-silence → ~0).
            {
                const float energy = std::min (1.0f, (std::abs (inL) + std::abs (inR)) * 4.0f);
                activitySum_ += energy;
                ++activityCount_;
            }

            const float dryL = inL;
            const float dryR = inR;

            float wetL = dryL;
            float wetR = dryR;

            // AGE≈0: wet path is identity (Stage 1 contract). Still advance smoothers above.
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

                wetL *= fracture_.gainL();
                wetR *= fracture_.gainR();
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

private:
    void rebuildRng() noexcept
    {
        noiseRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x4E4F4953ull); // NOIS
        instabilityRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x494E5354ull); // INST
    }

    void syncTargetsFromState (bool snap) noexcept
    {
        const auto& t = stateMachine_.targets();
        float tone = t.tone;
        float grit = t.grit;
        float wobble = t.wobble;
        float smear = t.smear;
        float frac = t.fractureAmount;
        wear_.applyToProfile (stateMachine_.state(), tone, grit, smear, frac);
        (void) wobble; // wobble remains state/inst only — wear does not redefine restlessness

        if (snap)
        {
            toneT_ = tone_ = tone;
            gritT_ = grit_ = grit;
            wobbleT_ = wobble_ = t.wobble;
            smearT_ = smear_ = smear;
            fractureAmtT_ = fractureAmt_ = frac;
        }
        else
        {
            toneT_ = tone;
            gritT_ = grit;
            wobbleT_ = t.wobble;
            smearT_ = smear;
            fractureAmtT_ = frac;
        }
    }

    void advanceStructure (double ppq) noexcept
    {
        const int evalIndex = static_cast<int> (std::floor (ppq / 4.0));
        if (evalIndex == lastEvalIndex_)
            return;

        if (evolutionPaused_ || discardPendingEvals_)
        {
            lastEvalIndex_ = evalIndex;
            discardPendingEvals_ = false;
            activitySum_ = 0.0;
            activityCount_ = 0;
            return;
        }

        const float age = ageSmooth_.current();
        const float inst = instSmooth_.current();
        const float mix = mixSmooth_.current();

        if (lastEvalIndex_ < 0 || evalIndex < lastEvalIndex_ || evalIndex > lastEvalIndex_ + 64)
        {
            // Seek / large jump: reconstruct structural state from PPQ.
            // Do NOT fabricate WearState from skipped exposure time.
            rebuildStructuralTo (evalIndex, age, inst);
            return;
        }

        const float activity = activityCount_ > 0
                                   ? static_cast<float> (activitySum_ / static_cast<double> (activityCount_))
                                   : 0.0f;
        activitySum_ = 0.0;
        activityCount_ = 0;

        for (int idx = lastEvalIndex_ + 1; idx <= evalIndex; ++idx)
            stateMachine_.evaluateAt (idx, age, inst);

        const int steps = evalIndex - lastEvalIndex_;
        if (steps > 0)
        {
            wear_.accumulate (4.0f * static_cast<float> (steps),
                              stateMachine_.state(),
                              age,
                              inst,
                              mix,
                              activity,
                              stateMachine_.recoveryPressure(),
                              0.0f);
            syncTargetsFromState (false);
            for (int idx = lastEvalIndex_ + 1; idx <= evalIndex; ++idx)
            {
                const double evalPpq = static_cast<double> (idx) * 4.0;
                fracture_.maybeSchedule (idx, evalPpq, fractureAmtT_, inst);
            }
        }
        lastEvalIndex_ = evalIndex;
    }

    void rebuildStructuralTo (int evalIndex, float age, float inst) noexcept
    {
        // Do NOT reset noiseRng_/instabilityRng_ — sample-rate streams must stay continuous
        // across seek; only musical-time structural state is reconstructed.
        // WearState is intentionally preserved (no PPQ-fabricated aging).
        fracture_.setSeed (masterSeed_);
        stateMachine_.reset (masterSeed_);
        stateMachine_.seedInitialFromAge (age, inst);
        const int end = std::max (0, evalIndex);
        for (int idx = 0; idx <= end; ++idx)
        {
            stateMachine_.evaluateAt (idx, age, inst);
            const double evalPpq = static_cast<double> (idx) * 4.0;
            fracture_.maybeSchedule (idx, evalPpq, stateMachine_.targets().fractureAmount, inst);
            // Advance through the 4-beat window so gestures can complete between schedules
            for (double p = evalPpq; p < evalPpq + 4.0; p += 0.125)
                fracture_.advance (p);
        }
        activitySum_ = 0.0;
        activityCount_ = 0;
        syncTargetsFromState (true);
        mapDspTargets (age, inst, true);
        lastEvalIndex_ = end;
    }

    void mapDspTargets (float age, float inst, bool snap) noexcept
    {
        const float a = age * age * (3.0f - 2.0f * age);
        const float t = std::clamp (0.55f * tone_ + 0.45f * a, 0.0f, 1.0f);
        const float g = std::clamp (0.55f * grit_ + 0.45f * a, 0.0f, 1.0f);
        const float s = std::clamp (0.55f * smear_ + 0.45f * a, 0.0f, 1.0f);

        const float cutoff = 18000.0f * std::pow (0.08f, t) + 220.0f;
        const float drive = a < 1.0e-4f ? 0.0f : (0.05f + 0.85f * g);
        const float noise = a < 0.15f ? 0.0f
                                      : (0.002f + 0.06f * std::pow ((a - 0.15f) / 0.85f, 1.3f) * g);
        const float delaySec = 0.03f + 0.55f * s * s;
        // Wear may color feedback character but never exceeds Stage 1 hard cap.
        const float fb = a < 1.0e-4f ? 0.0f : std::min (kMaxFeedback, 0.12f + 0.55f * s);

        if (snap)
        {
            cutoffSmooth_.setCurrentAndTarget (cutoff);
            driveSmooth_.setCurrentAndTarget (drive);
            noiseSmooth_.setCurrentAndTarget (noise);
            delaySecSmooth_.setCurrentAndTarget (delaySec);
            fbSmooth_.setCurrentAndTarget (fb);
        }
        else
        {
            cutoffSmooth_.setTarget (cutoff * microCutoffMul_);
            driveSmooth_.setTarget (drive);
            noiseSmooth_.setTarget (noise);
            delaySecSmooth_.setTarget (delaySec * microDelayMul_);
            fbSmooth_.setTarget (fb);
        }
        (void) inst;
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
    int lastEvalIndex_ = -1;

    Filter filterL_, filterR_;
    Saturator saturator_;
    FeedbackDelay delay_;
    DCBlocker dcL_, dcR_;
    SafetyLimiter limL_, limR_;
    RuinStateMachine stateMachine_;
    RuinFractureEnvelope fracture_;
    WearAccumulator wear_;
    double activitySum_ = 0.0;
    int activityCount_ = 0;

    ParamSmoother mixSmooth_, ageSmooth_, instSmooth_, outSmooth_;
    ParamSmoother cutoffSmooth_, driveSmooth_, delaySecSmooth_, fbSmooth_, noiseSmooth_;

    pfl::generative::DeterministicRNG noiseRng_, instabilityRng_;

    float tone_ = 0, grit_ = 0, wobble_ = 0, smear_ = 0, fractureAmt_ = 0;
    float toneT_ = 0, gritT_ = 0, wobbleT_ = 0, smearT_ = 0, fractureAmtT_ = 0;

    double lfoPhase_ = 0.0;
    float walk_ = 0.0f;
    float walkTarget_ = 0.0f;
    int samplesUntilWalk_ = 0;
    float microCutoffMul_ = 1.0f;
    float microDelayMul_ = 1.0f;
};

} // namespace pfl::dsp

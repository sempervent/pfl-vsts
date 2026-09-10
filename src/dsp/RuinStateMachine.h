#pragma once

#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pfl::dsp
{

enum class RuinProcessingState : uint8_t
{
    Intact = 0,
    Weathered,
    Fractured,
    Ruined,
    Recovering,
    Count
};

inline const char* ruinStateName (RuinProcessingState s) noexcept
{
    switch (s)
    {
        case RuinProcessingState::Intact: return "INTACT";
        case RuinProcessingState::Weathered: return "WEATHERED";
        case RuinProcessingState::Fractured: return "FRACTURED";
        case RuinProcessingState::Ruined: return "RUINED";
        case RuinProcessingState::Recovering: return "RECOVERING";
        default: return "?";
    }
}

struct RuinProfileTargets
{
    float tone = 0.0f;
    float grit = 0.0f;
    float wobble = 0.0f;
    float smear = 0.0f;
    float fractureAmount = 0.0f;
};

/**
 * Stage 2 state machine: constrained graph, dwell, damage/recovery pressure.
 * Replayable from (seed, evalIndex, age, inst) via rebuildFromScratch.
 */
class RuinStateMachine
{
public:
    void reset (uint64_t masterSeed) noexcept
    {
        masterSeed_ = masterSeed == 0 ? 1ull : masterSeed;
        rebuildRng();
        state_ = RuinProcessingState::Intact;
        previousState_ = RuinProcessingState::Intact;
        damagePressure_ = 0.0f;
        recoveryPressure_ = 0.0f;
        severityAtEntry_ = 0.0f;
        dwellEvals_ = 0;
        minDwellEvals_ = 4;
        majorPeriod_ = 3;
        evalsUntilMajor_ = majorPeriod_;
        healT_ = 0.0f;
        forced_ = false;
        forcedState_ = RuinProcessingState::Intact;
        applyNeighborhood (0.0f, 0.0f, true);
    }

    /** After macros known, optionally bias initial state from AGE landscape. */
    void seedInitialFromAge (float age, float inst) noexcept
    {
        if (forced_)
            return;
        if (age >= 0.85f)
            enterState (RuinProcessingState::Weathered, age, inst, false);
        else if (age >= 0.55f && transitionRng_.nextFloat() < 0.55f)
            enterState (RuinProcessingState::Weathered, age, inst, false);
        applyNeighborhood (age, inst, true);
    }

    void setForcedState (bool on, RuinProcessingState s) noexcept
    {
        forced_ = on;
        forcedState_ = s;
        if (on)
        {
            enterState (s, 0.5f, 0.5f, /*countTransition*/ false);
            applyNeighborhood (0.5f, 0.5f, true);
        }
    }

    bool forced() const noexcept { return forced_; }

    /** Replay structural decisions 0..endEval inclusive (absolute PPQ rebuild). */
    void rebuildTo (int endEval, float age, float inst) noexcept
    {
        rebuildRng();
        state_ = RuinProcessingState::Intact;
        previousState_ = RuinProcessingState::Intact;
        damagePressure_ = 0.0f;
        recoveryPressure_ = 0.0f;
        severityAtEntry_ = 0.0f;
        dwellEvals_ = 0;
        minDwellEvals_ = minDwellFor (RuinProcessingState::Intact, inst);
        majorPeriod_ = rollMajorPeriod (inst);
        evalsUntilMajor_ = majorPeriod_;
        healT_ = 0.0f;
        applyNeighborhood (age, inst, true);
        const int end = std::max (0, endEval);
        for (int i = 0; i <= end; ++i)
            evaluateAt (i, age, inst);
    }

    void evaluateAt (int /*evalIndex*/, float age, float inst) noexcept
    {
        if (forced_)
        {
            state_ = forcedState_;
            applyNeighborhood (age, inst, false);
            return;
        }

        updatePressures (age, inst);
        ++dwellEvals_;

        if (state_ == RuinProcessingState::Recovering)
        {
            const float span = 2.0f + 4.0f * (1.0f - inst); // eval units
            healT_ = std::clamp (static_cast<float> (dwellEvals_) / span, 0.0f, 1.0f);
        }

        applyNeighborhood (age, inst, false);

        if (--evalsUntilMajor_ > 0)
            return;
        evalsUntilMajor_ = majorPeriod_;

        if (dwellEvals_ < minDwellEvals_)
            return;

        // Strong STAY — but high AGE reduces stay in mild states so deep ecology can breathe
        float pStay = 0.68f - 0.30f * inst - 0.08f * damagePressure_ + 0.10f * recoveryPressure_;
        if (state_ == RuinProcessingState::Intact || state_ == RuinProcessingState::Weathered)
        {
            pStay = std::max (pStay, 0.50f);
            if (age > 0.55f)
                pStay -= 0.35f * ((age - 0.55f) / 0.45f);
            if (state_ == RuinProcessingState::Intact && age > 0.75f)
                pStay = std::min (pStay, 0.40f);
        }
        if (state_ == RuinProcessingState::Ruined && recoveryPressure_ > 0.35f)
            pStay = std::min (pStay, 0.45f);
        pStay = std::clamp (pStay, 0.28f, 0.88f);

        if (transitionRng_.nextFloat() < pStay)
            return;

        const RuinProcessingState next = pickNext (age, inst);
        if (next == state_)
            return;
        enterState (next, age, inst, true);
        applyNeighborhood (age, inst, true);
    }

    RuinProcessingState state() const noexcept { return state_; }
    RuinProcessingState previousState() const noexcept { return previousState_; }
    float damagePressure() const noexcept { return damagePressure_; }
    float recoveryPressure() const noexcept { return recoveryPressure_; }
    float severityAtEntry() const noexcept { return severityAtEntry_; }
    float healT() const noexcept { return healT_; }
    const RuinProfileTargets& targets() const noexcept { return targets_; }

    static bool isLegalEdge (RuinProcessingState from, RuinProcessingState to) noexcept
    {
        switch (from)
        {
            case RuinProcessingState::Intact:
                return to == RuinProcessingState::Weathered;
            case RuinProcessingState::Weathered:
                return to == RuinProcessingState::Intact || to == RuinProcessingState::Fractured;
            case RuinProcessingState::Fractured:
                return to == RuinProcessingState::Weathered || to == RuinProcessingState::Ruined;
            case RuinProcessingState::Ruined:
                return to == RuinProcessingState::Recovering;
            case RuinProcessingState::Recovering:
                return to == RuinProcessingState::Intact || to == RuinProcessingState::Weathered;
            default:
                return false;
        }
    }

private:
    void rebuildRng() noexcept
    {
        transitionRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x5354524Eull); // STRN
        durationRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x4455524Eull);   // DURN
        profileRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x50524F46ull);    // PROF
        recoveryRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x52435652ull);   // RCVR
    }

    static int minDwellFor (RuinProcessingState s, float inst) noexcept
    {
        float base = 4.0f;
        switch (s)
        {
            case RuinProcessingState::Intact: base = 10.0f; break;    // ≥16 beats even at inst=1
            case RuinProcessingState::Weathered: base = 8.0f; break;  // ≥12 beats at inst=1
            case RuinProcessingState::Fractured: base = 4.0f; break;  // ≥8 beats at inst=1
            case RuinProcessingState::Ruined: base = 4.0f; break;
            case RuinProcessingState::Recovering: base = 4.0f; break;
            default: break;
        }
        const int m = static_cast<int> (std::floor (base * (0.45f + 0.55f * (1.0f - inst))));
        return std::max (2, m);
    }

    int rollMajorPeriod (float inst) noexcept
    {
        // 2–4 eval units → 8–16 beats
        const int span = 2 + static_cast<int> (durationRng_.nextFloat() * (1.0f + 2.0f * (1.0f - 0.5f * inst)));
        return std::clamp (span, 2, 4);
    }

    void updatePressures (float age, float inst) noexcept
    {
        if (state_ != RuinProcessingState::Intact)
            damagePressure_ += 0.006f * age; // AGE-driven; INSTABILITY does not deepen damage


        if (state_ == RuinProcessingState::Ruined)
            recoveryPressure_ += 0.012f + 0.020f * age;
        else if (state_ == RuinProcessingState::Fractured && dwellEvals_ > static_cast<int> (1.5f * minDwellEvals_))
            recoveryPressure_ += 0.006f;
        else if (state_ == RuinProcessingState::Recovering)
            recoveryPressure_ -= 0.08f;

        damagePressure_ = std::clamp (damagePressure_, 0.0f, 1.0f);
        recoveryPressure_ = std::clamp (recoveryPressure_, 0.0f, 1.0f);
    }

    static float eligibility (RuinProcessingState dest, float age) noexcept
    {
        auto ramp = [] (float x, float a, float b) {
            if (x <= a) return 0.0f;
            if (x >= b) return 1.0f;
            return (x - a) / (b - a);
        };
        switch (dest)
        {
            case RuinProcessingState::Intact:
                return std::clamp (1.0f - 0.75f * age, 0.15f, 1.0f);
            case RuinProcessingState::Weathered:
                return std::clamp (0.35f + 0.65f * (1.0f - std::abs (age - 0.4f)), 0.2f, 1.0f);
            case RuinProcessingState::Fractured:
                return ramp (age, 0.08f, 0.45f);
            case RuinProcessingState::Ruined:
                return ramp (age, 0.28f, 0.75f);
            case RuinProcessingState::Recovering:
                return std::clamp (0.2f + 0.8f * age, 0.15f, 1.0f);
            default:
                return 0.0f;
        }
    }

    RuinProcessingState pickNext (float age, float inst) noexcept
    {
        RuinProcessingState candidates[2];
        float weights[2];
        int n = 0;

        const float recoveryRoll = (state_ == RuinProcessingState::Recovering)
                                       ? recoveryRng_.nextFloat()
                                       : 0.0f;
        const float preferWeathered = (severityAtEntry_ > 0.65f || age > 0.55f) ? 0.72f : 0.35f;

        auto add = [&] (RuinProcessingState to, float base)
        {
            if (! isLegalEdge (state_, to))
                return;
            float w = base * eligibility (to, age);
            const bool deeper = static_cast<int> (to) > static_cast<int> (state_)
                                && to != RuinProcessingState::Recovering;
            if (deeper)
                w *= (1.0f + 0.35f * damagePressure_ + 0.55f * age);
            if (to == RuinProcessingState::Recovering)
                w *= (1.0f + 0.60f * recoveryPressure_);
            if (state_ == RuinProcessingState::Fractured && to == RuinProcessingState::Weathered)
                w *= (1.0f + 0.40f * recoveryPressure_);
            if (state_ == RuinProcessingState::Fractured && to == RuinProcessingState::Ruined)
                w *= (1.0f - 0.25f * recoveryPressure_);
            if (state_ == RuinProcessingState::Recovering)
            {
                if (to == RuinProcessingState::Weathered)
                    w *= (recoveryRoll < preferWeathered + 0.15f * inst) ? 1.4f : 0.7f;
                else
                    w *= (recoveryRoll >= preferWeathered) ? 1.3f : 0.75f;
            }
            if (w <= 1.0e-6f)
                return;
            if (n < 2)
            {
                candidates[n] = to;
                weights[n] = w;
                ++n;
            }
        };

        switch (state_)
        {
            case RuinProcessingState::Intact:
                add (RuinProcessingState::Weathered, 1.0f);
                break;
            case RuinProcessingState::Weathered:
                add (RuinProcessingState::Intact, 0.55f);
                add (RuinProcessingState::Fractured, 0.45f);
                break;
            case RuinProcessingState::Fractured:
                add (RuinProcessingState::Weathered, 0.50f);
                add (RuinProcessingState::Ruined, 0.50f);
                break;
            case RuinProcessingState::Ruined:
                add (RuinProcessingState::Recovering, 1.0f);
                break;
            case RuinProcessingState::Recovering:
                add (RuinProcessingState::Intact, 0.60f);
                add (RuinProcessingState::Weathered, 0.40f);
                break;
            default:
                break;
        }

        if (n == 0)
            return state_;
        if (n == 1)
            return candidates[0];

        const float sum = weights[0] + weights[1];
        const float r = transitionRng_.nextFloat() * sum;
        return (r < weights[0]) ? candidates[0] : candidates[1];
    }

    void enterState (RuinProcessingState next, float age, float inst, bool countTransition) noexcept
    {
        if (countTransition)
        {
            const bool deeper = static_cast<int> (next) > static_cast<int> (state_)
                                && next != RuinProcessingState::Recovering;
            if (deeper)
                damagePressure_ = std::clamp (damagePressure_ + 0.10f, 0.0f, 1.0f);
            else
                damagePressure_ *= 0.60f;

            if (next == RuinProcessingState::Recovering)
                recoveryPressure_ = std::max (recoveryPressure_, 0.25f);
            else if (deeper)
                recoveryPressure_ *= 0.40f;

            if (next == RuinProcessingState::Intact)
                damagePressure_ *= 0.25f;
        }

        previousState_ = state_;
        state_ = next;
        severityAtEntry_ = damagePressure_;
        dwellEvals_ = 0;
        healT_ = 0.0f;
        minDwellEvals_ = minDwellFor (state_, inst);
        majorPeriod_ = rollMajorPeriod (inst);
        evalsUntilMajor_ = majorPeriod_;
        (void) age;
    }

    void applyNeighborhood (float age, float inst, bool snapCenter) noexcept
    {
        // Center biases per state (neighborhoods, not presets)
        float cTone = 0.05f, cGrit = 0.05f, cWobble = 0.05f, cSmear = 0.05f, cFrac = 0.0f;
        float spread = 0.08f;

        switch (state_)
        {
            case RuinProcessingState::Intact:
                cTone = 0.05f; cGrit = 0.04f; cWobble = 0.06f; cSmear = 0.04f; cFrac = 0.0f;
                spread = 0.05f;
                break;
            case RuinProcessingState::Weathered:
                cTone = 0.35f; cGrit = 0.28f; cWobble = 0.30f; cSmear = 0.40f; cFrac = 0.0f;
                spread = 0.10f;
                break;
            case RuinProcessingState::Fractured:
                cTone = 0.55f; cGrit = 0.50f; cWobble = 0.55f; cSmear = 0.45f; cFrac = 0.65f;
                spread = 0.12f;
                break;
            case RuinProcessingState::Ruined:
                // Personality: smear + grit, not all-max
                cTone = 0.72f; cGrit = 0.68f; cWobble = 0.45f; cSmear = 0.78f; cFrac = 0.22f;
                spread = 0.10f;
                break;
            case RuinProcessingState::Recovering:
            {
                const float open = healT_;
                const float rem = (1.0f - healT_) * (0.15f + 0.25f * severityAtEntry_);
                cTone = (1.0f - open) * 0.65f + open * 0.12f;
                cGrit = (1.0f - open) * 0.55f + open * 0.10f;
                cWobble = ((1.0f - open) * 0.40f + open * 0.12f) * (0.35f + 0.25f * (1.0f - open));
                cSmear = (1.0f - open) * 0.70f + open * 0.15f;
                cFrac = rem * 0.5f;
                spread = 0.08f;
                break;
            }
            default:
                break;
        }

        // AGE reshapes landscape depth without becoming state index
        const float a = age * age * (3.0f - 2.0f * age);
        cTone = std::clamp (0.55f * cTone + 0.45f * a * (0.3f + 0.7f * cTone), 0.0f, 1.0f);
        cGrit = std::clamp (0.55f * cGrit + 0.45f * a * (0.3f + 0.7f * cGrit), 0.0f, 1.0f);
        cSmear = std::clamp (0.55f * cSmear + 0.45f * a * (0.3f + 0.7f * cSmear), 0.0f, 1.0f);
        cWobble = std::clamp (cWobble * (0.4f + 0.6f * inst), 0.0f, 1.0f);
        cFrac *= std::clamp (inst, 0.0f, 1.0f);

        if (snapCenter)
        {
            targets_.tone = cTone;
            targets_.grit = cGrit;
            targets_.wobble = cWobble;
            targets_.smear = cSmear;
            targets_.fractureAmount = cFrac;
        }
        else
        {
            // Small within-state wander (INSTABILITY depth), not Stage 1 soup
            const float d = 0.015f + 0.04f * inst;
            if (profileRng_.nextFloat() < (0.08f + 0.18f * inst))
            {
                const int which = static_cast<int> (profileRng_.nextFloat() * 4.0f) % 4;
                auto nudge = [&] (float& t, float center)
                {
                    t = std::clamp (t + profileRng_.nextFloat (-d, d),
                                    std::max (0.0f, center - spread),
                                    std::min (1.0f, center + spread));
                };
                if (which == 0) nudge (targets_.tone, cTone);
                else if (which == 1) nudge (targets_.grit, cGrit);
                else if (which == 2) nudge (targets_.wobble, cWobble);
                else nudge (targets_.smear, cSmear);
            }
            targets_.fractureAmount += (cFrac - targets_.fractureAmount) * 0.15f;
        }
    }

    uint64_t masterSeed_ = 2002;
    RuinProcessingState state_ = RuinProcessingState::Intact;
    RuinProcessingState previousState_ = RuinProcessingState::Intact;
    RuinProcessingState forcedState_ = RuinProcessingState::Intact;
    bool forced_ = false;
    float damagePressure_ = 0.0f;
    float recoveryPressure_ = 0.0f;
    float severityAtEntry_ = 0.0f;
    float healT_ = 0.0f;
    int dwellEvals_ = 0;
    int minDwellEvals_ = 4;
    int majorPeriod_ = 3;
    int evalsUntilMajor_ = 3;
    RuinProfileTargets targets_;
    pfl::generative::DeterministicRNG transitionRng_, durationRng_, profileRng_, recoveryRng_;
};

} // namespace pfl::dsp

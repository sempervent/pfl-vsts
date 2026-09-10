#pragma once

#include "RuinStateMachine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pfl::dsp
{

/** Bounded processing-history vector (not audio memory). */
struct WearState
{
    float spectral = 0.0f;   // filter darkening bias
    float nonlinear = 0.0f;  // grit / noise floor bias
    float temporal = 0.0f;   // smear / fracture residue bias

    void clamp() noexcept
    {
        spectral = std::clamp (spectral, 0.0f, 1.0f);
        nonlinear = std::clamp (nonlinear, 0.0f, 1.0f);
        temporal = std::clamp (temporal, 0.0f, 1.0f);
    }

    float mean() const noexcept { return (spectral + nonlinear + temporal) * (1.0f / 3.0f); }
};

/**
 * Accumulates / recovers WearState over musical time.
 * Does NOT reconstruct from absolute PPQ on seek (exposure is real, not fabricated).
 */
class WearAccumulator
{
public:
    void resetFresh() noexcept
    {
        wear_ = {};
        scarFloor_ = {};
        paused_ = false;
    }

    void setPaused (bool p) noexcept { paused_ = p; }
    bool paused() const noexcept { return paused_; }

    WearState wear() const noexcept { return wear_; }
    void setWear (WearState w) noexcept
    {
        wear_ = w;
        wear_.clamp();
    }

    WearState scarFloor() const noexcept { return scarFloor_; }
    void setScarFloor (WearState f) noexcept
    {
        scarFloor_ = f;
        scarFloor_.clamp();
    }

    void setWearAndFloor (WearState w, WearState f) noexcept
    {
        setWear (w);
        setScarFloor (f);
        // Floors cannot exceed current wear
        scarFloor_.spectral = std::min (scarFloor_.spectral, wear_.spectral);
        scarFloor_.nonlinear = std::min (scarFloor_.nonlinear, wear_.nonlinear);
        scarFloor_.temporal = std::min (scarFloor_.temporal, wear_.temporal);
    }

    /**
     * Integrate one musical-time slice (beats).
     * Call only while transport playing and not bypassed.
     */
    void accumulate (float deltaBeats,
                     RuinProcessingState state,
                     float age,
                     float /*inst*/,
                     float mix,
                     float inputActivity01,
                     float recoveryPressure,
                     float healT) noexcept
    {
        if (paused_ || deltaBeats <= 0.0f)
            return;

        const float a = age * age * (3.0f - 2.0f * age);
        // Near-silence must not meaningfully age a fresh processor.
        const float activity = inputActivity01 < 0.015f
                                   ? 0.0f
                                   : std::clamp (inputActivity01, 0.0f, 1.0f);
        // MIX continues wear (policy A): MIX is blend, not process enable.
        (void) mix;
        const float stress = a * activity;

        // Per-beat rates (scaled so ~128–256 beats of RUINED@AGE1 is significant)
        float dS = 0, dN = 0, dT = 0;
        switch (state)
        {
            case RuinProcessingState::Intact:
                break;
            case RuinProcessingState::Weathered:
                dS = 0.0016f; dN = 0.0018f; dT = 0.0014f;
                break;
            case RuinProcessingState::Fractured:
                dS = 0.0028f; dN = 0.0034f; dT = 0.0048f;
                break;
            case RuinProcessingState::Ruined:
                dS = 0.0042f; dN = 0.0048f; dT = 0.0054f;
                break;
            case RuinProcessingState::Recovering:
                recover (deltaBeats, age, recoveryPressure, healT);
                return;
            default:
                break;
        }

        if (state == RuinProcessingState::Intact)
        {
            // Slow heal in Intact
            recover (deltaBeats * 0.35f, age, recoveryPressure, 0.0f);
            return;
        }

        wear_.spectral = std::min (1.0f, wear_.spectral + dS * stress * deltaBeats);
        wear_.nonlinear = std::min (1.0f, wear_.nonlinear + dN * stress * deltaBeats);
        wear_.temporal = std::min (1.0f, wear_.temporal + dT * stress * deltaBeats);

        // Raise scar floors on severe exposure
        if (state == RuinProcessingState::Ruined || state == RuinProcessingState::Fractured)
        {
            scarFloor_.spectral = std::max (scarFloor_.spectral, wear_.spectral * 0.55f);
            scarFloor_.nonlinear = std::max (scarFloor_.nonlinear, wear_.nonlinear * 0.55f);
            scarFloor_.temporal = std::max (scarFloor_.temporal, wear_.temporal * 0.60f);
        }
        wear_.clamp();
        scarFloor_.clamp();
    }

    /**
     * Apply wear biases to profile scalars.
     * Intact is hard-capped so scarred INTACT stays recognizably Intact (not Weathered).
     * Other states receive bounded offsets without clipping Stage 2 centers.
     */
    void applyToProfile (RuinProcessingState state,
                         float& tone, float& grit, float& smear, float& fractureAmount) const noexcept
    {
        float depth = 1.0f;
        switch (state)
        {
            case RuinProcessingState::Intact: depth = 0.55f; break;
            case RuinProcessingState::Weathered: depth = 0.75f; break;
            case RuinProcessingState::Fractured: depth = 0.90f; break;
            case RuinProcessingState::Ruined: depth = 1.0f; break;
            case RuinProcessingState::Recovering: depth = 0.55f; break;
            default: break;
        }

        tone = std::clamp (tone + wear_.spectral * 0.20f * depth, 0.0f, 1.0f);
        grit = std::clamp (grit + wear_.nonlinear * 0.16f * depth, 0.0f, 1.0f);
        smear = std::clamp (smear + wear_.temporal * 0.18f * depth, 0.0f, 1.0f);
        fractureAmount = std::clamp (fractureAmount + wear_.temporal * 0.10f * depth, 0.0f, 1.0f);

        if (state == RuinProcessingState::Intact)
        {
            tone = std::min (tone, 0.18f);
            grit = std::min (grit, 0.14f);
            smear = std::min (smear, 0.14f);
            fractureAmount = std::min (fractureAmount, 0.06f);
        }
    }

private:
    void recover (float deltaBeats, float age, float recoveryPressure, float healT) noexcept
    {
        const float base = 0.0032f * (1.0f - 0.45f * age) * (1.0f + 0.5f * recoveryPressure + 0.6f * healT);
        auto step = [&] (float& v, float floor)
        {
            const float target = floor;
            v -= base * deltaBeats * std::max (0.0f, v - target);
            v = std::max (v, target);
        };
        step (wear_.spectral, scarFloor_.spectral);
        step (wear_.nonlinear, scarFloor_.nonlinear);
        step (wear_.temporal, scarFloor_.temporal);

        // Very slow scar-floor bleed only when mostly healed environment
        if (age < 0.25f)
        {
            const float bleed = 0.0004f * (1.0f - age) * deltaBeats;
            scarFloor_.spectral = std::max (0.0f, scarFloor_.spectral - bleed);
            scarFloor_.nonlinear = std::max (0.0f, scarFloor_.nonlinear - bleed);
            scarFloor_.temporal = std::max (0.0f, scarFloor_.temporal - bleed);
        }
        wear_.clamp();
        scarFloor_.clamp();
    }

    WearState wear_;
    WearState scarFloor_;
    bool paused_ = false;
};

} // namespace pfl::dsp

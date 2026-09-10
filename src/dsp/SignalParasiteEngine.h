#pragma once

#include "DCBlocker.h"
#include "Filter.h"
#include "ParamSmoother.h"
#include "SafetyLimiter.h"
#include "Saturator.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace pfl::dsp
{

// ============================================================================
// Stage 1 event model
// ============================================================================

enum class StimulusKind : uint8_t
{
    Attack = 0,
    Shift = 1
};

/** One detected input event. Sample index is absolute → partition stable. */
struct StimulusEvent
{
    int64_t sampleIndex = 0;
    double ppq = 0.0;
    StimulusKind kind = StimulusKind::Attack;
    float strength = 0.0f;
    float energy = 0.0f;
    float brightness = 0.5f;
    float change = 0.0f;
    float balance = 0.0f;
};

enum class ParasiteSuppressReason : uint8_t
{
    None = 0,
    Accept,
    Weak,
    Stale,
    VoiceBusy,
    RecentResponse,
    Hunger,
    SourceBusy,
    Relationship,
    Count
};

inline const char* parasiteSuppressName (ParasiteSuppressReason r) noexcept
{
    switch (r)
    {
        case ParasiteSuppressReason::None: return "NONE";
        case ParasiteSuppressReason::Accept: return "ACCEPT";
        case ParasiteSuppressReason::Weak: return "WEAK";
        case ParasiteSuppressReason::Stale: return "STALE";
        case ParasiteSuppressReason::VoiceBusy: return "VOICE_BUSY";
        case ParasiteSuppressReason::RecentResponse: return "RECENT_RESPONSE";
        case ParasiteSuppressReason::Hunger: return "HUNGER";
        case ParasiteSuppressReason::SourceBusy: return "SOURCE_BUSY";
        case ParasiteSuppressReason::Relationship: return "RELATIONSHIP";
        default: return "?";
    }
}

/** Published continuous listening state. Observe-only; never fed from wet. */
struct ParasiteFeatureFrame
{
    float energy = 0.0f;     // amplitude of fast power envelope
    float attack = 0.0f;     // max(0, envFast/envSlow - 1), smoothed
    float bright = 0.5f;     // HP / (HP + LP) power ratio around the split
    float change = 0.0f;     // level or brightness excursion vs long baseline
    float balance = 0.0f;    // -1 = L, +1 = R
    float activeFrac = 0.0f; // fraction of recent time the activity gate is open
    float fill01 = 0.0f;     // how continuously filled the source is (voids ↔ wall)
    int active = 0;          // hysteresis gate
    int rising = 0;          // fast envelope is currently climbing
};

struct ParasiteResponseEvent
{
    int64_t onsetSample = 0;
    double onsetBeat = 0.0;
    int delaySlot = 0;
    int durSlot = 0;
    float durationBeats = 0.0f;
    float strength = 0.0f;
    float brightness = 0.5f;
    float pan = 0.0f;
    StimulusKind kind = StimulusKind::Attack;
    uint8_t state = 0; // ParasiteRelationship the answer was issued from
};

// ============================================================================
// FeatureExtractor — time domain only (no FFT, no pitch, no AGC)
// ============================================================================

class ParasiteFeatureExtractor
{
public:
    static constexpr float kEnergyFloor = 1.0e-8f;
    static constexpr float kAmpFloor = 1.0e-4f;
    static constexpr float kBrightSplitHz = 800.0f;
    static constexpr float kOpenThresh = 2.0e-3f;
    static constexpr float kCloseThresh = 8.0e-4f;

    void prepare (double sampleRate) noexcept
    {
        sr_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        aFastUp_ = alphaTau (0.008);
        aFastDn_ = alphaTau (0.030);
        aSlow_ = alphaTau (0.200);
        aAttUp_ = alphaTau (0.015);
        aAttDn_ = alphaTau (0.080);
        aBright_ = alphaTau (0.030);
        aBrightSlow_ = alphaTau (0.400);
        aMed_ = alphaTau (0.060);
        aLong_ = alphaTau (0.800);
        aChgUp_ = alphaTau (0.025);
        aChgDn_ = alphaTau (0.200);
        aActive_ = alphaTau (0.400);
        aPeak_ = alphaTau (2.000);
        aBal_ = alphaTau (0.050);
        aLp_ = 1.0f - std::exp (-2.0f * 3.14159265f * kBrightSplitHz / static_cast<float> (sr_));
        minOpenN_ = msSamples (20.0, sr_);
        minGapN_ = msSamples (40.0, sr_);
        // `attack` and `change` are ratios against 200 ms and 400–800 ms
        // baselines, so they are meaningless until those baselines converge.
        // Publishing 0 until then is what stops a mid-insert, a transport start
        // or a seek into already-flowing audio from looking like an onset.
        // Audio that arrives *after* a reset still lands a real onset, because
        // the baselines converge on the silence that preceded it.
        attackSettleN_ = msSamples (600.0, sr_);
        changeSettleN_ = msSamples (2500.0, sr_);
        // `fill01` divides by a 2 s peak follower that starts *at* the signal,
        // so it reads a hard 1.0 for the first seconds of any material at all.
        // Publishing 0 until the follower has had a couple of time constants is
        // what stops a fresh insert from looking like a wall of sound.
        fillSettleN_ = msSamples (4000.0, sr_);
        settleN_ = std::max (changeSettleN_, fillSettleN_);
        reset();
    }

    void reset() noexcept
    {
        primed_ = false;
        envFast1_ = envFast_ = envSlow_ = 0.0f;
        attackSm_ = 0.0f;
        lastEnvFast_ = 0.0f;
        lpZ_ = 0.0f;
        envLp_ = envHp_ = 0.0f;
        ampMed_ = ampLong_ = 0.0f;
        changeSm_ = 0.0f;
        peakSlow_ = 0.0f;
        fillSm_ = 0.0f;
        eL_ = eR_ = 0.0f;
        active_ = 0;
        sinceEdge_ = 0;
        sinceReset_ = 0;
        frame_ = {};
        brightSlow_ = 0.5f;
    }

    void processSample (float l, float r) noexcept
    {
        const float mid = 0.5f * (l + r);
        const float e = mid * mid;
        const float am = std::abs (mid);

        // First sample after reset primes the baselines so a mid-insert, a
        // transport start, or a seek can never look like a transient.
        if (! primed_)
        {
            primed_ = true;
            envFast1_ = envFast_ = envSlow_ = e;
            lastEnvFast_ = e;
            ampMed_ = ampLong_ = am;
            lpZ_ = mid;
            envLp_ = e;
            envHp_ = 0.0f;
            eL_ = l * l;
            eR_ = r * r;
            frame_.energy = std::sqrt (std::max (envFast_, kEnergyFloor));
            frame_.attack = 0.0f;
            frame_.change = 0.0f;
            frame_.bright = envLp_ > kEnergyFloor ? 0.0f : 0.5f;
            brightSlow_ = frame_.bright;
            frame_.balance = 0.0f;
            frame_.active = 0;
            frame_.rising = 0;
            frame_.activeFrac = 0.0f;
            peakSlow_ = frame_.energy;
            fillSm_ = 0.0f;
            frame_.fill01 = 0.0f;
            return;
        }

        // Asymmetric fast envelope: 8 ms rise keeps transient response, 30 ms fall
        // stops cycle-rate ripple on tonal material from reading as onsets. Two
        // cascaded stages, because a single pole at 8 ms still passes enough
        // ripple from a 100–200 Hz fundamental for the ratio below to read it as
        // an onset on every cycle.
        envFast1_ = flush (envFast1_ + (e > envFast1_ ? aFastUp_ : aFastDn_) * (e - envFast1_));
        envFast_ = flush (envFast_
                          + (envFast1_ > envFast_ ? aFastUp_ : aFastDn_) * (envFast1_ - envFast_));
        envSlow_ = flush (envSlow_ + aSlow_ * (e - envSlow_));
        const float energy = std::sqrt (std::max (envFast_, kEnergyFloor));

        const float ratio = envFast_ / std::max (envSlow_, kEnergyFloor);
        const float attackRaw = std::clamp (ratio - 1.0f, 0.0f, 8.0f);
        const float aAtt = attackRaw > attackSm_ ? aAttUp_ : aAttDn_;
        attackSm_ = flush (attackSm_ + aAtt * (attackRaw - attackSm_));

        // An onset is a rise. Without this, the long tail of a hit keeps `attack`
        // pinned high while the 200 ms baseline crawls up to meet it, and the
        // decay of one hit reads as a run of new ones.
        frame_.rising = envFast_ > lastEnvFast_ * 1.0005f ? 1 : 0;
        lastEnvFast_ = envFast_;

        lpZ_ = flush (lpZ_ + aLp_ * (mid - lpZ_));
        const float hp = mid - lpZ_;
        envLp_ = flush (envLp_ + aBright_ * (lpZ_ * lpZ_ - envLp_));
        envHp_ = flush (envHp_ + aBright_ * (hp * hp - envHp_));
        const float bandSum = envHp_ + envLp_;
        if (bandSum > kEnergyFloor)
            frame_.bright = std::clamp (envHp_ / bandSum, 0.0f, 1.0f);
        brightSlow_ += aBrightSlow_ * (frame_.bright - brightSlow_);

        ampMed_ = flush (ampMed_ + aMed_ * (am - ampMed_));
        ampLong_ = flush (ampLong_ + aLong_ * (am - ampLong_));
        const float levelChange = std::abs (ampMed_ - ampLong_) / std::max (ampLong_, kAmpFloor);
        const float brightChange = std::abs (frame_.bright - brightSlow_) * 2.5f;
        const float changeRaw = std::min (1.5f, std::max (levelChange, brightChange));
        const float aChg = changeRaw > changeSm_ ? aChgUp_ : aChgDn_;
        changeSm_ = flush (changeSm_ + aChg * (changeRaw - changeSm_));

        ++sinceEdge_;
        if (active_ == 0)
        {
            if (energy > kOpenThresh && sinceEdge_ >= minGapN_)
            {
                active_ = 1;
                sinceEdge_ = 0;
            }
        }
        else if (energy < kCloseThresh && sinceEdge_ >= minOpenN_)
        {
            active_ = 0;
            sinceEdge_ = 0;
        }
        frame_.activeFrac += aActive_ * (static_cast<float> (active_) - frame_.activeFrac);

        // "Wall of sound" probe: sparse material spends most of its time far below
        // its own slow peak; a continuous bed sits right at it. Bounded and floored.
        if (energy > peakSlow_)
            peakSlow_ = energy;
        else
            peakSlow_ = flush (peakSlow_ + aPeak_ * (energy - peakSlow_));
        const float fillRaw = std::min (1.0f, energy / std::max (peakSlow_, 1.0e-3f) * 1.25f);
        fillSm_ += aActive_ * (fillRaw - fillSm_);

        eL_ = flush (eL_ + aBal_ * (l * l - eL_));
        eR_ = flush (eR_ + aBal_ * (r * r - eR_));
        const float stereoSum = eL_ + eR_;
        frame_.balance = stereoSum > kEnergyFloor
                             ? std::clamp ((eR_ - eL_) / stereoSum, -1.0f, 1.0f)
                             : 0.0f;

        if (sinceReset_ < settleN_)
            ++sinceReset_;

        frame_.energy = energy;
        frame_.active = active_;
        frame_.attack = sinceReset_ >= attackSettleN_ ? attackSm_ : 0.0f;
        frame_.change = sinceReset_ >= changeSettleN_ ? changeSm_ : 0.0f;
        frame_.fill01 = sinceReset_ >= fillSettleN_ ? fillSm_ : 0.0f;
    }

    const ParasiteFeatureFrame& frame() const noexcept { return frame_; }

    static int msSamples (double ms, double sr) noexcept
    {
        return std::max (1, static_cast<int> (std::lround (ms * 0.001 * sr)));
    }

private:
    float alphaTau (double tauSec) const noexcept
    {
        return 1.0f - std::exp (-1.0f / static_cast<float> (std::max (1.0e-6, tauSec) * sr_));
    }

    static float flush (float y) noexcept
    {
        return std::abs (y) < 1.0e-20f ? 0.0f : y;
    }

    double sr_ = 44100.0;
    float aFastUp_ = 0.1f, aFastDn_ = 0.03f, aSlow_ = 0.01f;
    float aAttUp_ = 0.1f, aAttDn_ = 0.01f;
    float aBright_ = 0.1f, aBrightSlow_ = 0.01f;
    float aMed_ = 0.1f, aLong_ = 0.01f;
    float aChgUp_ = 0.1f, aChgDn_ = 0.01f;
    float aActive_ = 0.01f, aPeak_ = 0.001f, aBal_ = 0.05f, aLp_ = 0.1f;
    int minOpenN_ = 1, minGapN_ = 1, attackSettleN_ = 1, changeSettleN_ = 1;
    int fillSettleN_ = 1, settleN_ = 1;

    bool primed_ = false;
    float envFast1_ = 0.0f, envFast_ = 0.0f, envSlow_ = 0.0f;
    float attackSm_ = 0.0f, lastEnvFast_ = 0.0f;
    float lpZ_ = 0.0f, envLp_ = 0.0f, envHp_ = 0.0f;
    float brightSlow_ = 0.5f;
    float ampMed_ = 0.0f, ampLong_ = 0.0f, changeSm_ = 0.0f;
    float peakSlow_ = 0.0f, fillSm_ = 0.0f;
    float eL_ = 0.0f, eR_ = 0.0f;
    int active_ = 0;
    int sinceEdge_ = 0;
    int sinceReset_ = 0;
    ParasiteFeatureFrame frame_ {};
};

// ============================================================================
// StimulusDetector — SENSITIVITY owns thresholds + cooldowns, nothing else
// ============================================================================

class ParasiteStimulusDetector
{
public:
    static constexpr int kQueueCap = 8;
    static constexpr double kStaleBeats = 2.0;
    static constexpr float kAbsFloor = 1.5e-3f;
    static constexpr float kMinStrength = 0.12f;

    void prepare (double sampleRate) noexcept
    {
        sr_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        setSensitivity (sensitivity_);
        reset();
    }

    void reset() noexcept
    {
        clearQueue();
        armed_ = false;
        attackArmed_ = shiftArmed_ = true;
        armEnergy_ = armChange_ = 0.0f;
        attackHoldN_ = shiftHoldN_ = 0;
        attackRefractLeft_ = shiftRefractLeft_ = shiftLockLeft_ = 0;
    }

    void clearQueue() noexcept
    {
        head_ = tail_ = size_ = 0;
    }

    /** SENSITIVITY → detection thresholds and cooldown lengths only. */
    void setSensitivity (float s01) noexcept
    {
        sensitivity_ = std::clamp (s01, 0.0f, 1.0f);
        const float s = sensitivity_;
        eOpen_ = std::max (lerp3 (s, 0.120f, 0.055f, 0.022f), 2.0f * kAbsFloor);
        eClose_ = eOpen_ * 0.55f;
        attOn_ = std::max (lerp3 (s, 0.42f, 0.22f, 0.10f), 0.06f);
        shOn_ = std::max (lerp3 (s, 0.35f, 0.18f, 0.08f), 0.06f);
        debounceAttN_ = msN (std::max (1.0f, lerp3 (s, 8.0f, 4.0f, 2.0f)));
        debounceShiftN_ = msN (std::max (8.0f, lerp3 (s, 40.0f, 25.0f, 15.0f)));
        refractAttN_ = msN (std::max (25.0f, lerp3 (s, 120.0f, 70.0f, 35.0f)));
        refractShiftN_ = msN (std::max (80.0f, lerp3 (s, 400.0f, 220.0f, 100.0f)));
        shiftLockN_ = msN (std::max (20.0f, lerp3 (s, 80.0f, 50.0f, 30.0f)));
    }

    void setStaleSamples (int64_t n) noexcept { staleSamples_ = std::max<int64_t> (1, n); }

    /** Feed one feature sample. Returns true if an event was queued. */
    bool processFeature (const ParasiteFeatureFrame& f, int64_t absSample, double ppq) noexcept
    {
        if (attackRefractLeft_ > 0) --attackRefractLeft_;
        if (shiftRefractLeft_ > 0) --shiftRefractLeft_;
        if (shiftLockLeft_ > 0) --shiftLockLeft_;

        if (f.energy < kAbsFloor)
        {
            armed_ = false;
            attackArmed_ = shiftArmed_ = true;
            armEnergy_ = armChange_ = 0.0f;
            attackHoldN_ = shiftHoldN_ = 0;
            return false;
        }

        if (! armed_)
        {
            if (f.energy <= eOpen_)
                return false;
            armed_ = true;
        }
        else if (f.energy < eClose_)
        {
            armed_ = false;
            attackHoldN_ = shiftHoldN_ = 0;
            return false;
        }

        bool fired = false;

        // One hit is one event. The tail of a hit holds the attack ratio high
        // while the 200 ms baseline climbs to meet it, so a refractory window
        // alone lets one hit read as a run of fresh onsets. Re-arming is
        // anchored to the envelope rather than to the threshold: either the hit
        // we fired on has decayed by ~4 dB, or a louder one has arrived. Both
        // are independent of SENSITIVITY, which is what keeps SENSITIVITY the
        // only thing setting detection density.
        if (! attackArmed_
            && (f.energy < armEnergy_ * 0.6f
                || (f.energy > armEnergy_ * 1.4f && f.rising != 0)))
            attackArmed_ = true;

        if (f.attack >= attOn_ && attackArmed_ && f.rising != 0)
        {
            ++attackHoldN_;
            const bool confirmed = attackHoldN_ >= debounceAttN_ || f.attack >= attOn_ * 2.5f;
            if (confirmed)
            {
                if (attackRefractLeft_ == 0)
                {
                    const float strength = std::clamp (f.attack / (attOn_ * 3.0f), 0.0f, 1.0f);
                    if (strength >= kMinStrength)
                    {
                        push (makeEvent (f, absSample, ppq, StimulusKind::Attack, strength));
                        ++attackCount_;
                        fired = true;
                    }
                    attackRefractLeft_ = refractAttN_;
                    attackArmed_ = false;
                    armEnergy_ = f.energy;
                }
                attackHoldN_ = 0;
                shiftHoldN_ = 0;
            }
        }
        else if (f.attack < attOn_)
        {
            attackHoldN_ = 0;
        }

        if (! fired)
        {
            // Same re-arm discipline as ATTACK, anchored to the excursion we
            // fired on: a plateau is not a new shift, but an excursion that
            // keeps deepening is.
            if (! shiftArmed_
                && (f.change < armChange_ * 0.5f || f.change > armChange_ * 1.5f))
                shiftArmed_ = true;

            // "Filter sweep on a hit" is the only SHIFT allowed inside ATTACK refractory.
            const float need = attackRefractLeft_ > 0 ? shOn_ * 1.4f : shOn_;
            if (f.change >= need && f.active != 0 && shiftLockLeft_ == 0 && shiftArmed_)
            {
                ++shiftHoldN_;
                if (shiftHoldN_ >= debounceShiftN_)
                {
                    if (shiftRefractLeft_ == 0)
                    {
                        const float strength = std::clamp (f.change / (shOn_ * 2.5f), 0.0f, 1.0f);
                        if (strength >= kMinStrength)
                        {
                            push (makeEvent (f, absSample, ppq, StimulusKind::Shift, strength));
                            ++shiftCount_;
                            fired = true;
                        }
                        shiftRefractLeft_ = refractShiftN_;
                        shiftLockLeft_ = shiftLockN_;
                        shiftArmed_ = false;
                        armChange_ = f.change;
                    }
                    shiftHoldN_ = 0;
                }
            }
            else if (f.change < shOn_ * 0.5f)
            {
                shiftHoldN_ = 0;
            }
        }

        return fired;
    }

    /** Front event after stale expiry. */
    bool peek (StimulusEvent& out, int64_t absSample) noexcept
    {
        while (size_ > 0)
        {
            const auto& e = ring_[static_cast<size_t> (head_)];
            if (absSample - e.sampleIndex > staleSamples_)
            {
                dropFront();
                ++staleDrops_;
                continue;
            }
            out = e;
            return true;
        }
        return false;
    }

    void dropFront() noexcept
    {
        if (size_ <= 0)
            return;
        head_ = (head_ + 1) % kQueueCap;
        --size_;
    }

    int size() const noexcept { return size_; }
    const StimulusEvent& lastEvent() const noexcept { return lastEvent_; }
    uint32_t attackCount() const noexcept { return attackCount_; }
    uint32_t shiftCount() const noexcept { return shiftCount_; }
    uint32_t overflowDrops() const noexcept { return overflowDrops_; }
    uint32_t staleDrops() const noexcept { return staleDrops_; }
    float eOpen() const noexcept { return eOpen_; }

    void clearCounters() noexcept
    {
        attackCount_ = shiftCount_ = overflowDrops_ = staleDrops_ = 0;
    }

private:
    static float lerp3 (float s, float at02, float at05, float at09) noexcept
    {
        if (s < 0.5f)
            return at02 + (at05 - at02) * (s - 0.2f) / 0.3f;
        return at05 + (at09 - at05) * (s - 0.5f) / 0.4f;
    }

    int msN (float ms) const noexcept
    {
        return ParasiteFeatureExtractor::msSamples (static_cast<double> (ms), sr_);
    }

    static StimulusEvent makeEvent (const ParasiteFeatureFrame& f, int64_t absSample, double ppq,
                                    StimulusKind kind, float strength) noexcept
    {
        StimulusEvent e;
        e.sampleIndex = absSample;
        e.ppq = ppq;
        e.kind = kind;
        e.strength = strength;
        e.energy = f.energy;
        e.brightness = f.bright;
        e.change = f.change;
        e.balance = f.balance;
        return e;
    }

    /** Fixed capacity; overflow drops the oldest — freshest cues win. */
    void push (const StimulusEvent& e) noexcept
    {
        if (size_ >= kQueueCap)
        {
            dropFront();
            ++overflowDrops_;
        }
        ring_[static_cast<size_t> (tail_)] = e;
        tail_ = (tail_ + 1) % kQueueCap;
        ++size_;
        lastEvent_ = e;
    }

    double sr_ = 44100.0;
    float sensitivity_ = 0.5f;
    float eOpen_ = 0.055f, eClose_ = 0.030f, attOn_ = 0.22f, shOn_ = 0.18f;
    int debounceAttN_ = 1, debounceShiftN_ = 1;
    int refractAttN_ = 1, refractShiftN_ = 1, shiftLockN_ = 1;
    int64_t staleSamples_ = 48000;

    bool armed_ = false;
    bool attackArmed_ = true;
    bool shiftArmed_ = true;
    float armEnergy_ = 0.0f, armChange_ = 0.0f;
    int attackHoldN_ = 0, shiftHoldN_ = 0;
    int attackRefractLeft_ = 0, shiftRefractLeft_ = 0, shiftLockLeft_ = 0;

    std::array<StimulusEvent, kQueueCap> ring_ {};
    StimulusEvent lastEvent_ {};
    int head_ = 0, tail_ = 0, size_ = 0;
    uint32_t attackCount_ = 0, shiftCount_ = 0, overflowDrops_ = 0, staleDrops_ = 0;
};

// ============================================================================
// ParasiteDNA — response manners; MUTATION evolves this, HUNGER never does
// ============================================================================

enum class ParasiteMutOp : uint8_t
{
    Stay = 0,
    NudgeDelay,
    NudgeDur,
    TiltGap,
    TiltEcho,
    TiltColour,
    StaleWindow,
    Count
};

inline const char* parasiteMutOpName (ParasiteMutOp op) noexcept
{
    switch (op)
    {
        case ParasiteMutOp::Stay: return "STAY";
        case ParasiteMutOp::NudgeDelay: return "NUDGE_DELAY";
        case ParasiteMutOp::NudgeDur: return "NUDGE_DUR";
        case ParasiteMutOp::TiltGap: return "TILT_GAP";
        case ParasiteMutOp::TiltEcho: return "TILT_ECHO";
        case ParasiteMutOp::TiltColour: return "TILT_COLOUR";
        case ParasiteMutOp::StaleWindow: return "STALE_WINDOW";
        default: return "?";
    }
}

struct ParasiteDNA
{
    static constexpr int kDelaySlots = 6;
    static constexpr int kDurSlots = 5;

    uint32_t generation = 0;
    int birthBar = 0;
    int lifespanBars = 12;
    uint8_t lastOp = 0;

    std::array<float, kDelaySlots> delayW { 0.10f, 0.14f, 0.24f, 0.26f, 0.17f, 0.09f };
    std::array<float, kDurSlots> durW { 0.14f, 0.28f, 0.30f, 0.18f, 0.10f };

    float gapPreference = 0.5f;
    float echoBias = 0.4f;
    float brightnessBias = 0.0f;
    float energyBias = 0.0f;
    float stereoMode = 0.2f;
    float stereoFixed = 0.0f;
    float minStimulusStrength = 0.25f;
    float staleBeats = 1.5f;

    // Voice colour hooks (consumed at note-on only)
    float chirpSign = 1.0f;
    float resBias = 0.5f;
    float durBias = 0.5f;
    float panBias = 0.0f;
};

/** Beats between accepted responses. HUNGER 0 → 6 beats, HUNGER 1 → 0.75. */
inline float parasiteMinGapBeats (float hunger) noexcept
{
    const float h = std::clamp (hunger, 0.0f, 1.0f);
    return 6.0f + (0.75f - 6.0f) * std::pow (h, 0.45f);
}

/** Per-stimulus accept probability. Hard capped so HUNGER 1 still leaves space. */
inline float parasiteAcceptProbability (float hunger, float beatsSinceResponse) noexcept
{
    const float h = std::clamp (hunger, 0.0f, 1.0f);
    if (h < 1.0e-4f)
        return 0.0f;
    const float base = std::min (0.62f, 0.04f + 0.55f * std::pow (h, 0.95f));
    const float minGap = parasiteMinGapBeats (h);
    const float t = std::clamp ((beatsSinceResponse - minGap) / 4.0f, 0.0f, 1.0f);
    const float silenceBoost = t * t * (3.0f - 2.0f * t);
    return std::clamp (base * (0.55f + 0.90f * silenceBoost), 0.0f, 0.62f);
}

/** MUTATION 0 freezes evolution; 1 → one MutOp every 4 bars. */
inline int parasiteLifespanBars (float mutation) noexcept
{
    if (mutation <= 1.0e-4f)
        return 1000000;
    const float m = std::clamp (mutation, 0.0f, 1.0f);
    return std::clamp (static_cast<int> (std::lround (16.0f + (4.0f - 16.0f) * m)), 4, 16);
}

// ============================================================================
// Stage 2 relationship — recent stimulus history, never audio memory
// ============================================================================

/**
 * How the parasite currently relates to whatever is feeding it.
 *
 * LURKING    — present and listening, answers sparingly and late.
 * ATTACHED   — it has decided this source is worth following.
 * ANSWERING  — an exchange is going: it answers sooner, a touch louder.
 * WITHDRAWN  — it has backed off, from a wall of sound or from its own fatigue.
 */
enum class ParasiteRelationship : uint8_t
{
    Lurking = 0,
    Attached,
    Answering,
    Withdrawn,
    Count
};

inline const char* parasiteRelationshipName (ParasiteRelationship s) noexcept
{
    switch (s)
    {
        case ParasiteRelationship::Lurking: return "LURKING";
        case ParasiteRelationship::Attached: return "ATTACHED";
        case ParasiteRelationship::Answering: return "ANSWERING";
        case ParasiteRelationship::Withdrawn: return "WITHDRAWN";
        default: return "?";
    }
}

/**
 * One remembered stimulus: when it arrived, how strong and how bright it was,
 * which kind it was, and whether the parasite answered it. There is no PCM
 * here. Stage 2 remembers *that* things happened, not what they sounded like.
 */
struct ParasiteStimulusMemo
{
    int64_t absSample = 0; // identity — matches StimulusEvent::sampleIndex
    int64_t relSample = 0; // relationship clock — pauses with the transport
    float strength = 0.0f;
    float brightness = 0.5f;
    StimulusKind kind = StimulusKind::Attack;
    bool answered = false;
    bool exchange = false; // arrived soon after something the parasite answered
};

struct ParasiteHistorySummary
{
    int offered = 0;
    int answered = 0;
    int exchanges = 0;
    float ratePerBeat = 0.0f;
    float interest = 0.0f;
    float gapScore = 1.0f;
    float success = 0.0f;
    float beatsSinceLast = 1.0e6f;
};

/** Fixed-capacity ring of stimulus descriptors. Never allocates. */
class RecentStimulusHistory
{
public:
    static constexpr int kCapacity = 16;
    static constexpr float kExchangeBeats = 2.0f;

    void reset() noexcept
    {
        head_ = 0;
        size_ = 0;
        lastAnsweredRel_ = -1;
    }

    void push (const StimulusEvent& e, int64_t relSample, double samplesPerBeat) noexcept
    {
        ParasiteStimulusMemo m;
        m.absSample = e.sampleIndex;
        m.relSample = relSample;
        m.strength = e.strength;
        m.brightness = e.brightness;
        m.kind = e.kind;
        if (lastAnsweredRel_ >= 0)
        {
            const double gapBeats = static_cast<double> (relSample - lastAnsweredRel_)
                                    / std::max (1.0, samplesPerBeat);
            m.exchange = gapBeats <= static_cast<double> (kExchangeBeats);
        }
        ring_[static_cast<size_t> (head_)] = m;
        head_ = (head_ + 1) % kCapacity;
        if (size_ < kCapacity)
            ++size_;
    }

    void markAnswered (int64_t absSample, int64_t relSample) noexcept
    {
        for (int i = 0; i < size_; ++i)
        {
            auto& m = ring_[static_cast<size_t> (i)];
            if (m.absSample == absSample)
            {
                m.answered = true;
                break;
            }
        }
        lastAnsweredRel_ = relSample;
    }

    int size() const noexcept { return size_; }

    /** Aggregate the entries inside `windowBeats` of now. O(kCapacity), no sort. */
    ParasiteHistorySummary summarise (int64_t nowRel, double samplesPerBeat,
                                      float windowBeats) const noexcept
    {
        ParasiteHistorySummary s;
        const double spb = std::max (1.0, samplesPerBeat);
        const int64_t windowSamples =
            static_cast<int64_t> (std::llround (static_cast<double> (windowBeats) * spb));

        float sMin = 1.0f, sMax = 0.0f, bMin = 1.0f, bMax = 0.0f, sSum = 0.0f;
        int nAttack = 0, nShift = 0;
        int64_t oldest = 0, newest = 0;

        for (int i = 0; i < size_; ++i)
        {
            const auto& m = ring_[static_cast<size_t> (i)];
            if (nowRel - m.relSample > windowSamples)
                continue;
            if (s.offered == 0)
                oldest = newest = m.relSample;
            else
            {
                oldest = std::min (oldest, m.relSample);
                newest = std::max (newest, m.relSample);
            }
            ++s.offered;
            if (m.answered) ++s.answered;
            if (m.exchange) ++s.exchanges;
            sMin = std::min (sMin, m.strength);
            sMax = std::max (sMax, m.strength);
            bMin = std::min (bMin, m.brightness);
            bMax = std::max (bMax, m.brightness);
            sSum += m.strength;
            if (m.kind == StimulusKind::Attack) ++nAttack; else ++nShift;
        }

        if (s.offered == 0)
            return s;

        s.beatsSinceLast = static_cast<float> (static_cast<double> (nowRel - newest) / spb);
        s.ratePerBeat =
            static_cast<float> (static_cast<double> (s.offered) / std::max (0.25, static_cast<double> (windowBeats)));
        s.success = static_cast<float> (s.answered) / static_cast<float> (s.offered);

        if (s.offered >= 2)
        {
            // Mean spacing without sorting: the window's span over its intervals.
            const double meanGap = static_cast<double> (newest - oldest) / spb
                                   / static_cast<double> (s.offered - 1);
            s.gapScore = std::clamp (static_cast<float> (meanGap / 1.5), 0.0f, 1.0f);
            const float kindMix = 2.0f * static_cast<float> (std::min (nAttack, nShift))
                                  / static_cast<float> (s.offered);
            s.interest = std::clamp (0.45f * std::min (1.0f, (bMax - bMin) * 2.5f)
                                         + 0.35f * std::min (1.0f, (sMax - sMin) * 2.0f)
                                         + 0.20f * kindMix,
                                     0.0f, 1.0f);
        }
        else
        {
            s.gapScore = 1.0f;
            s.interest = std::clamp (0.35f * sSum, 0.0f, 1.0f);
        }
        return s;
    }

private:
    std::array<ParasiteStimulusMemo, kCapacity> ring_ {};
    int head_ = 0;
    int size_ = 0;
    int64_t lastAnsweredRel_ = -1;
};

/** Bounded 0…1 drives read by the state machine. */
struct ParasitePressures
{
    float source = 0.0f;
    float attachment = 0.0f;
    float conversation = 0.0f;
    float withdrawal = 0.0f;
    float fatigue = 0.0f;
};

struct ParasiteTransition
{
    int64_t relSample = 0;
    uint8_t from = 0;
    uint8_t to = 0;
    uint8_t major = 0;
};

/**
 * The Stage 2 brain. It owns a paused-with-the-transport sample clock, the
 * bounded stimulus history, the pressures derived from it, and the state.
 *
 * It decides only *manner*: how readily the parasite accepts a stimulus and
 * how it shapes the answer. It never manufactures a stimulus, so no state —
 * ANSWERING included — can make a sound without the source doing something
 * first.
 */
class ParasiteRelationshipModel
{
public:
    using RNG = pfl::generative::DeterministicRNG;

    static constexpr float kMinorEvalBeats = 1.0f;
    static constexpr float kMajorEvalBeatsMin = 4.0f;
    static constexpr float kMajorEvalBeatsMax = 16.0f;
    static constexpr float kWindowBeats = 8.0f;
    static constexpr float kAbandonBeats = 6.0f;
    static constexpr float kFatiguePerResponse = 0.18f;
    static constexpr float kFatigueTauBeats = 6.0f;
    static constexpr float kInertia = 0.35f;
    static constexpr float kWithdrawRise = 0.15f;
    static constexpr int kNumStates = static_cast<int> (ParasiteRelationship::Count);
    static constexpr int kMaxTransitionTrace = 1024;

    // Dwell floors, in beats. Nothing leaves a state before it has lived in it.
    static constexpr float kDwellLurking = 2.0f;
    static constexpr float kDwellAttached = 4.0f;
    static constexpr float kDwellAnswering = 3.0f;
    static constexpr float kDwellWithdrawn = 6.0f;

    // Transition thresholds. Entering costs more than leaving (hysteresis).
    static constexpr float kAttachEnter = 0.35f;
    static constexpr float kAnswerEnter = 0.50f;
    static constexpr float kAnswerExit = 0.30f;
    static constexpr float kAnswerFatigueMax = 0.60f;
    static constexpr float kAnswerFatigueExit = 0.80f;
    static constexpr float kWithdrawFromLurking = 0.58f;
    static constexpr float kWithdrawFromAttached = 0.54f;
    static constexpr float kWithdrawFromAnswering = 0.50f;
    static constexpr float kWithdrawExit = 0.40f;

    struct SlotRange
    {
        int lo = 0;
        int hi = 0;
    };

    void prepare() { trace_.reserve (kMaxTransitionTrace); }

    void setClockRng (const RNG& rng) noexcept { clockRng_ = rng; }

    /** Full reset: clock, history, state, pressures and the journey metrics. */
    void reset() noexcept
    {
        onDiscontinuity();
        relSample_ = 0;
        stateEnterRel_ = 0;
        lastEvalRel_ = 0;
        nextMinorRel_ = -1;
        nextMajorRel_ = -1;
        stateSamples_.fill (0);
        playedSamples_ = 0;
        transitions_ = 0;
        trace_.clear();
    }

    /**
     * Seek, loop wrap or a SEED change. The parasite lost the thread: history
     * and pressures go, the state falls back to LURKING, the DNA does not move.
     * The journey metrics survive, because they describe the whole session.
     */
    void onDiscontinuity() noexcept
    {
        history_.reset();
        pressures_ = {};
        lastSummary_ = {};
        state_ = ParasiteRelationship::Lurking;
        stateEnterRel_ = relSample_;
        lastEvalRel_ = relSample_;
        nextMinorRel_ = -1;
        nextMajorRel_ = -1;
        winSamples_ = 0;
        winVoiceSamples_ = 0;
        winFillSum_ = 0.0f;
    }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }

    int64_t clock() const noexcept { return relSample_; }

    void noteStimulus (const StimulusEvent& e, double samplesPerBeat) noexcept
    {
        history_.push (e, relSample_, samplesPerBeat);
    }

    void noteResponse (int64_t absStimulusSample) noexcept
    {
        history_.markAnswered (absStimulusSample, relSample_);
        pressures_.fatigue = std::clamp (pressures_.fatigue + kFatiguePerResponse, 0.0f, 1.0f);
    }

    /**
     * One sample of relationship time. Only called while the transport plays,
     * which is what makes "stopped" a pause rather than a wall clock.
     */
    void advance (double samplesPerBeat, bool voiceActive, float fill01) noexcept
    {
        const double spb = std::max (1.0, samplesPerBeat);
        if (nextMinorRel_ < 0)
        {
            nextMinorRel_ = relSample_ + beatsToSamples (kMinorEvalBeats, spb);
            nextMajorRel_ = relSample_ + beatsToSamples (drawMajorBeats(), spb);
        }

        ++winSamples_;
        if (voiceActive)
            ++winVoiceSamples_;
        winFillSum_ += fill01;

        stateSamples_[static_cast<size_t> (state_)] += 1;
        ++playedSamples_;
        ++relSample_;

        if (relSample_ >= nextMinorRel_)
        {
            const bool major = relSample_ >= nextMajorRel_;
            evaluate (major, spb);
            nextMinorRel_ = relSample_ + beatsToSamples (kMinorEvalBeats, spb);
            if (major)
                nextMajorRel_ = relSample_ + beatsToSamples (drawMajorBeats(), spb);
        }
    }

    // ---- what the response brain reads --------------------------------------

    ParasiteRelationship state() const noexcept { return state_; }
    const ParasitePressures& pressures() const noexcept { return pressures_; }
    int historySize() const noexcept { return history_.size(); }

    /** The window the last evaluation read. Diagnostics for renders and tests. */
    const ParasiteHistorySummary& lastSummary() const noexcept { return lastSummary_; }

    /** Multiplies the HUNGER accept probability. Never lifts the 0.62 cap. */
    float acceptGain() const noexcept
    {
        switch (state_)
        {
            case ParasiteRelationship::Lurking: return 0.80f;
            case ParasiteRelationship::Attached: return 1.00f;
            case ParasiteRelationship::Answering: return 1.20f;
            case ParasiteRelationship::Withdrawn: return 0.20f;
            default: return 1.00f;
        }
    }

    /** Stretches DNA politeness. The HUNGER minimum gap underneath is untouched. */
    float gapMultiplier() const noexcept
    {
        switch (state_)
        {
            case ParasiteRelationship::Lurking: return 1.15f;
            case ParasiteRelationship::Attached: return 1.00f;
            case ParasiteRelationship::Answering: return 0.85f;
            case ParasiteRelationship::Withdrawn: return 1.60f;
            default: return 1.00f;
        }
    }

    float levelScale() const noexcept
    {
        switch (state_)
        {
            case ParasiteRelationship::Lurking: return 0.85f;
            case ParasiteRelationship::Attached: return 1.00f;
            case ParasiteRelationship::Answering: return 1.10f;
            case ParasiteRelationship::Withdrawn: return 0.70f;
            default: return 1.00f;
        }
    }

    /** Half of the Stage 1 delay vocabulary the state prefers. ATTACHED is all of it. */
    SlotRange delayTilt() const noexcept
    {
        switch (state_)
        {
            case ParasiteRelationship::Lurking: return { 2, ParasiteDNA::kDelaySlots - 1 };
            case ParasiteRelationship::Answering: return { 0, 3 };
            case ParasiteRelationship::Withdrawn: return { 3, ParasiteDNA::kDelaySlots - 1 };
            default: return { 0, ParasiteDNA::kDelaySlots - 1 };
        }
    }

    SlotRange durationTilt() const noexcept
    {
        switch (state_)
        {
            case ParasiteRelationship::Lurking: return { 0, 3 };
            case ParasiteRelationship::Answering: return { 1, ParasiteDNA::kDurSlots - 1 };
            case ParasiteRelationship::Withdrawn: return { 0, 2 };
            default: return { 0, ParasiteDNA::kDurSlots - 1 };
        }
    }

    // ---- journey metrics -----------------------------------------------------

    float occupancy (ParasiteRelationship s) const noexcept
    {
        const auto i = static_cast<size_t> (s);
        if (playedSamples_ <= 0 || i >= stateSamples_.size())
            return 0.0f;
        return static_cast<float> (static_cast<double> (stateSamples_[i])
                                   / static_cast<double> (playedSamples_));
    }

    int64_t occupancySamples (ParasiteRelationship s) const noexcept
    {
        const auto i = static_cast<size_t> (s);
        return i < stateSamples_.size() ? stateSamples_[i] : 0;
    }

    uint32_t transitionCount() const noexcept { return transitions_; }
    const std::vector<ParasiteTransition>& transitions() const noexcept { return trace_; }
    int64_t playedSamples() const noexcept { return playedSamples_; }

private:
    static int64_t beatsToSamples (float beats, double spb) noexcept
    {
        return std::max<int64_t> (1, static_cast<int64_t> (std::llround (
                                         static_cast<double> (beats) * spb)));
    }

    /**
     * How far away the next major opportunity is. Drawn from its own stream so
     * that MUTATION — which owns DNA and nothing else — cannot become a state
     * transition rate.
     */
    float drawMajorBeats() noexcept
    {
        return kMajorEvalBeatsMin
               + (kMajorEvalBeatsMax - kMajorEvalBeatsMin) * clockRng_.nextFloat();
    }

    void evaluate (bool major, double spb) noexcept
    {
        const auto sum = history_.summarise (relSample_, spb, kWindowBeats);
        lastSummary_ = sum;
        const float winFill = winSamples_ > 0
                                  ? winFillSum_ / static_cast<float> (winSamples_)
                                  : 0.0f;
        const float occ = winSamples_ > 0
                              ? static_cast<float> (winVoiceSamples_)
                                    / static_cast<float> (winSamples_)
                              : 0.0f;
        winSamples_ = 0;
        winVoiceSamples_ = 0;
        winFillSum_ = 0.0f;

        const float dtBeats =
            static_cast<float> (static_cast<double> (relSample_ - lastEvalRel_) / spb);
        lastEvalRel_ = relSample_;
        pressures_.fatigue *= std::exp (-std::max (0.0f, dtBeats) / kFatigueTauBeats);

        // Two independent readings of "how much room is there": how often the
        // source hands out events, and whether it ever leaves its own peak.
        // Two independent ways for a source to be busy: it hands out events
        // faster than one voice can answer them, or it never steps off its own
        // peak. A wall of sound produces almost no events precisely because it
        // is a wall, so these have to count separately rather than blend.
        const float density01 = std::clamp (sum.ratePerBeat / 2.5f, 0.0f, 1.0f);
        const float wall01 = std::clamp ((winFill - 0.75f) / 0.15f, 0.0f, 1.0f);
        const float sourceT = std::clamp (std::max (density01, wall01), 0.0f, 1.0f);
        const float space01 =
            std::clamp (0.55f * (1.0f - winFill) + 0.45f * sum.gapScore, 0.0f, 1.0f);

        const float attachT =
            sum.offered == 0
                ? 0.0f
                : std::clamp (0.40f * sum.interest + 0.35f * space01 + 0.25f * sum.success,
                              0.0f, 1.0f);

        // A conversation is evidence that answering changed what came back:
        // stimuli landing in the shadow of an answer, plus a healthy hit rate.
        const float share = sum.offered > 0 ? static_cast<float> (sum.exchanges)
                                                  / static_cast<float> (sum.offered)
                                            : 0.0f;
        const float exchange01 = std::clamp (share / 0.40f, 0.0f, 1.0f);
        const float answerRate01 = std::clamp (sum.success / 0.30f, 0.0f, 1.0f);
        const float convT =
            sum.offered == 0
                ? 0.0f
                : std::clamp (0.45f * exchange01 + 0.35f * answerRate01 + 0.20f * sum.interest,
                              0.0f, 1.0f);

        const float withdrawT = std::clamp (
            0.70f * sourceT + 0.20f * pressures_.fatigue + 0.10f * occ, 0.0f, 1.0f);

        pressures_.source += kInertia * (sourceT - pressures_.source);
        pressures_.attachment += kInertia * (attachT - pressures_.attachment);
        pressures_.conversation += kInertia * (convT - pressures_.conversation);
        // Backing off is a decision, not a reflex: withdrawal builds over about
        // ten beats of sustained pressure, so the peak of a swell cannot chase
        // the parasite away while an actual wall of sound still can.
        pressures_.withdrawal +=
            (withdrawT > pressures_.withdrawal ? kWithdrawRise : kInertia)
            * (withdrawT - pressures_.withdrawal);

        transitionStep (major, sum, spb);
    }

    void transitionStep (bool major, const ParasiteHistorySummary& sum, double spb) noexcept
    {
        const float beatsInState =
            static_cast<float> (static_cast<double> (relSample_ - stateEnterRel_) / spb);
        const bool starved = sum.offered == 0 || sum.beatsSinceLast > kAbandonBeats;
        const auto& p = pressures_;
        auto next = state_;

        switch (state_)
        {
            case ParasiteRelationship::Lurking:
                if (beatsInState >= kDwellLurking)
                {
                    // Not in the locked skeleton, but a wall of sound has to be
                    // able to drive the parasite off before it ever attaches.
                    if (p.withdrawal > kWithdrawFromLurking)
                        next = ParasiteRelationship::Withdrawn;
                    else if (major && ! starved && p.attachment > kAttachEnter)
                        next = ParasiteRelationship::Attached;
                }
                break;

            case ParasiteRelationship::Attached:
                if (beatsInState >= kDwellAttached)
                {
                    if (p.withdrawal > kWithdrawFromAttached)
                        next = ParasiteRelationship::Withdrawn;
                    else if (starved)
                        next = ParasiteRelationship::Lurking;
                    else if (major && p.conversation > kAnswerEnter
                             && p.fatigue < kAnswerFatigueMax)
                        next = ParasiteRelationship::Answering;
                }
                break;

            case ParasiteRelationship::Answering:
                if (beatsInState >= kDwellAnswering)
                {
                    if (p.withdrawal > kWithdrawFromAnswering || p.fatigue > kAnswerFatigueExit)
                        next = ParasiteRelationship::Withdrawn;
                    else if (starved || (major && p.conversation < kAnswerExit))
                        next = ParasiteRelationship::Attached;
                }
                break;

            case ParasiteRelationship::Withdrawn:
                if (major && beatsInState >= kDwellWithdrawn && p.withdrawal < kWithdrawExit)
                    next = (! starved && p.attachment > kAttachEnter)
                               ? ParasiteRelationship::Attached
                               : ParasiteRelationship::Lurking;
                break;

            default:
                break;
        }

        if (next == state_)
            return;

        if (traceEnabled_ && trace_.size() < trace_.capacity())
        {
            ParasiteTransition t;
            t.relSample = relSample_;
            t.from = static_cast<uint8_t> (state_);
            t.to = static_cast<uint8_t> (next);
            t.major = major ? 1u : 0u;
            trace_.push_back (t);
        }
        state_ = next;
        stateEnterRel_ = relSample_;
        ++transitions_;
    }

    RecentStimulusHistory history_;
    ParasiteHistorySummary lastSummary_ {};
    ParasitePressures pressures_ {};
    ParasiteRelationship state_ = ParasiteRelationship::Lurking;
    RNG clockRng_ {};

    int64_t relSample_ = 0;
    int64_t stateEnterRel_ = 0;
    int64_t lastEvalRel_ = 0;
    int64_t nextMinorRel_ = -1;
    int64_t nextMajorRel_ = -1;

    int64_t winSamples_ = 0;
    int64_t winVoiceSamples_ = 0;
    float winFillSum_ = 0.0f;

    std::array<int64_t, static_cast<size_t> (ParasiteRelationship::Count)> stateSamples_ {};
    int64_t playedSamples_ = 0;
    uint32_t transitions_ = 0;

    bool traceEnabled_ = false;
    std::vector<ParasiteTransition> trace_;
};

// ============================================================================
// ParasiteVoice — generated wet: noise → resonant LP chirp → AR → sat → pan
// ============================================================================

class ParasiteVoice
{
public:
    static constexpr float kMaxResonance = 0.72f;
    static constexpr float kMaxPan = 0.85f;

    void prepare (double sampleRate) noexcept
    {
        sr_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        filter_.prepare (sr_);
        cutSm_.prepare (sr_, 0.050f);
        resSm_.prepare (sr_, 0.012f);
        drvSm_.prepare (sr_, 0.015f);
        panSm_.prepare (sr_, 0.060f);
        maxCutoff_ = std::min (8000.0f, static_cast<float> (0.42 * sr_));
        reset();
    }

    void reset() noexcept
    {
        phase_ = 0;
        pos_ = 0;
        env_ = 0.0f;
        envStart_ = 0.0f;
        relStart_ = 0.0f;
        level_ = 0.0f;
        burst_ = 0.0f;
        makeup_ = 1.0f;
        filter_.reset();
        cutSm_.setCurrentAndTarget (800.0f);
        resSm_.setCurrentAndTarget (0.12f);
        drvSm_.setCurrentAndTarget (0.05f);
        panSm_.setCurrentAndTarget (0.0f);
    }

    struct NoteOn
    {
        float energy = 0.35f;
        float brightness = 0.45f;
        float change = 0.40f;
        float balance = 0.0f;
        int64_t durationSamples = 4800;
        float levelScale = 1.0f; // relationship trim; 1.0 is the Stage 1 voice
    };

    void noteOn (const NoteOn& n, const ParasiteDNA& dna) noexcept
    {
        const float E = std::clamp (n.energy, 0.0f, 1.0f);
        const float B = std::clamp (n.brightness + 0.35f * dna.brightnessBias, 0.0f, 1.0f);
        const float C = std::clamp (n.change, 0.0f, 1.0f);

        const float f0 = std::clamp (180.0f * std::pow (6200.0f / 180.0f, B), 80.0f, maxCutoff_);
        const float f1 = std::clamp (
            f0 * std::pow (2.0f, dna.chirpSign * (0.15f + 0.85f * C) * 1.75f), 80.0f, maxCutoff_);
        const float chirpSec = (25.0f + 90.0f * (1.0f - C)) * 0.001f;

        cutSm_.setTime (chirpSec);
        cutSm_.setCurrentAndTarget (f0);
        cutSm_.setTarget (f1);

        // A one-pole LP throws away most of the noise energy, and how much
        // depends on where the chirp sits. Compensate once at note-on from the
        // geometric mean cutoff so a dark answer is as present as a bright one.
        const float gMid = 1.0f
                           - std::exp (-2.0f * 3.14159265f * std::sqrt (f0 * f1)
                                       / static_cast<float> (sr_));
        makeup_ = std::clamp (1.0f / std::sqrt (std::max (gMid, 1.0e-4f) * 0.5f), 1.0f, 6.0f);

        const float res = std::clamp (0.12f + 0.48f * C * (0.55f + 0.45f * dna.resBias),
                                      0.0f, kMaxResonance);
        resSm_.setTarget (res);

        level_ = std::clamp ((0.08f + 0.42f * std::sqrt (E)) * (1.0f + 0.30f * dna.energyBias)
                                 * std::clamp (n.levelScale, 0.0f, 1.25f),
                             0.0f, 0.60f);
        burst_ = std::clamp (0.35f + 0.65f * E, 0.0f, 1.0f);
        drvSm_.setTarget (std::clamp (0.05f + 0.22f * E * (0.4f + 0.6f * C), 0.0f, 0.30f));

        const float pan = dna.stereoMode > 0.5f
                              ? dna.stereoFixed
                              : 0.65f * n.balance + 0.35f * dna.panBias;
        panSm_.setTarget (std::clamp (pan, -kMaxPan, kMaxPan));

        const int64_t durN = std::max<int64_t> (msN (12.0), n.durationSamples);
        int attN = msN (1.5 + 6.0 * (1.0 - static_cast<double> (C)));
        attN = std::max (1, static_cast<int> (std::min<int64_t> (attN, durN / 4 + 1)));
        const float relMs = (40.0f + 180.0f * (1.0f - 0.65f * C)) * (0.7f + 0.6f * dna.durBias);

        attN_ = attN;
        holdN_ = std::max<int64_t> (1, durN - attN_);
        relN_ = std::max (msN (25.0), msN (static_cast<double> (relMs)));

        // Retrigger restarts the AR from the current envelope — never a hard zero.
        envStart_ = phase_ != 0 ? env_ : 0.0f;
        phase_ = 1;
        pos_ = 0;
    }

    void stopSafely() noexcept
    {
        if (phase_ == 0)
            return;
        relStart_ = env_;
        relN_ = std::max (msN (25.0), std::min (relN_, msN (60.0)));
        phase_ = 3;
        pos_ = 0;
    }

    bool isActive() const noexcept { return phase_ != 0; }

    void processSample (float& outL, float& outR,
                        pfl::generative::DeterministicRNG& noiseRng) noexcept
    {
        if (phase_ == 0)
        {
            outL = 0.0f;
            outR = 0.0f;
            return;
        }

        const float cut = std::clamp (cutSm_.getNext(), 80.0f, maxCutoff_);
        filter_.setCutoffHz (cut);
        filter_.setResonance (std::clamp (resSm_.getNext(), 0.0f, kMaxResonance));

        float x = (noiseRng.nextFloat() * 2.0f - 1.0f) * burst_;
        x = filter_.processSample (x);

        advanceEnvelope();
        x *= env_ * level_ * makeup_;

        sat_.setDrive (std::clamp (drvSm_.getNext(), 0.0f, 0.30f));
        x = sat_.processSample (x);
        if (! std::isfinite (x))
            x = 0.0f;

        const float angle = (std::clamp (panSm_.getNext(), -kMaxPan, kMaxPan) + 1.0f)
                            * 0.25f * 3.14159265f;
        outL = x * std::cos (angle);
        outR = x * std::sin (angle);
    }

    float envelope() const noexcept { return env_; }

private:
    void advanceEnvelope() noexcept
    {
        if (phase_ == 1)
        {
            ++pos_;
            const float t = std::min (1.0f, static_cast<float> (pos_)
                                                / static_cast<float> (std::max (1, attN_)));
            env_ = envStart_ + (1.0f - envStart_) * t;
            if (pos_ >= attN_)
            {
                env_ = 1.0f;
                phase_ = 2;
                pos_ = 0;
            }
        }
        else if (phase_ == 2)
        {
            env_ = 1.0f;
            if (++pos_ >= holdN_)
            {
                phase_ = 3;
                pos_ = 0;
                relStart_ = 1.0f;
            }
        }
        else if (phase_ == 3)
        {
            ++pos_;
            const float t = std::min (1.0f, static_cast<float> (pos_)
                                                / static_cast<float> (std::max (1, relN_)));
            env_ = relStart_ * (1.0f - t);
            if (pos_ >= relN_ || env_ < 1.0e-5f)
            {
                env_ = 0.0f;
                phase_ = 0;
                pos_ = 0;
            }
        }
    }

    int msN (double ms) const noexcept
    {
        return ParasiteFeatureExtractor::msSamples (ms, sr_);
    }

    double sr_ = 44100.0;
    float maxCutoff_ = 8000.0f;
    Filter filter_;
    Saturator sat_;
    ParamSmoother cutSm_, resSm_, drvSm_, panSm_;

    int phase_ = 0; // 0 idle, 1 attack, 2 hold, 3 release
    int64_t pos_ = 0;
    int attN_ = 64;
    int64_t holdN_ = 1024;
    int relN_ = 2048;
    float env_ = 0.0f, envStart_ = 0.0f, relStart_ = 0.0f;
    float level_ = 0.0f, burst_ = 0.0f, makeup_ = 1.0f;
};

// ============================================================================
// SignalParasiteEngine — algorithm v2
// ============================================================================

/**
 * Signal Parasite Stage 2 — a collaborator with a relationship to its source.
 *
 * Listen (original input only) → StimulusDetector → RelationshipModel →
 * ParasiteDNA response decision → ONE generated ParasiteVoice → DC → limiter
 * → MIX → OUTPUT.
 *
 * Stage 2 adds a bounded history of recent stimuli — descriptors, not audio —
 * and a LURKING / ATTACHED / ANSWERING / WITHDRAWN state over it. The state
 * biases how readily and how forwardly the parasite answers; DNA still owns
 * the personality, and a stimulus is still the only thing that can make a
 * sound. Algorithm v2: no performance verbs, no FFT, no pitch, one voice.
 */
class SignalParasiteEngine
{
public:
    static constexpr int kAlgorithmVersion = 2;
    static constexpr int kNumSuppressReasons = static_cast<int> (ParasiteSuppressReason::Count);
    static constexpr int kMaxTraceEvents = 4096;
    static constexpr double kWarmupMs = 50.0;

    static constexpr std::array<double, ParasiteDNA::kDelaySlots> kDelayBeats {
        0.0, 0.0625, 0.125, 0.25, 0.5, 1.0
    };
    static constexpr std::array<double, ParasiteDNA::kDurSlots> kDurationBeats {
        0.0625, 0.125, 0.25, 0.5, 1.0
    };

    void prepare (double sampleRate, int maxBlockSize = 1024) noexcept
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        maxBlockSize_ = std::max (64, maxBlockSize);
        mixSm_.prepare (sampleRate_, 0.05f);
        sensSm_.prepare (sampleRate_, 0.08f);
        hungerSm_.prepare (sampleRate_, 0.08f);
        mutSm_.prepare (sampleRate_, 0.08f);
        outSm_.prepare (sampleRate_, 0.05f);
        features_.prepare (sampleRate_);
        detector_.prepare (sampleRate_);
        voice_.prepare (sampleRate_);
        dcL_.prepare (sampleRate_);
        dcR_.prepare (sampleRate_);
        limL_.prepare (sampleRate_);
        limR_.prepare (sampleRate_);
        warmupN_ = ParasiteFeatureExtractor::msSamples (kWarmupMs, sampleRate_);
        stimulusTrace_.reserve (kMaxTraceEvents);
        responseTrace_.reserve (kMaxTraceEvents);
        relationship_.prepare();
        reset();
    }

    void reset() noexcept
    {
        features_.reset();
        detector_.reset();
        detector_.clearCounters();
        voice_.reset();
        dcL_.reset();
        dcR_.reset();
        limL_.reset();
        limR_.reset();
        absSample_ = 0;
        beatClock_ = 0.0;
        lastPpq_ = -1.0e9;
        lastPlaying_ = false;
        lastBar_ = -1;
        lastSensQ_ = -1;
        warmupLeft_ = warmupN_;
        pendingActive_ = false;
        pendingSlid_ = false;
        beatsSinceResponse_ = 4.0f;
        prevMutation_ = -1.0f;
        responseCount_ = 0;
        voiceActiveSamples_ = 0;
        totalSamples_ = 0;
        lastResponseOnsetSample_ = -1;
        minResponseGapSamples_ = -1;
        voiceBusyStim_ = -1;
        suppress_.fill (0);
        stimulusTrace_.clear();
        responseTrace_.clear();
        samplesPerBeat_ = sampleRate_ * 0.5;
        rebuildRng();
        relationship_.reset();
        birthDNA (0);
        seedDirty_ = false;
    }

    void setSeed (uint64_t seed) noexcept
    {
        const uint64_t s = seed == 0 ? 1ull : seed;
        if (s == masterSeed_)
            return;
        masterSeed_ = s;
        seedDirty_ = true;
    }

    uint64_t seed() const noexcept { return masterSeed_; }

    void setMix (float v) noexcept { mixSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setSensitivity (float v) noexcept { sensSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setHunger (float v) noexcept { hungerSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setMutation (float v) noexcept { mutSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setOutput (float v) noexcept { outSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }

    void setMacros (float mix, float sensitivity, float hunger, float mutation,
                    float output) noexcept
    {
        setMix (mix);
        setSensitivity (sensitivity);
        setHunger (hunger);
        setMutation (mutation);
        setOutput (output);
    }

    void snapMacros() noexcept
    {
        mixSm_.setCurrentAndTarget (mixSm_.target());
        sensSm_.setCurrentAndTarget (sensSm_.target());
        hungerSm_.setCurrentAndTarget (hungerSm_.target());
        mutSm_.setCurrentAndTarget (mutSm_.target());
        outSm_.setCurrentAndTarget (outSm_.target());
    }

    /** Rebuild the DNA universe from SEED at an absolute bar (project load / tests). */
    void forceRebuild (int bar = 0) noexcept
    {
        rebuildRng();
        relationship_.reset();
        birthDNA (bar);
        detector_.reset();
        detector_.clearCounters();
        features_.reset();
        voice_.reset();
        pendingActive_ = false;
        pendingSlid_ = false;
        lastBar_ = bar - 1;
        warmupLeft_ = warmupN_;
        beatsSinceResponse_ = 4.0f;
        prevMutation_ = -1.0f;
        responseCount_ = 0;
        voiceActiveSamples_ = 0;
        totalSamples_ = 0;
        lastResponseOnsetSample_ = -1;
        minResponseGapSamples_ = -1;
        voiceBusyStim_ = -1;
        suppress_.fill (0);
        stimulusTrace_.clear();
        responseTrace_.clear();
        seedDirty_ = false;
    }

    void setTraceEnabled (bool e) noexcept
    {
        traceEnabled_ = e;
        relationship_.setTraceEnabled (e);
    }
    bool traceEnabled() const noexcept { return traceEnabled_; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  double ppqStart, double bpm, bool playing) noexcept
    {
        if (inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr
            || numSamples <= 0)
            return;

        const double safeBpm = bpm > 1.0 ? bpm : 120.0;
        const double beatsPerSample = (safeBpm / 60.0) / sampleRate_;
        samplesPerBeat_ = sampleRate_ * 60.0 / safeBpm;
        detector_.setStaleSamples (
            static_cast<int64_t> (std::llround (ParasiteStimulusDetector::kStaleBeats
                                                * samplesPerBeat_)));

        if (lastPpq_ > -1.0e8)
        {
            const double jump = ppqStart - lastPpq_;
            const double maxBlockBeats = (static_cast<double> (maxBlockSize_) / sampleRate_)
                                             * (safeBpm / 60.0) * 2.5
                                         + 0.05;
            if (jump < -0.01 || jump > maxBlockBeats)
                handleSeek (ppqStart);
        }

        if (playing && ! lastPlaying_)
        {
            // Re-arm quietly: the first sample after digital silence must not be an onset.
            features_.reset();
            detector_.reset();
            warmupLeft_ = warmupN_;
        }
        else if (! playing && lastPlaying_)
        {
            detector_.clearQueue();
            pendingActive_ = false;
            pendingSlid_ = false;
            voice_.stopSafely(); // finish/release any sounding answer; no new scheduling
        }
        lastPlaying_ = playing;

        for (int i = 0; i < numSamples; ++i)
        {
            float dryL = inL[i];
            float dryR = inR[i];
            if (! std::isfinite (dryL)) dryL = 0.0f;
            if (! std::isfinite (dryR)) dryR = 0.0f;

            const float mix = mixSm_.getNext();
            const float sens = sensSm_.getNext();
            const float hunger = hungerSm_.getNext();
            const float mut = mutSm_.getNext();
            const float outG = outSm_.getNext();

            const int sensQ = static_cast<int> (std::lround (sens * 1024.0f));
            if (sensQ != lastSensQ_)
            {
                lastSensQ_ = sensQ;
                detector_.setSensitivity (static_cast<float> (sensQ) / 1024.0f);
            }

            if (playing)
            {
                const double ppq = ppqStart + static_cast<double> (i) * beatsPerSample;
                advanceMusical (ppq, mut);

                // Relationship time only runs while the transport does, so a
                // stop is a pause and never a wall clock.
                relationship_.advance (samplesPerBeat_, voice_.isActive(),
                                       features_.frame().fill01);

                features_.processSample (dryL, dryR);
                if (warmupLeft_ > 0)
                {
                    --warmupLeft_;
                }
                else if (detector_.processFeature (features_.frame(), absSample_, ppq))
                {
                    if (traceEnabled_ && stimulusTrace_.size() < stimulusTrace_.capacity())
                        stimulusTrace_.push_back (detector_.lastEvent());
                    relationship_.noteStimulus (detector_.lastEvent(), samplesPerBeat_);
                }

                drainStimuli (hunger);

                if (pendingActive_ && absSample_ >= pending_.onsetSample)
                    resolvePending();

                beatsSinceResponse_ = std::min (1.0e6f,
                                                beatsSinceResponse_
                                                    + static_cast<float> (beatsPerSample));
                beatClock_ += beatsPerSample;
            }

            float wetL = 0.0f;
            float wetR = 0.0f;
            voice_.processSample (wetL, wetR, voiceNoiseRng_);
            wetL = dcL_.processSample (wetL);
            wetR = dcR_.processSample (wetR);
            wetL = limL_.processSample (wetL);
            wetR = limR_.processSample (wetR);

            float mixedL = dryL * (1.0f - mix) + wetL * mix;
            float mixedR = dryR * (1.0f - mix) + wetR * mix;
            mixedL = std::clamp (mixedL * outG, -0.99f, 0.99f);
            mixedR = std::clamp (mixedR * outG, -0.99f, 0.99f);
            if (! std::isfinite (mixedL)) mixedL = 0.0f;
            if (! std::isfinite (mixedR)) mixedR = 0.0f;
            outL[i] = mixedL;
            outR[i] = mixedR;

            if (voice_.isActive())
                ++voiceActiveSamples_;
            ++totalSamples_;
            ++absSample_;
        }

        lastPpq_ = playing ? ppqStart + static_cast<double> (numSamples) * beatsPerSample
                           : ppqStart;
    }

    // ---- metrics / test hooks -------------------------------------------------

    uint32_t stimulusCount() const noexcept
    {
        return detector_.attackCount() + detector_.shiftCount();
    }
    uint32_t attackCount() const noexcept { return detector_.attackCount(); }
    uint32_t shiftCount() const noexcept { return detector_.shiftCount(); }
    uint32_t responseCount() const noexcept { return responseCount_; }
    uint32_t overflowDrops() const noexcept { return detector_.overflowDrops(); }

    uint32_t suppressCount (ParasiteSuppressReason r) const noexcept
    {
        const auto i = static_cast<size_t> (r);
        return i < suppress_.size() ? suppress_[i] : 0u;
    }

    const ParasiteDNA& dna() const noexcept { return dna_; }
    uint32_t dnaGeneration() const noexcept { return dna_.generation; }
    const ParasiteFeatureFrame& lastFeatures() const noexcept { return features_.frame(); }
    bool voiceActive() const noexcept { return voice_.isActive(); }

    // ---- Stage 2 relationship ------------------------------------------------

    ParasiteRelationship relationshipState() const noexcept { return relationship_.state(); }
    const ParasitePressures& pressures() const noexcept { return relationship_.pressures(); }
    const ParasiteHistorySummary& historySummary() const noexcept
    {
        return relationship_.lastSummary();
    }
    int historySize() const noexcept { return relationship_.historySize(); }
    uint32_t stateTransitions() const noexcept { return relationship_.transitionCount(); }

    /** Samples of relationship time. Advances only while the transport plays. */
    int64_t relationshipSamples() const noexcept { return relationship_.playedSamples(); }

    /** Fraction of *played* time spent in one state. Stopped time does not count. */
    float stateOccupancy (ParasiteRelationship s) const noexcept
    {
        return relationship_.occupancy (s);
    }

    /** Accepted responses per detected stimulus. −1 when nothing was detected. */
    float acceptRatio() const noexcept
    {
        const auto stim = stimulusCount();
        return stim == 0 ? -1.0f
                         : static_cast<float> (responseCount_) / static_cast<float> (stim);
    }

    /** Fraction of processed samples with the parasite voice sounding. */
    float responseDuty() const noexcept
    {
        return totalSamples_ > 0 ? static_cast<float> (voiceActiveSamples_)
                                       / static_cast<float> (totalSamples_)
                                 : 0.0f;
    }

    /** Smallest observed gap between accepted response onsets (beats); <0 if none. */
    float minResponseGapBeats() const noexcept
    {
        if (minResponseGapSamples_ < 0)
            return -1.0f;
        return static_cast<float> (static_cast<double> (minResponseGapSamples_)
                                   / std::max (1.0, samplesPerBeat_));
    }

    const std::vector<StimulusEvent>& stimuli() const noexcept { return stimulusTrace_; }
    const std::vector<ParasiteResponseEvent>& responses() const noexcept
    {
        return responseTrace_;
    }

    /** Structural stimulus schedule — no PCM, partition stable. */
    std::string stimulusFingerprint() const
    {
        std::ostringstream os;
        os << detector_.attackCount() << "/" << detector_.shiftCount() << "/"
           << detector_.overflowDrops() << ";";
        for (const auto& e : stimulusTrace_)
            os << e.sampleIndex << ":" << static_cast<int> (e.kind) << ":"
               << std::lround (e.strength * 10000.0f) << ":"
               << std::lround (e.balance * 1000.0f) << ";";
        return os.str();
    }

    /** Structural response schedule — no PCM, partition stable. */
    std::string responseFingerprint() const
    {
        std::ostringstream os;
        os << responseCount_ << ";";
        for (int r = 0; r < kNumSuppressReasons; ++r)
            os << suppress_[static_cast<size_t> (r)] << ",";
        os << ";";
        for (const auto& e : responseTrace_)
            os << e.onsetSample << ":" << e.delaySlot << ":" << e.durSlot << ":"
               << std::lround (e.pan * 1000.0f) << ";";
        return os.str();
    }

    /** Structural relationship schedule — transitions plus occupancy, no PCM. */
    std::string stateFingerprint() const
    {
        std::ostringstream os;
        os << relationship_.transitionCount() << ";";
        for (int s = 0; s < ParasiteRelationshipModel::kNumStates; ++s)
            os << relationship_.occupancySamples (static_cast<ParasiteRelationship> (s)) << ",";
        os << ";";
        for (const auto& t : relationship_.transitions())
            os << t.relSample << ":" << static_cast<int> (t.from) << ">"
               << static_cast<int> (t.to) << ":" << static_cast<int> (t.major) << ";";
        return os.str();
    }

    std::string dnaFingerprint() const
    {
        std::ostringstream os;
        os << dna_.generation << ":" << static_cast<int> (dna_.lastOp) << ":"
           << dna_.lifespanBars << ":";
        for (float w : dna_.delayW)
            os << std::lround (w * 1000.0f) << ",";
        os << ":";
        for (float w : dna_.durW)
            os << std::lround (w * 1000.0f) << ",";
        os << ":" << std::lround (dna_.gapPreference * 1000.0f)
           << ":" << std::lround (dna_.echoBias * 1000.0f)
           << ":" << std::lround (dna_.brightnessBias * 1000.0f)
           << ":" << std::lround (dna_.energyBias * 1000.0f)
           << ":" << std::lround (dna_.staleBeats * 1000.0f)
           << ":" << std::lround (dna_.minStimulusStrength * 1000.0f);
        return os.str();
    }

private:
    // ---- RNG streams ---------------------------------------------------------

    static uint64_t hashTag (const char* s) noexcept
    {
        uint64_t h = 0xcbf29ce484222325ull;
        for (const char* p = s; *p != '\0'; ++p)
        {
            h ^= static_cast<uint64_t> (static_cast<uint8_t> (*p));
            h *= 0x100000001b3ull;
        }
        return h;
    }

    void rebuildRng() noexcept
    {
        using RNG = pfl::generative::DeterministicRNG;
        dnaBirthRng_ = RNG::derived (masterSeed_, hashTag ("parasite/dna/birth"));
        dnaMutateRng_ = RNG::derived (masterSeed_, hashTag ("parasite/dna/mutate"));
        delayRng_ = RNG::derived (masterSeed_, hashTag ("parasite/response/delay"));
        durationRng_ = RNG::derived (masterSeed_, hashTag ("parasite/response/duration"));
        acceptRng_ = RNG::derived (masterSeed_, hashTag ("parasite/response/accept"));
        spatialRng_ = RNG::derived (masterSeed_, hashTag ("parasite/response/spatial"));
        voiceNoiseRng_ = RNG::derived (masterSeed_, hashTag ("parasite/voice/noise"));
        // Its own stream: the state clock must not move when MUTATION does.
        relationship_.setClockRng (
            RNG::derived (masterSeed_, hashTag ("parasite/relationship/clock")));
    }

    static void normalise (float* w, int n, float floorEach = 0.005f) noexcept
    {
        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            w[i] = std::max (floorEach, w[i]);
            sum += w[i];
        }
        if (sum <= 1.0e-6f)
        {
            for (int i = 0; i < n; ++i)
                w[i] = 1.0f / static_cast<float> (n);
            return;
        }
        for (int i = 0; i < n; ++i)
            w[i] /= sum;
    }

    /** Cap one slot's mass and redistribute the surplus over the others. */
    static void capSlot (float* w, int n, int slot, float cap) noexcept
    {
        if (w[slot] <= cap)
            return;
        const float surplus = w[slot] - cap;
        w[slot] = cap;
        float rest = 0.0f;
        for (int i = 0; i < n; ++i)
            if (i != slot)
                rest += w[i];
        if (rest <= 1.0e-6f)
            return;
        for (int i = 0; i < n; ++i)
            if (i != slot)
                w[i] += surplus * (w[i] / rest);
    }

    void birthDNA (int bar) noexcept
    {
        auto& rng = dnaBirthRng_;
        dna_ = {};
        dna_.generation = 0;
        dna_.birthBar = bar;
        dna_.lastOp = 0;

        static constexpr float kDelayBase[ParasiteDNA::kDelaySlots] = {
            0.10f, 0.14f, 0.24f, 0.26f, 0.17f, 0.09f
        };
        static constexpr float kDurBase[ParasiteDNA::kDurSlots] = {
            0.14f, 0.28f, 0.30f, 0.18f, 0.10f
        };
        for (int i = 0; i < ParasiteDNA::kDelaySlots; ++i)
            dna_.delayW[static_cast<size_t> (i)] =
                kDelayBase[i] * (0.70f + 0.60f * rng.nextFloat());
        normalise (dna_.delayW.data(), ParasiteDNA::kDelaySlots);
        capSlot (dna_.delayW.data(), ParasiteDNA::kDelaySlots, 0, 0.18f);

        for (int i = 0; i < ParasiteDNA::kDurSlots; ++i)
            dna_.durW[static_cast<size_t> (i)] =
                kDurBase[i] * (0.70f + 0.60f * rng.nextFloat());
        normalise (dna_.durW.data(), ParasiteDNA::kDurSlots);
        capSlot (dna_.durW.data(), ParasiteDNA::kDurSlots, 4, 0.12f);

        dna_.gapPreference = 0.35f + 0.30f * rng.nextFloat();
        gapPreferenceBirth_ = dna_.gapPreference;
        dna_.echoBias = 0.25f + 0.30f * rng.nextFloat();
        dna_.brightnessBias = -0.35f + 0.70f * rng.nextFloat();
        dna_.energyBias = -0.35f + 0.70f * rng.nextFloat();
        dna_.stereoMode = 0.40f * rng.nextFloat();
        dna_.stereoFixed = -1.0f + 2.0f * rng.nextFloat();
        dna_.minStimulusStrength = 0.15f + 0.20f * rng.nextFloat();
        dna_.staleBeats = 1.0f + 1.0f * rng.nextFloat();
        dna_.chirpSign = rng.nextFloat() < 0.5f ? -1.0f : 1.0f;
        dna_.resBias = rng.nextFloat();
        dna_.durBias = rng.nextFloat();
        dna_.panBias = (rng.nextFloat() * 2.0f - 1.0f) * 0.60f;

        const float mut = mutSm_.current() >= 0.0f ? mutSm_.current() : mutSm_.target();
        dna_.lifespanBars = parasiteLifespanBars (mut);
    }

    ParasiteMutOp drawMutOp (float mut) noexcept
    {
        if (mut <= 1.0e-4f)
            return ParasiteMutOp::Stay;
        const float u = dnaMutateRng_.nextFloat();
        const float stayP = 0.10f + 0.35f * (1.0f - std::clamp (mut, 0.0f, 1.0f));
        if (u < stayP)
            return ParasiteMutOp::Stay;
        const float v = (u - stayP) / std::max (1.0e-4f, 1.0f - stayP);
        if (v < 0.24f) return ParasiteMutOp::NudgeDelay;
        if (v < 0.46f) return ParasiteMutOp::NudgeDur;
        if (v < 0.62f) return ParasiteMutOp::TiltGap;
        if (v < 0.76f) return ParasiteMutOp::TiltEcho;
        if (v < 0.92f) return ParasiteMutOp::TiltColour;
        return ParasiteMutOp::StaleWindow;
    }

    /** One bounded touch. Never edits HUNGER curves, gap floors or the accept cap. */
    bool applyMutOp (ParasiteMutOp op) noexcept
    {
        auto& rng = dnaMutateRng_;
        switch (op)
        {
            case ParasiteMutOp::Stay:
                return false;

            case ParasiteMutOp::NudgeDelay:
            {
                const int a = static_cast<int> (rng.nextFloat()
                                                * (ParasiteDNA::kDelaySlots - 1))
                              % (ParasiteDNA::kDelaySlots - 1);
                const int b = a + 1;
                const float move = 0.06f * (0.5f + rng.nextFloat());
                const int from = rng.nextFloat() < 0.5f ? a : b;
                const int to = from == a ? b : a;
                const float take = std::min (move, dna_.delayW[static_cast<size_t> (from)] * 0.8f);
                dna_.delayW[static_cast<size_t> (from)] -= take;
                dna_.delayW[static_cast<size_t> (to)] += take;
                normalise (dna_.delayW.data(), ParasiteDNA::kDelaySlots);
                capSlot (dna_.delayW.data(), ParasiteDNA::kDelaySlots, 0, 0.18f);
                return true;
            }

            case ParasiteMutOp::NudgeDur:
            {
                const int a = static_cast<int> (rng.nextFloat() * (ParasiteDNA::kDurSlots - 1))
                              % (ParasiteDNA::kDurSlots - 1);
                const int b = a + 1;
                const float move = 0.06f * (0.5f + rng.nextFloat());
                const int from = rng.nextFloat() < 0.5f ? a : b;
                const int to = from == a ? b : a;
                const float take = std::min (move, dna_.durW[static_cast<size_t> (from)] * 0.8f);
                dna_.durW[static_cast<size_t> (from)] -= take;
                dna_.durW[static_cast<size_t> (to)] += take;
                normalise (dna_.durW.data(), ParasiteDNA::kDurSlots);
                capSlot (dna_.durW.data(), ParasiteDNA::kDurSlots, 4, 0.12f);
                return true;
            }

            case ParasiteMutOp::TiltGap:
                // Bounded drift around birth: MUTATION 1 must still sound like this
                // parasite, and gapPreference must never become a density control.
                dna_.gapPreference = std::clamp (
                    dna_.gapPreference + (rng.nextFloat() < 0.5f ? -0.08f : 0.08f),
                    std::max (0.0f, gapPreferenceBirth_ - 0.20f),
                    std::min (1.0f, gapPreferenceBirth_ + 0.20f));
                return true;

            case ParasiteMutOp::TiltEcho:
                dna_.echoBias = std::clamp (
                    dna_.echoBias + (rng.nextFloat() < 0.5f ? -0.08f : 0.08f), 0.0f, 1.0f);
                return true;

            case ParasiteMutOp::TiltColour:
            {
                const float d = rng.nextFloat() < 0.5f ? -0.08f : 0.08f;
                if (rng.nextFloat() < 0.5f)
                    dna_.brightnessBias = std::clamp (dna_.brightnessBias + d, -1.0f, 1.0f);
                else
                    dna_.energyBias = std::clamp (dna_.energyBias + d, -1.0f, 1.0f);
                return true;
            }

            case ParasiteMutOp::StaleWindow:
                dna_.staleBeats = std::clamp (
                    dna_.staleBeats + (rng.nextFloat() < 0.5f ? -0.25f : 0.25f), 0.5f, 3.0f);
                return true;

            default:
                return false;
        }
    }

    void evolveAtBar (int bar, float mut) noexcept
    {
        if (mut <= 1.0e-4f)
        {
            dna_.lifespanBars = 1000000;
            prevMutation_ = mut;
            return;
        }
        // A live MUTATION rise shortens the remaining horizon; it never adds density.
        if (prevMutation_ >= 0.0f && mut > prevMutation_ + 0.2f)
            dna_.lifespanBars = std::min (dna_.lifespanBars, 4);
        prevMutation_ = mut;

        if (bar - dna_.birthBar < dna_.lifespanBars)
            return;

        const ParasiteMutOp op = drawMutOp (mut);
        const bool changed = applyMutOp (op);
        dna_.birthBar = bar;
        dna_.lifespanBars = parasiteLifespanBars (mut);
        dna_.lastOp = static_cast<uint8_t> (op);
        if (changed)
            ++dna_.generation;
    }

    void advanceMusical (double ppq, float mut) noexcept
    {
        const int bar = static_cast<int> (std::floor (ppq / 4.0));
        if (bar == lastBar_)
            return;

        if (seedDirty_)
        {
            // SEED change lands on a bar boundary; the sounding voice releases naturally.
            rebuildRng();
            birthDNA (bar);
            detector_.clearQueue();
            // A new personality has no history with this source yet.
            relationship_.onDiscontinuity();
            pendingActive_ = false;
            pendingSlid_ = false;
            seedDirty_ = false;
            lastBar_ = bar;
            return;
        }

        // Mid-insert / first attach / large forward jump: reconstruct DNA for absolute
        // musical time (same policy as seek). Analyzer stays fresh — no fabricated stimuli.
        if (lastBar_ < 0 || bar - lastBar_ > 64)
        {
            rebuildRng();
            const int replayFrom = std::max (0, bar - 4096);
            birthDNA (replayFrom);
            prevMutation_ = -1.0f;
            for (int b = replayFrom + 1; b <= bar; ++b)
                evolveAtBar (b, mut);
            lastBar_ = bar;
            return;
        }

        if (lastBar_ >= 0)
        {
            for (int b = lastBar_ + 1; b <= bar; ++b)
                evolveAtBar (b, mut);
        }
        lastBar_ = bar;
    }

    /** Seek / loop wrap: discontinuity, never a stimulus. */
    void handleSeek (double ppq) noexcept
    {
        features_.reset();
        detector_.reset();
        // The thread of the conversation is gone: history and state go back to
        // LURKING. The DNA does not — it is reconstructed for absolute musical
        // time below, exactly as in Stage 1.
        relationship_.onDiscontinuity();
        pendingActive_ = false;
        pendingSlid_ = false;
        voice_.stopSafely();
        warmupLeft_ = warmupN_;
        beatsSinceResponse_ = 4.0f;

        const int bar = std::max (0, static_cast<int> (std::floor (ppq / 4.0)));
        rebuildRng();
        birthDNA (bar);
        // Replay bar evolution so DNA matches absolute musical time, not arrival path.
        prevMutation_ = -1.0f;
        const float mut = mutSm_.current();
        const int replayFrom = std::max (0, bar - 4096);
        dna_.birthBar = replayFrom;
        for (int b = replayFrom + 1; b <= bar; ++b)
            evolveAtBar (b, mut);
        lastBar_ = bar;
        seedDirty_ = false;
    }

    // ---- response decision ---------------------------------------------------

    static int weightedPick (const float* w, int n,
                             pfl::generative::DeterministicRNG& rng) noexcept
    {
        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
            sum += std::max (0.0f, w[i]);
        const float u = rng.nextFloat() * (sum > 1.0e-6f ? sum : 1.0f);
        float acc = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            acc += std::max (0.0f, w[i]);
            if (u <= acc)
                return i;
        }
        for (int i = n - 1; i >= 0; --i)
            if (w[i] > 0.0f)
                return i;
        return 0;
    }

    /**
     * Restrict a draw to the half of a vocabulary the relationship prefers,
     * re-rolling at most once and only ever from the DNA weights. ATTACHED
     * spans the whole vocabulary, so it never costs a second draw and behaves
     * exactly as Stage 1 did.
     */
    static int applyTilt (int slot, const float* w, int n,
                          ParasiteRelationshipModel::SlotRange tilt,
                          pfl::generative::DeterministicRNG& rng) noexcept
    {
        if (slot >= tilt.lo && slot <= tilt.hi)
            return slot;
        float masked[8] = {};
        for (int i = 0; i < n && i < 8; ++i)
            masked[i] = (i >= tilt.lo && i <= tilt.hi) ? w[i] : 0.0f;
        return weightedPick (masked, n, rng);
    }

    int pickDelaySlot (bool sourceBusy) noexcept
    {
        int slot = weightedPick (dna_.delayW.data(), ParasiteDNA::kDelaySlots, delayRng_);
        if (sourceBusy && dna_.gapPreference > 0.55f && slot < 3)
        {
            // One re-roll toward the later half — still from DNA weights.
            float late[ParasiteDNA::kDelaySlots] = {
                0.0f, 0.0f, 0.0f,
                dna_.delayW[3], dna_.delayW[4], dna_.delayW[5]
            };
            slot = weightedPick (late, ParasiteDNA::kDelaySlots, delayRng_);
        }
        return applyTilt (slot, dna_.delayW.data(), ParasiteDNA::kDelaySlots,
                          relationship_.delayTilt(), delayRng_);
    }

    int pickDurationSlot (float hunger) noexcept
    {
        float w[ParasiteDNA::kDurSlots];
        for (int i = 0; i < ParasiteDNA::kDurSlots; ++i)
            w[i] = dna_.durW[static_cast<size_t> (i)];
        // High hunger nudges toward shorter answers so activity rises without smear.
        const float shift = 0.15f * std::clamp (hunger, 0.0f, 1.0f);
        const float take = (w[3] + w[4]) * shift;
        w[3] *= (1.0f - shift);
        w[4] *= (1.0f - shift);
        const float shortSum = w[0] + w[1] + w[2];
        if (shortSum > 1.0e-6f)
            for (int i = 0; i < 3; ++i)
                w[i] += take * (w[i] / shortSum);
        const int slot = weightedPick (w, ParasiteDNA::kDurSlots, durationRng_);
        return applyTilt (slot, w, ParasiteDNA::kDurSlots, relationship_.durationTilt(),
                          durationRng_);
    }

    /**
     * DNA politeness, stretched or relaxed by the relationship. This sits on
     * top of the HUNGER minimum gap and can only ever add to it — the state
     * has no way to make the parasite denser than HUNGER allows.
     */
    float recentGapBeats() const noexcept
    {
        const float dnaGap = 0.75f + (2.0f - 0.75f) * std::clamp (dna_.gapPreference, 0.0f, 1.0f);
        return dnaGap * relationship_.gapMultiplier();
    }

    /**
     * SOURCE_BUSY threshold on the fill probe. Deliberately high: negative space
     * is already guaranteed by HUNGER min-gap, DNA politeness and one-voice
     * arbitration, so this gate exists only to refuse answering into a genuine
     * wall of sound. Sparse drums never trip it; sustained beds trip it while
     * they sit at their own peak, which is what a gap-preferring DNA wants.
     */
    float sourceBusyThreshold() const noexcept
    {
        return 1.05f + (0.82f - 1.05f) * std::clamp (dna_.gapPreference, 0.0f, 1.0f);
    }

    void countSuppress (ParasiteSuppressReason r) noexcept
    {
        const auto i = static_cast<size_t> (r);
        if (i < suppress_.size())
            ++suppress_[i];
    }

    /**
     * Ordered gate: WEAK → STALE → VOICE_BUSY → RECENT_RESPONSE → HUNGER.
     * VOICE_BUSY keeps the event queued (it may still be answered late);
     * every other rejection consumes it. Accept RNG is drawn at most once
     * per surviving event, which keeps the stream partition independent.
     */
    void drainStimuli (float hunger) noexcept
    {
        StimulusEvent ev;
        while (detector_.peek (ev, absSample_))
        {
            if (ev.strength < dna_.minStimulusStrength)
            {
                detector_.dropFront();
                countSuppress (ParasiteSuppressReason::Weak);
                continue;
            }

            const int64_t dnaStale =
                static_cast<int64_t> (std::llround (dna_.staleBeats * samplesPerBeat_));
            if (absSample_ - ev.sampleIndex > dnaStale)
            {
                detector_.dropFront();
                countSuppress (ParasiteSuppressReason::Stale);
                continue;
            }

            if (voice_.isActive() || pendingActive_)
            {
                // Keep the event queued — it may still be answered late — but
                // count the block once, not once per sample.
                if (voiceBusyStim_ != ev.sampleIndex)
                {
                    voiceBusyStim_ = ev.sampleIndex;
                    countSuppress (ParasiteSuppressReason::VoiceBusy);
                }
                return;
            }

            if (beatsSinceResponse_ < recentGapBeats())
            {
                detector_.dropFront();
                countSuppress (ParasiteSuppressReason::RecentResponse);
                continue;
            }

            if (hunger < 1.0e-4f
                || beatsSinceResponse_ < parasiteMinGapBeats (hunger))
            {
                detector_.dropFront();
                countSuppress (ParasiteSuppressReason::Hunger);
                continue;
            }

            // HUNGER sets the appetite; the relationship only scales it, and
            // the 0.62 cap survives so ANSWERING is still selective.
            const float pBase = parasiteAcceptProbability (hunger, beatsSinceResponse_);
            const float pEff = std::clamp (pBase * relationship_.acceptGain(), 0.0f, 0.62f);
            const float draw = acceptRng_.nextFloat();
            if (draw >= pEff)
            {
                detector_.dropFront();
                countSuppress (draw < pBase ? ParasiteSuppressReason::Relationship
                                            : ParasiteSuppressReason::Hunger);
                continue;
            }

            schedule (ev, hunger);
            detector_.dropFront();
            return;
        }
    }

    void schedule (const StimulusEvent& ev, float hunger) noexcept
    {
        const bool busy = features_.frame().fill01 > 0.60f;
        pending_.stim = ev;
        pending_.delaySlot = pickDelaySlot (busy);
        pending_.durSlot = pickDurationSlot (hunger);
        pending_.onsetSample =
            ev.sampleIndex
            + static_cast<int64_t> (std::llround (
                kDelayBeats[static_cast<size_t> (pending_.delaySlot)] * samplesPerBeat_));
        pending_.durSamples = std::max<int64_t> (
            1, static_cast<int64_t> (std::llround (
                   kDurationBeats[static_cast<size_t> (pending_.durSlot)] * samplesPerBeat_)));
        pendingActive_ = true;
        pendingSlid_ = false;
    }

    void resolvePending() noexcept
    {
        // SOURCE_BUSY is probed at the scheduled onset, not at detection time.
        if (features_.frame().fill01 > sourceBusyThreshold())
        {
            if (! pendingSlid_ && pending_.delaySlot < ParasiteDNA::kDelaySlots - 1)
            {
                const int from = pending_.delaySlot;
                const int to = from + 1;
                pending_.delaySlot = to;
                pending_.onsetSample += static_cast<int64_t> (std::llround (
                    (kDelayBeats[static_cast<size_t> (to)]
                     - kDelayBeats[static_cast<size_t> (from)])
                    * samplesPerBeat_));
                pendingSlid_ = true;
                return;
            }
            pendingActive_ = false;
            pendingSlid_ = false;
            countSuppress (ParasiteSuppressReason::SourceBusy);
            return;
        }

        const auto& ev = pending_.stim;
        ParasiteVoice::NoteOn n;
        n.energy = std::clamp (ev.energy * 2.0f, 0.0f, 1.0f);
        n.brightness = std::clamp (ev.brightness, 0.0f, 1.0f);
        n.change = std::clamp (std::max (ev.strength, ev.change), 0.0f, 1.0f);
        n.balance = std::clamp (ev.balance, -1.0f, 1.0f);
        n.durationSamples = pending_.durSamples;
        n.levelScale = relationship_.levelScale();

        // Spatial stream is isolated from the schedule streams.
        const float jitter = (spatialRng_.nextFloat() * 2.0f - 1.0f) * 0.12f;
        n.balance = std::clamp (n.balance + jitter, -1.0f, 1.0f);

        voice_.noteOn (n, dna_);

        if (lastResponseOnsetSample_ >= 0)
        {
            const int64_t gap = pending_.onsetSample - lastResponseOnsetSample_;
            if (minResponseGapSamples_ < 0 || gap < minResponseGapSamples_)
                minResponseGapSamples_ = gap;
        }
        lastResponseOnsetSample_ = pending_.onsetSample;

        if (traceEnabled_ && responseTrace_.size() < responseTrace_.capacity())
        {
            ParasiteResponseEvent r;
            r.onsetSample = pending_.onsetSample;
            r.onsetBeat = beatClock_;
            r.delaySlot = pending_.delaySlot;
            r.durSlot = pending_.durSlot;
            r.durationBeats =
                static_cast<float> (kDurationBeats[static_cast<size_t> (pending_.durSlot)]);
            r.strength = ev.strength;
            r.brightness = ev.brightness;
            r.pan = n.balance;
            r.kind = ev.kind;
            r.state = static_cast<uint8_t> (relationship_.state());
            responseTrace_.push_back (r);
        }

        ++responseCount_;
        countSuppress (ParasiteSuppressReason::Accept);
        relationship_.noteResponse (ev.sampleIndex);
        beatsSinceResponse_ = 0.0f;
        pendingActive_ = false;
        pendingSlid_ = false;
    }

    struct PendingResponse
    {
        StimulusEvent stim {};
        int64_t onsetSample = 0;
        int64_t durSamples = 1;
        int delaySlot = 0;
        int durSlot = 1;
    };

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 1024;
    double samplesPerBeat_ = 22050.0;
    uint64_t masterSeed_ = 2002;
    bool seedDirty_ = false;

    ParamSmoother mixSm_, sensSm_, hungerSm_, mutSm_, outSm_;
    ParasiteFeatureExtractor features_;
    ParasiteStimulusDetector detector_;
    ParasiteRelationshipModel relationship_;
    ParasiteVoice voice_;
    DCBlocker dcL_, dcR_;
    SafetyLimiter limL_, limR_;

    pfl::generative::DeterministicRNG dnaBirthRng_ {}, dnaMutateRng_ {};
    pfl::generative::DeterministicRNG delayRng_ {}, durationRng_ {}, acceptRng_ {},
        spatialRng_ {};
    pfl::generative::DeterministicRNG voiceNoiseRng_ {};

    ParasiteDNA dna_ {};
    float gapPreferenceBirth_ = 0.5f;
    PendingResponse pending_ {};
    bool pendingActive_ = false;
    bool pendingSlid_ = false;

    int64_t absSample_ = 0;
    double beatClock_ = 0.0;
    double lastPpq_ = -1.0e9;
    bool lastPlaying_ = false;
    int lastBar_ = -1;
    int lastSensQ_ = -1;
    int warmupN_ = 2205;
    int warmupLeft_ = 2205;
    float beatsSinceResponse_ = 4.0f;
    float prevMutation_ = -1.0f;

    uint32_t responseCount_ = 0;
    int64_t voiceActiveSamples_ = 0;
    int64_t totalSamples_ = 0;
    int64_t lastResponseOnsetSample_ = -1;
    int64_t minResponseGapSamples_ = -1;
    int64_t voiceBusyStim_ = -1;
    std::array<uint32_t, static_cast<size_t> (ParasiteSuppressReason::Count)> suppress_ {};

    bool traceEnabled_ = false;
    std::vector<StimulusEvent> stimulusTrace_;
    std::vector<ParasiteResponseEvent> responseTrace_;
};

} // namespace pfl::dsp

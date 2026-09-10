#pragma once

#include "DeterministicRNG.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pfl::generative
{

/** Compact tonal ecology for Broken Conductor Stage 6 (not shared with DO Scale). */
enum class HarmonicFieldId : uint8_t
{
    Home = 0,   // D F G A C — distance 0
    Shadow = 1, // A C D E G — distance 1
    Lift = 2,   // G Bb C D F — distance 1
    Haze = 3,   // D F G Ab C — distance 1
    Wide = 4,   // C Eb F G Bb — distance 2
    Drift = 5,  // F Ab Bb C Eb — distance 3
    Count = 6
};

enum class JourneyState : uint8_t
{
    Settled = 0,
    Departing,
    Exploring,
    Returning
};

struct HarmonicField
{
    HarmonicFieldId id = HarmonicFieldId::Home;
    const char* name = "HOME";
    int rootPc = 2; // D
    int distanceFromHome = 0;
    std::array<int, 5> offsets { 0, 3, 5, 7, 10 }; // relative to rootPc
    std::array<float, 5> gravity { 1.35f, 1.05f, 0.75f, 1.15f, 0.70f };

    static constexpr int kNumDegrees = 5;

    int toMidi (int degree, int octave) const noexcept
    {
        while (degree < 0)
        {
            degree += kNumDegrees;
            --octave;
        }
        while (degree >= kNumDegrees)
        {
            degree -= kNumDegrees;
            ++octave;
        }
        return (octave + 1) * 12 + rootPc + offsets[static_cast<size_t> (degree)];
    }

    void fromMidi (int midiNote, int& degreeOut, int& octaveOut) const noexcept
    {
        int pc = ((midiNote % 12) + 12) % 12;
        octaveOut = (midiNote / 12) - 1;
        int bestDeg = 0;
        int bestDist = 99;
        for (int i = 0; i < kNumDegrees; ++i)
        {
            const int target = (rootPc + offsets[static_cast<size_t> (i)]) % 12;
            const int d = std::abs (pc - target);
            const int wrap = 12 - d;
            const int dist = d < wrap ? d : wrap;
            if (dist < bestDist)
            {
                bestDist = dist;
                bestDeg = i;
            }
        }
        degreeOut = bestDeg;
    }

    float gravityWeight (int degree) const noexcept
    {
        degree = ((degree % kNumDegrees) + kNumDegrees) % kNumDegrees;
        return gravity[static_cast<size_t> (degree)];
    }

    bool containsPc (int pc) const noexcept
    {
        pc = ((pc % 12) + 12) % 12;
        for (int o : offsets)
            if (((rootPc + o) % 12) == pc)
                return true;
        return false;
    }
};

inline HarmonicField fieldById (HarmonicFieldId id) noexcept
{
    switch (id)
    {
        case HarmonicFieldId::Shadow:
            return { HarmonicFieldId::Shadow, "SHADOW", 9, 1, { 0, 3, 5, 7, 10 }, { 1.30f, 1.00f, 1.10f, 0.80f, 0.75f } };
            // A C D E G → offsets from A: 0,3,5,7,10
        case HarmonicFieldId::Lift:
            return { HarmonicFieldId::Lift, "LIFT", 7, 1, { 0, 3, 5, 7, 10 }, { 1.30f, 1.05f, 0.85f, 1.10f, 0.80f } };
            // G Bb C D F
        case HarmonicFieldId::Haze:
            // D F G Ab C — offsets from D: 0,3,5,6,10
            return { HarmonicFieldId::Haze, "HAZE", 2, 1, { 0, 3, 5, 6, 10 }, { 1.40f, 1.05f, 0.70f, 0.90f, 0.75f } };
        case HarmonicFieldId::Wide:
            // C Eb F G Bb — offsets from C: 0,3,5,7,10
            return { HarmonicFieldId::Wide, "WIDE", 0, 2, { 0, 3, 5, 7, 10 }, { 1.25f, 1.00f, 0.85f, 1.05f, 0.70f } };
        case HarmonicFieldId::Drift:
            // F Ab Bb C Eb — offsets from F: 0,3,5,7,10
            return { HarmonicFieldId::Drift, "DRIFT", 5, 3, { 0, 3, 5, 7, 10 }, { 1.20f, 0.95f, 0.80f, 1.00f, 0.70f } };
        case HarmonicFieldId::Home:
        default:
            return { HarmonicFieldId::Home, "HOME", 2, 0, { 0, 3, 5, 7, 10 }, { 1.35f, 1.05f, 0.75f, 1.15f, 0.70f } };
    }
}

inline const char* journeyStateName (JourneyState s) noexcept
{
    switch (s)
    {
        case JourneyState::Settled: return "SETTLED";
        case JourneyState::Departing: return "DEPARTING";
        case JourneyState::Exploring: return "EXPLORING";
        case JourneyState::Returning: return "RETURNING";
        default: return "UNKNOWN";
    }
}

struct HarmonyTraceEvent
{
    double ppq = 0.0;
    HarmonicFieldId field = HarmonicFieldId::Home;
    JourneyState state = JourneyState::Settled;
    int distance = 0;
    const char* reason = "";
};

/**
 * Deterministic harmonic journey. Evaluates every 16 beats.
 * Isolated RNG streams: harmony.transition/duration/field/return.
 */
class HarmonicJourney
{
public:
    static constexpr double kEvalBeats = 16.0;
    static constexpr int kMinSettled = 32;
    static constexpr int kMinDeparting = 16;
    static constexpr int kMinExploring = 24;
    static constexpr int kMinReturning = 16;

    void reset (uint64_t masterSeed) noexcept
    {
        masterSeed_ = masterSeed;
        fieldId_ = HarmonicFieldId::Home;
        state_ = JourneyState::Settled;
        stateStartPpq_ = 0.0;
        awayStartPpq_ = -1.0;
        lastAdvancePpq_ = 0.0;
        lastEvalIndex_ = 0;
        locked_ = false;
        pressurePaused_ = false;
        collapseSuspend_ = false;
        traces_.clear();
        rebuildRng();
        field_ = fieldById (fieldId_);
    }

    void setLocked (bool locked) noexcept { locked_ = locked; }
    void setPressurePaused (bool paused) noexcept { pressurePaused_ = paused; }
    void setCollapseSuspend (bool s) noexcept { collapseSuspend_ = s; }

    const HarmonicField& field() const noexcept { return field_; }
    HarmonicFieldId fieldId() const noexcept { return fieldId_; }
    JourneyState state() const noexcept { return state_; }
    double beatsAway() const noexcept
    {
        if (awayStartPpq_ < 0.0)
            return 0.0;
        return std::max (0.0, lastAdvancePpq_ - awayStartPpq_);
    }
    double beatsInState() const noexcept
    {
        return std::max (0.0, lastAdvancePpq_ - stateStartPpq_);
    }
    const std::vector<HarmonyTraceEvent>& traces() const noexcept { return traces_; }
    void clearTraces() noexcept { traces_.clear(); }
    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }

    /** Advance musical time; evaluate transitions on 16-beat boundaries. */
    void advance (double fromPpq, double toPpq, float mutation, float density) noexcept
    {
        if (toPpq <= fromPpq)
            return;

        if (! locked_ && ! pressurePaused_ && ! collapseSuspend_
            && fieldId_ != HarmonicFieldId::Home && awayStartPpq_ < 0.0)
            awayStartPpq_ = fromPpq;

        // Integer eval indices avoid float near-miss skips (e.g. 31.99999999999752/16 == 2.0).
        const std::int64_t toIndex = static_cast<std::int64_t> (
            std::floor (toPpq / kEvalBeats + 1.0e-9));
        if (! locked_ && ! collapseSuspend_)
        {
            while (lastEvalIndex_ < toIndex)
            {
                ++lastEvalIndex_;
                const double boundary = static_cast<double> (lastEvalIndex_) * kEvalBeats;
                lastAdvancePpq_ = boundary;
                evaluateAt (boundary, mutation, density);
            }
        }
        lastAdvancePpq_ = toPpq;
    }

    /** Rare bounded nearby hop for manual MUTATE (external RNG already advanced for role pick). */
    bool tryManualNearbyHop (DeterministicRNG& rng, double ppq) noexcept
    {
        if (locked_ || collapseSuspend_)
            return false;
        if (rng.nextFloat() > 0.12f)
            return false;

        HarmonicFieldId candidates[4];
        int n = 0;
        collectNeighbors (fieldId_, state_ == JourneyState::Returning, mutationHint_, candidates, n);
        if (n <= 0)
            return false;
        const int pick = static_cast<int> (rng.nextFloat() * static_cast<float> (n)) % n;
        lastAdvancePpq_ = ppq;
        enterField (candidates[pick], ppq, "manualMutate");
        if (fieldId_ == HarmonicFieldId::Home)
            state_ = JourneyState::Settled;
        else if (state_ == JourneyState::Settled)
            state_ = JourneyState::Departing;
        return true;
    }

    void forceHomeSettled (double ppq, const char* reason) noexcept
    {
        fieldId_ = HarmonicFieldId::Home;
        field_ = fieldById (fieldId_);
        state_ = JourneyState::Settled;
        stateStartPpq_ = ppq;
        awayStartPpq_ = -1.0;
        lastAdvancePpq_ = ppq;
        record (ppq, reason);
    }

private:
    static uint64_t hashTag (const char* s) noexcept
    {
        uint64_t h = 0xcbf29ce484222325ull;
        while (*s)
        {
            h ^= static_cast<uint64_t> (*s++);
            h *= 0x100000001b3ull;
        }
        return h;
    }

    void rebuildRng() noexcept
    {
        transitionRng_ = DeterministicRNG::derived (masterSeed_, hashTag ("harmony.transition"));
        durationRng_ = DeterministicRNG::derived (masterSeed_, hashTag ("harmony.duration"));
        fieldRng_ = DeterministicRNG::derived (masterSeed_, hashTag ("harmony.field"));
        returnRng_ = DeterministicRNG::derived (masterSeed_, hashTag ("harmony.return"));
    }

    int minDwellBeats() const noexcept
    {
        switch (state_)
        {
            case JourneyState::Settled: return kMinSettled;
            case JourneyState::Departing: return kMinDeparting;
            case JourneyState::Exploring: return kMinExploring;
            case JourneyState::Returning: return kMinReturning;
            default: return kMinSettled;
        }
    }

    void collectNeighbors (HarmonicFieldId from, bool homeward, float mut,
                           HarmonicFieldId* out, int& n) const noexcept
    {
        n = 0;
        auto add = [&] (HarmonicFieldId id, bool allowFar) {
            if (id == from)
                return;
            const int d = fieldById (id).distanceFromHome;
            const int cur = fieldById (from).distanceFromHome;
            if (homeward && d > cur)
                return;
            if (! allowFar && d >= 3 && mut < 0.7f)
                return;
            out[n++] = id;
        };

        const bool allowFar = mut >= 0.7f;
        switch (from)
        {
            case HarmonicFieldId::Home:
                add (HarmonicFieldId::Shadow, allowFar);
                add (HarmonicFieldId::Lift, allowFar);
                add (HarmonicFieldId::Haze, allowFar);
                break;
            case HarmonicFieldId::Shadow:
                add (HarmonicFieldId::Home, allowFar);
                add (HarmonicFieldId::Wide, allowFar);
                add (HarmonicFieldId::Haze, allowFar);
                break;
            case HarmonicFieldId::Lift:
                add (HarmonicFieldId::Home, allowFar);
                add (HarmonicFieldId::Wide, allowFar);
                add (HarmonicFieldId::Drift, allowFar);
                break;
            case HarmonicFieldId::Haze:
                add (HarmonicFieldId::Home, allowFar);
                add (HarmonicFieldId::Shadow, allowFar);
                add (HarmonicFieldId::Wide, allowFar);
                break;
            case HarmonicFieldId::Wide:
                add (HarmonicFieldId::Shadow, allowFar);
                add (HarmonicFieldId::Lift, allowFar);
                add (HarmonicFieldId::Haze, allowFar);
                add (HarmonicFieldId::Drift, allowFar);
                add (HarmonicFieldId::Home, allowFar);
                break;
            case HarmonicFieldId::Drift:
                add (HarmonicFieldId::Wide, allowFar);
                add (HarmonicFieldId::Lift, allowFar);
                break;
            default:
                break;
        }
    }

    void enterField (HarmonicFieldId id, double ppq, const char* reason) noexcept
    {
        if (id == fieldId_)
            return;
        fieldId_ = id;
        field_ = fieldById (id);
        stateStartPpq_ = ppq;
        if (id == HarmonicFieldId::Home)
            awayStartPpq_ = -1.0;
        else if (awayStartPpq_ < 0.0)
            awayStartPpq_ = ppq;
        record (ppq, reason);
    }

    void record (double ppq, const char* reason) noexcept
    {
        if (! traceEnabled_)
            return;
        HarmonyTraceEvent e;
        e.ppq = ppq;
        e.field = fieldId_;
        e.state = state_;
        e.distance = field_.distanceFromHome;
        e.reason = reason;
        traces_.push_back (e);
    }

    void evaluateAt (double ppq, float mutation, float density) noexcept
    {
        mutationHint_ = mutation;
        const float mut = std::clamp (mutation, 0.0f, 1.0f);
        lastAdvancePpq_ = ppq;
        const double dwell = ppq - stateStartPpq_;
        if (dwell + 1.0e-9 < static_cast<double> (minDwellBeats()))
            return;

        const double away = beatsAway();

        // Return pressure while away
        if (fieldId_ != HarmonicFieldId::Home)
        {
            const float dist = static_cast<float> (field_.distanceFromHome);
            const float awayClamped = static_cast<float> (std::min (away, 128.0));
            float pressure = 0.08f + 0.12f * dist + 0.004f * awayClamped;
            pressure *= (1.0f - 0.35f * mut);
            pressure = std::clamp (pressure, 0.05f, 0.85f);
            if (returnRng_.nextFloat() < pressure)
            {
                if (state_ != JourneyState::Returning)
                {
                    state_ = JourneyState::Returning;
                    stateStartPpq_ = ppq;
                    record (ppq, "returnPressure");
                }
            }
        }

        if (state_ == JourneyState::Settled)
        {
            float leave = 0.04f + 0.16f * mut;
            leave *= (0.92f + 0.16f * std::clamp (density, 0.0f, 1.0f));
            if (dwell < 48.0 + 40.0 * (1.0 - mut))
                leave *= 0.55f;
            if (transitionRng_.nextFloat() >= leave)
                return;

            HarmonicFieldId cands[4];
            int n = 0;
            collectNeighbors (HarmonicFieldId::Home, false, mut, cands, n);
            if (n <= 0)
                return;
            (void) durationRng_.nextFloat();
            const int pick = static_cast<int> (fieldRng_.nextFloat() * static_cast<float> (n)) % n;
            state_ = JourneyState::Departing;
            enterField (cands[pick], ppq, "depart");
            return;
        }

        if (state_ == JourneyState::Departing)
        {
            if (dwell >= kMinDeparting)
            {
                if (returnRng_.nextFloat() < 0.25f * (1.0f - mut))
                {
                    state_ = JourneyState::Returning;
                    stateStartPpq_ = ppq;
                    record (ppq, "earlyReturn");
                }
                else
                {
                    state_ = JourneyState::Exploring;
                    stateStartPpq_ = ppq;
                    record (ppq, "explore");
                }
            }
            return;
        }

        if (state_ == JourneyState::Exploring)
        {
            float hop = 0.10f + 0.18f * mut;
            if (dwell < 32.0)
                hop *= 0.5f;
            if (transitionRng_.nextFloat() >= hop)
                return;

            HarmonicFieldId cands[8];
            int n = 0;
            collectNeighbors (fieldId_, false, mut, cands, n);
            if (n <= 0)
                return;
            (void) durationRng_.nextFloat();
            const int pick = static_cast<int> (fieldRng_.nextFloat() * static_cast<float> (n)) % n;
            enterField (cands[pick], ppq, "exploreHop");
            if (fieldId_ == HarmonicFieldId::Home)
                state_ = JourneyState::Settled;
            return;
        }

        if (state_ == JourneyState::Returning)
        {
            HarmonicFieldId cands[8];
            int n = 0;
            collectNeighbors (fieldId_, true, mut, cands, n);
            bool hasHome = false;
            for (int i = 0; i < n; ++i)
                if (cands[i] == HarmonicFieldId::Home)
                    hasHome = true;
            if (! hasHome && fieldId_ != HarmonicFieldId::Home && n < 8)
                cands[n++] = HarmonicFieldId::Home;
            if (n <= 0)
            {
                forceHomeSettled (ppq, "returnForce");
                return;
            }
            int bestD = 99;
            for (int i = 0; i < n; ++i)
                bestD = std::min (bestD, fieldById (cands[i]).distanceFromHome);
            int ties[8];
            int tn = 0;
            for (int i = 0; i < n; ++i)
                if (fieldById (cands[i]).distanceFromHome == bestD)
                    ties[tn++] = i;
            const int pick = ties[static_cast<int> (fieldRng_.nextFloat() * static_cast<float> (tn)) % tn];
            (void) durationRng_.nextFloat();
            enterField (cands[pick], ppq, "returnHop");
            if (fieldId_ == HarmonicFieldId::Home)
                state_ = JourneyState::Settled;
        }
    }

    uint64_t masterSeed_ = 1001;
    HarmonicFieldId fieldId_ = HarmonicFieldId::Home;
    HarmonicField field_{};
    JourneyState state_ = JourneyState::Settled;
    double stateStartPpq_ = 0.0;
    double awayStartPpq_ = -1.0;
    double lastAdvancePpq_ = 0.0;
    std::int64_t lastEvalIndex_ = 0;
    float mutationHint_ = 0.35f;
    bool locked_ = false;
    bool pressurePaused_ = false;
    bool collapseSuspend_ = false;
    bool traceEnabled_ = false;
    DeterministicRNG transitionRng_{};
    DeterministicRNG durationRng_{};
    DeterministicRNG fieldRng_{};
    DeterministicRNG returnRng_{};
    std::vector<HarmonyTraceEvent> traces_;
};

} // namespace pfl::generative

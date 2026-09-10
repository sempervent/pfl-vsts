#pragma once

#include "DCBlocker.h"
#include "ParamSmoother.h"
#include "SafetyLimiter.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pfl::dsp
{

struct PulseGateWindow
{
    uint16_t id = 0;
    int16_t startSlot = 0;
    uint8_t lengthSlots = 2;
};

struct PulseDNA
{
    static constexpr int kMaxBars = 4;
    static constexpr int kStepsPerBar = 16;
    static constexpr int kMaxCells = 64;
    static constexpr int kMaxWindows = 24;

    std::array<PulseGateWindow, kMaxWindows> windows {};
    int windowCount = 0;
    int lengthBars = 2;
    int phaseShiftSlots = 0;
    int generation = 0;
    int birthBar = 0;
    int lifespanBars = 6;
    uint32_t lastOp = 0;
    std::array<uint8_t, kMaxCells> mask {}; // 0 closed, 1 onset, 2 hold

    int lengthCells() const noexcept { return lengthBars * kStepsPerBar; }

    float occupancy() const noexcept
    {
        const int n = lengthCells();
        if (n <= 0) return 0.0f;
        int open = 0;
        for (int i = 0; i < n; ++i)
            if (mask[static_cast<size_t> (i)] != 0)
                ++open;
        return static_cast<float> (open) / static_cast<float> (n);
    }
};

enum class PulseRole : int8_t
{
    Anchor = 0,
    Skitter = 1,
    Ghost = 2,
    Count = 3
};

enum class PulseSuppressReason : uint8_t
{
    None = 0,
    Accept,
    Collision,
    GlobalBudget,
    Congestion,
    GapPreserve,
    RoleCooldown,
    RedundantOpen,
    SoloFilter,
    InteractionOff
};

struct PulseIntent
{
    int8_t role = -1;
    int startAbsSlot = 0;
    uint8_t lengthSlots = 1;
    float importance = 0.5f;
    float spatialTarget = 0.0f;
    uint8_t kind = 0; // 0 OPEN, 1 ANSWER, 2 ANTICIPATE, 3 HUNGER
};

struct PulseTraceEvent
{
    double beat = 0.0;
    std::string detail;
};

struct PulseOpenEvent
{
    double beat = 0.0;
    float durationBeats = 0.0f;
    float gain = 1.0f;
    float pan = 0.0f;
    int8_t role = -1;
};

/**
 * Pulse Colony Stage 2 — three PulseCells propose; ColonyArbiter accepts
 * into ONE gate/motion wet path. Algorithm v2.
 */
class PulseColonyEngine
{
public:
    static constexpr int kAlgorithmVersion = 2;
    static constexpr double kSlotBeats = 0.25; // sixteenth
    static constexpr int kNumRoles = static_cast<int> (PulseRole::Count);
    static constexpr int kMaxProposals = 8;
    static constexpr int kOccRingSlots = 64; // 16 beats

    void prepare (double sampleRate, int maxBlockSize = 1024) noexcept
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        maxBlockSize_ = std::max (64, maxBlockSize);
        mixSm_.prepare (sampleRate_, 0.05f);
        densSm_.prepare (sampleRate_, 0.08f);
        mutSm_.prepare (sampleRate_, 0.08f);
        motionSm_.prepare (sampleRate_, 0.05f);
        outSm_.prepare (sampleRate_, 0.05f);
        panSm_.prepare (sampleRate_, 0.08f);
        dcL_.prepare (sampleRate_);
        dcR_.prepare (sampleRate_);
        limL_.prepare (sampleRate_);
        limR_.prepare (sampleRate_);
        attN_ = std::max (1, static_cast<int> (std::lround (0.003 * sampleRate_)));
        relN_ = std::max (1, static_cast<int> (std::lround (0.004 * sampleRate_)));
        reset();
    }

    void reset() noexcept
    {
        gateEnv_ = 1.0f;
        gatePhase_ = 2;
        holdLeft_ = 0;
        lastPpq_ = -1.0e9;
        lastTransportPlaying_ = false;
        lastBar_ = -1;
        lastSlot_ = -1;
        lastPanBar_ = -1;
        resetColonyRuntime();
        rebuildRng();
        for (int r = 0; r < kNumRoles; ++r)
        {
            cells_[static_cast<size_t> (r)].prevDensity = -1.0f;
            cells_[static_cast<size_t> (r)].prevMutation = -1.0f;
            regenerateDNA (r, 0);
        }
        snapScheduler (0.0);
    }

    void setSeed (uint64_t seed) noexcept
    {
        const uint64_t s = seed == 0 ? 1ull : seed;
        if (s == masterSeed_)
            return;
        masterSeed_ = s;
        seedDirty_ = true;
        rebuildRng();
    }

    uint64_t seed() const noexcept { return masterSeed_; }

    void setMix (float v) noexcept { mixSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setDensity (float v) noexcept { densSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setMutation (float v) noexcept { mutSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setMotion (float v) noexcept { motionSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setOutput (float v) noexcept { outSm_.setTarget (std::clamp (v, 0.0f, 1.0f)); }

    void snapMacros() noexcept
    {
        mixSm_.setCurrentAndTarget (mixSm_.target());
        densSm_.setCurrentAndTarget (densSm_.target());
        mutSm_.setCurrentAndTarget (mutSm_.target());
        motionSm_.setCurrentAndTarget (motionSm_.target());
        outSm_.setCurrentAndTarget (outSm_.target());
        panSm_.setCurrentAndTarget (panSm_.target());
    }

    /** Stage 1 compat: Anchor DNA. */
    const PulseDNA& dna() const noexcept { return cells_[0].dna; }
    const PulseDNA& cellDna (int role) const noexcept
    {
        const int r = std::clamp (role, 0, kNumRoles - 1);
        return cells_[static_cast<size_t> (r)].dna;
    }

    int generation() const noexcept
    {
        int g = 0;
        for (int r = 0; r < kNumRoles; ++r)
            g += cells_[static_cast<size_t> (r)].dna.generation;
        return g;
    }

    float gateEnv() const noexcept { return gateEnv_; }

    /** Diagnostic: enable call/response, congestion, gap-preserve, hunger (default true). */
    void setInteractionEnabled (bool e) noexcept { interactionEnabled_ = e; }
    bool interactionEnabled() const noexcept { return interactionEnabled_; }

    /** Diagnostic: -1 = all roles, 0/1/2 = solo Anchor/Skitter/Ghost. */
    void setSoloRole (int roleOrNegativeForAll) noexcept
    {
        soloRole_ = (roleOrNegativeForAll < 0) ? -1 : std::clamp (roleOrNegativeForAll, 0, kNumRoles - 1);
    }
    int soloRole() const noexcept { return soloRole_; }

    int roleAcceptCount (int role) const noexcept
    {
        if (role < 0 || role >= kNumRoles) return 0;
        return roleAcceptCount_[static_cast<size_t> (role)];
    }

    float colonyOpenOccupancy() const noexcept
    {
        return static_cast<float> (occOpenCount_) / static_cast<float> (kOccRingSlots);
    }

    float ghostMedianGapBeats() const noexcept
    {
        if (ghostGapCount_ <= 0) return 0.0f;
        return ghostGapSum_ / static_cast<float> (ghostGapCount_);
    }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }
    const std::vector<PulseTraceEvent>& traces() const noexcept { return events_; }
    const std::vector<PulseOpenEvent>& opens() const noexcept { return opens_; }
    void clearTraces() noexcept { events_.clear(); opens_.clear(); }

    void forceRebuild (int bar = 0) noexcept
    {
        rebuildRng();
        for (int r = 0; r < kNumRoles; ++r)
        {
            cells_[static_cast<size_t> (r)].prevDensity = -1.0f;
            cells_[static_cast<size_t> (r)].prevMutation = -1.0f;
            regenerateDNA (r, bar);
        }
        resetColonyRuntime();
        snapScheduler (static_cast<double> (bar) * 4.0);
    }

    void process (float* left, float* right, int numSamples,
                  bool transportPlaying, double ppqStart, double bpm) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;

        const double safeBpm = bpm > 1.0 ? bpm : 120.0;
        const double beatsPerSample = (safeBpm / 60.0) / sampleRate_;

        if (lastPpq_ > -1.0e8)
        {
            const double jump = ppqStart - lastPpq_;
            const double maxBlockBeats =
                (static_cast<double> (maxBlockSize_) / sampleRate_) * (safeBpm / 60.0) * 2.5 + 0.05;
            if (jump < -0.01 || jump > maxBlockBeats)
            {
                reconstructAt (ppqStart, densSm_.current(), mutSm_.current());
                syncGateToPpq (ppqStart);
            }
        }

        if (! transportPlaying && lastTransportPlaying_)
            beginPassThroughRamp();
        if (transportPlaying && ! lastTransportPlaying_)
        {
            // Leave pass-through: arbiter owns the gate again.
            holdOwnerRole_ = -1;
            holdEndAbsSlot_ = -1;
            if (gatePhase_ == 1 || gatePhase_ == 2)
            {
                gatePhase_ = 3;
                gatePos_ = 0;
                pulseRelN_ = std::max (1, attN_);
            }
        }
        lastTransportPlaying_ = transportPlaying;

        for (int i = 0; i < numSamples; ++i)
        {
            const double ppq = ppqStart + static_cast<double> (i) * beatsPerSample;
            float inL = left[i];
            float inR = right[i];
            if (! std::isfinite (inL)) inL = 0.0f;
            if (! std::isfinite (inR)) inR = 0.0f;

            const float mix = mixSm_.getNext();
            const float dens = densSm_.getNext();
            const float mut = mutSm_.getNext();
            const float motion = motionSm_.getNext();
            const float outG = outSm_.getNext();

            if (transportPlaying)
            {
                advanceMusical (ppq, dens, mut, safeBpm);
                tickGate();
            }
            else
            {
                if (gateEnv_ < 0.999f)
                {
                    tickGate();
                    if (gatePhase_ == 0)
                    {
                        gatePhase_ = 1;
                        gatePos_ = 0;
                        pulseAttN_ = attN_;
                    }
                }
                else
                {
                    gateEnv_ = 1.0f;
                    gatePhase_ = 2;
                    holdLeft_ = std::max (holdLeft_, 1);
                }
            }

            float wetL = inL * gateEnv_;
            float wetR = inR * gateEnv_;

            const float pan = panSm_.getNext();
            applyMotion (wetL, wetR, motion, pan);

            wetL = dcL_.processSample (wetL);
            wetR = dcR_.processSample (wetR);
            wetL = limL_.processSample (wetL);
            wetR = limR_.processSample (wetR);

            float outL = inL * (1.0f - mix) + wetL * mix;
            float outR = inR * (1.0f - mix) + wetR * mix;
            outL = std::clamp (outL * outG, -0.99f, 0.99f);
            outR = std::clamp (outR * outG, -0.99f, 0.99f);
            if (! std::isfinite (outL)) outL = 0.0f;
            if (! std::isfinite (outR)) outR = 0.0f;
            left[i] = outL;
            right[i] = outR;
        }

        if (transportPlaying)
            lastPpq_ = ppqStart + static_cast<double> (numSamples) * beatsPerSample;
        else
            lastPpq_ = ppqStart;
    }

private:
    struct PulseCell
    {
        PulseDNA dna {};
        int nextWindowId = 1;
        float prevDensity = -1.0f;
        float prevMutation = -1.0f;
        pfl::generative::DeterministicRNG initialRng {}, mutateRng {}, durationRng {}, spatialRng {};
    };

    struct ColonySnapshot
    {
        float dens = 0.5f;
        float mut = 0.35f;
        float congestion01 = 0.0f;
        int gapLengthSlots = 0;
        float budgetRemaining = 1.0f;
        int lastAcceptedRole = -1;
        std::array<float, kNumRoles> beatsSinceRole {};
        bool holding = false;
        int holdOwner = -1;
        int holdEndSlot = -1;
        bool interaction = true;
        int solo = -1;
        bool callLive = false;
        int callKind = 0;
        int callExpireSlot = -1;
        int callCaller = -1;
    };

    static uint64_t hashTag (const char* s) noexcept
    {
        uint64_t h = 0xcbf29ce484222325ull;
        for (const char* p = s; *p; ++p)
        {
            h ^= static_cast<uint64_t> (static_cast<uint8_t> (*p));
            h *= 0x100000001b3ull;
        }
        return h;
    }

    static const char* roleName (int role) noexcept
    {
        switch (role)
        {
            case 0: return "ANCHOR";
            case 1: return "SKITTER";
            case 2: return "GHOST";
            default: return "?";
        }
    }

    static const char* suppressName (PulseSuppressReason r) noexcept
    {
        switch (r)
        {
            case PulseSuppressReason::None: return "NONE";
            case PulseSuppressReason::Accept: return "ACCEPT";
            case PulseSuppressReason::Collision: return "COLLISION";
            case PulseSuppressReason::GlobalBudget: return "GLOBAL_BUDGET";
            case PulseSuppressReason::Congestion: return "CONGESTION";
            case PulseSuppressReason::GapPreserve: return "GAP_PRESERVE";
            case PulseSuppressReason::RoleCooldown: return "ROLE_COOLDOWN";
            case PulseSuppressReason::RedundantOpen: return "REDUNDANT_OPEN";
            case PulseSuppressReason::SoloFilter: return "SOLO_FILTER";
            case PulseSuppressReason::InteractionOff: return "INTERACTION_OFF";
        }
        return "NONE";
    }

    static float mutScale (int role) noexcept
    {
        switch (role)
        {
            case 0: return 0.30f;
            case 1: return 0.90f;
            case 2: return 1.25f;
            default: return 1.0f;
        }
    }

    static float colonyBudgetMid (float dens) noexcept
    {
        dens = std::clamp (dens, 0.0f, 1.0f);
        const float x[] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f };
        const float y[] = { 0.12f, 0.22f, 0.38f, 0.52f, 0.60f };
        for (int i = 0; i < 4; ++i)
        {
            if (dens <= x[i + 1])
            {
                const float t = (dens - x[i]) / (x[i + 1] - x[i]);
                return y[i] + (y[i + 1] - y[i]) * t;
            }
        }
        return y[4];
    }

    static void roleShares (float dens, float& a, float& s, float& g) noexcept
    {
        dens = std::clamp (dens, 0.0f, 1.0f);
        // Anchor keeps majority share at mid dens; Skitter ramps; Ghost stays minority.
        a = 0.92f + (0.45f - 0.92f) * dens;
        s = 0.05f + (0.40f - 0.05f) * dens;
        g = 0.03f + (0.15f - 0.03f) * dens;
        const float sum = a + s + g;
        a /= sum; s /= sum; g /= sum;
    }

    static int voidMinSlots (float dens) noexcept
    {
        if (dens <= 0.25f) return 4;
        if (dens <= 0.50f) return 3;
        if (dens <= 0.75f) return 2;
        return 1;
    }

    void rebuildRng() noexcept
    {
        static constexpr const char* roles[] = { "anchor", "skitter", "ghost" };
        static constexpr const char* purposes[] = { "initial", "mutation", "duration", "spatial" };
        for (int r = 0; r < kNumRoles; ++r)
        {
            auto& c = cells_[static_cast<size_t> (r)];
            char tag[64];
            std::snprintf (tag, sizeof (tag), "%s/%s", roles[r], purposes[0]);
            c.initialRng = pfl::generative::DeterministicRNG::derived (masterSeed_, hashTag (tag));
            std::snprintf (tag, sizeof (tag), "%s/%s", roles[r], purposes[1]);
            c.mutateRng = pfl::generative::DeterministicRNG::derived (masterSeed_, hashTag (tag));
            std::snprintf (tag, sizeof (tag), "%s/%s", roles[r], purposes[2]);
            c.durationRng = pfl::generative::DeterministicRNG::derived (masterSeed_, hashTag (tag));
            std::snprintf (tag, sizeof (tag), "%s/%s", roles[r], purposes[3]);
            c.spatialRng = pfl::generative::DeterministicRNG::derived (masterSeed_, hashTag (tag));
        }
        arbiterRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, hashTag ("colony/arbitrate"));
    }

    void resetColonyRuntime() noexcept
    {
        congestionEma_ = 0.0f;
        gapLengthSlots_ = 0;
        // Start not artificially hungry (esp. Ghost); pressure accumulates in beats.
        beatsSinceRole_.fill (0.0f);
        beatsSinceRole_[2] = 4.0f;
        budgetRemaining_ = 1.0f;
        lastAcceptedRole_ = -1;
        holdOwnerRole_ = -1;
        holdEndAbsSlot_ = -1;
        callLive_ = false;
        callKind_ = 0;
        callExpireSlot_ = -1;
        callCaller_ = -1;
        occRing_.fill (0);
        occRingPos_ = 0;
        occOpenCount_ = 0;
        roleAcceptCount_.fill (0);
        roleProposeCount_.fill (0);
        ghostGapSum_ = 0.0f;
        ghostGapCount_ = 0;
        lastGhostAcceptBeat_ = -1.0f;
        events_.clear();
        opens_.clear();
    }

    void snapScheduler (double ppq) noexcept
    {
        lastSlot_ = static_cast<int> (std::floor (ppq / kSlotBeats)) - 1;
        lastBar_ = static_cast<int> (std::floor (ppq / 4.0)) - 1;
    }

    void beginPassThroughRamp() noexcept
    {
        gatePhase_ = 1;
        gatePos_ = 0;
        pulseAttN_ = attN_;
        pulseRelN_ = relN_;
        holdLeft_ = std::max (1, static_cast<int> (0.5 * sampleRate_));
    }

    void syncGateToPpq (double /*ppq*/) noexcept
    {
        // After reconstruct, land closed unless mid-hold would require full replay.
        // Safe: idle until next accepted onset.
        gatePhase_ = 0;
        gateEnv_ = 0.0f;
        holdLeft_ = 0;
        holdOwnerRole_ = -1;
        holdEndAbsSlot_ = -1;
    }

    int lifespanBarsFor (int role, float mut) noexcept
    {
        auto& rng = cells_[static_cast<size_t> (role)].mutateRng;
        if (mut <= 1.0e-4f)
            return 1000000;
        const float scaled = std::clamp (mut * mutScale (role), 0.0f, 1.0f);
        const int lo = std::max (1, static_cast<int> (std::lround (2.0 - scaled)));
        const int hi = std::max (lo, static_cast<int> (std::lround (3.0 + 10.0 * (1.0 - scaled))));
        const float u = rng.nextFloat();
        return lo + static_cast<int> (u * static_cast<float> (hi - lo + 1));
    }

    void rebuildMask (int role) noexcept
    {
        auto& dna = cells_[static_cast<size_t> (role)].dna;
        const int n = dna.lengthCells();
        dna.mask.fill (0);
        for (int w = 0; w < dna.windowCount; ++w)
        {
            const auto& win = dna.windows[static_cast<size_t> (w)];
            for (int k = 0; k < win.lengthSlots; ++k)
            {
                int s = (win.startSlot + k) % n;
                if (s < 0) s += n;
                dna.mask[static_cast<size_t> (s)] = (k == 0) ? 1 : 2;
            }
        }
    }

    int drawLength (int role, float dens) noexcept
    {
        auto& rng = cells_[static_cast<size_t> (role)].durationRng;
        static constexpr int lens[] = { 1, 2, 4, 8 };
        float w[4];
        if (role == 0) // Anchor: {4,8} primary
        {
            w[0] = 0.00f; w[1] = 0.12f; w[2] = 0.48f; w[3] = 0.40f;
        }
        else if (role == 1) // Skitter: {1,2}
        {
            w[0] = 0.55f; w[1] = 0.35f; w[2] = 0.10f; w[3] = 0.00f;
            if (dens < 0.35f) { w[0] = 0.45f; w[1] = 0.45f; w[2] = 0.10f; }
        }
        else // Ghost: {2,4} primary, occasional 8, rare 1
        {
            w[0] = 0.08f; w[1] = 0.42f; w[2] = 0.40f; w[3] = 0.10f;
        }
        float u = rng.nextFloat();
        for (int i = 0; i < 4; ++i)
        {
            u -= w[i];
            if (u <= 0.0f) return lens[i];
        }
        return role == 0 ? 4 : 2;
    }

    int preferredStart (int role, float dens) noexcept
    {
        auto& rng = cells_[static_cast<size_t> (role)].initialRng;
        static constexpr int prefs[] = { 2, 3, 6, 7, 10, 11, 14, 15, 4, 12, 1, 5, 9, 13, 8, 0 };
        const float allowDown = dens > 0.85f ? 0.35f : (role == 0 ? 0.12f : 0.06f);
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const int idx = static_cast<int> (rng.nextFloat() * 16.0f) % 16;
            const int s = prefs[idx];
            if (s % 16 == 0 && rng.nextFloat() > allowDown)
                continue;
            return s;
        }
        return role == 1 ? 3 : 6;
    }

    bool overlaps (int role, int start, int len, int ignore = -1) const noexcept
    {
        const auto& dna = cells_[static_cast<size_t> (role)].dna;
        const int n = dna.lengthCells();
        for (int w = 0; w < dna.windowCount; ++w)
        {
            if (w == ignore) continue;
            const auto& o = dna.windows[static_cast<size_t> (w)];
            for (int a = 0; a < len; ++a)
            {
                const int sa = (start + a) % n;
                for (int b = 0; b < o.lengthSlots; ++b)
                {
                    const int sb = (o.startSlot + b) % n;
                    if (sa == sb) return true;
                }
            }
        }
        return false;
    }

    int pickPhraseBars (int role) noexcept
    {
        auto& rng = cells_[static_cast<size_t> (role)].initialRng;
        const float ub = rng.nextFloat();
        if (role == 0) // Anchor 2–4 bias 2–3
        {
            if (ub < 0.08f) return 1;
            if (ub < 0.55f) return 2;
            if (ub < 0.85f) return 3;
            return 4;
        }
        if (role == 1) // Skitter 1–2 bias 1
        {
            if (ub < 0.65f) return 1;
            if (ub < 0.95f) return 2;
            return 3;
        }
        // Ghost 2–4 bias 2, sparse
        if (ub < 0.10f) return 1;
        if (ub < 0.55f) return 2;
        if (ub < 0.85f) return 3;
        return 4;
    }

    void regenerateDNA (int role, int bar) noexcept
    {
        float dens = densSm_.current();
        if (dens < 0.0f) dens = densSm_.target();
        float shareA, shareS, shareG;
        roleShares (dens, shareA, shareS, shareG);
        const float shares[] = { shareA, shareS, shareG };
        const float targetOcc = std::max (0.02f, colonyBudgetMid (dens) * shares[role]);

        auto& cell = cells_[static_cast<size_t> (role)];
        auto& dna = cell.dna;
        dna = {};
        dna.lengthBars = pickPhraseBars (role);
        dna.phaseShiftSlots = static_cast<int> (cell.initialRng.nextFloat() * 16.0f) % 16;
        dna.generation = 0;
        dna.birthBar = bar;
        const float mut = mutSm_.current() >= 0.0f ? mutSm_.current() : mutSm_.target();
        dna.lifespanBars = lifespanBarsFor (role, mut);
        cell.nextWindowId = 1;

        const int n = dna.lengthCells();
        int budget = std::max (1, static_cast<int> (std::lround (targetOcc * static_cast<float> (n))));
        if (role == 2) // Ghost: keep DNA sparse
            budget = std::max (1, std::min (budget, std::max (1, n / 12)));

        int guard = 0;
        while (budget > 0 && dna.windowCount < PulseDNA::kMaxWindows && guard++ < 64)
        {
            int len = drawLength (role, dens);
            len = std::min (len, budget);
            len = std::max (1, len);
            int start = preferredStart (role, dens) % n;
            bool placed = false;
            for (int t = 0; t < 12; ++t)
            {
                const int s = (start + t * 3) % n;
                if (! overlaps (role, s, len))
                {
                    auto& w = dna.windows[static_cast<size_t> (dna.windowCount++)];
                    w.id = static_cast<uint16_t> (cell.nextWindowId++);
                    w.startSlot = static_cast<int16_t> (s);
                    w.lengthSlots = static_cast<uint8_t> (len);
                    budget -= len;
                    placed = true;
                    break;
                }
            }
            if (! placed)
                break;
        }
        rebuildMask (role);
        if (dna.occupancy() > 0.90f && dna.windowCount > 1)
        {
            --dna.windowCount;
            rebuildMask (role);
        }
        pushTrace (static_cast<double> (bar) * 4.0,
                   role, PulseSuppressReason::None, "DNA_BIRTH");
    }

    void reconstructAt (double ppq, float dens, float mut) noexcept
    {
        densSm_.setCurrentAndTarget (dens);
        mutSm_.setCurrentAndTarget (mut);
        rebuildRng();
        const int endSlot = std::max (0, static_cast<int> (std::floor (ppq / kSlotBeats)));
        for (int r = 0; r < kNumRoles; ++r)
        {
            cells_[static_cast<size_t> (r)].prevDensity = -1.0f;
            cells_[static_cast<size_t> (r)].prevMutation = -1.0f;
            regenerateDNA (r, 0);
        }

        // Replay bar evolution + arbitration so colony snapshot matches absolute PPQ.
        const bool prevTrace = traceEnabled_;
        traceEnabled_ = false;
        resetColonyRuntime();
        int simLastBar = -1;
        // Replay slots strictly before landing PPQ; snapScheduler lands before current slot.
        for (int s = 0; s < endSlot; ++s)
        {
            const int b = s / PulseDNA::kStepsPerBar;
            if (b != simLastBar)
            {
                if (simLastBar >= 0)
                {
                    for (int bb = simLastBar + 1; bb <= b; ++bb)
                        for (int r = 0; r < kNumRoles; ++r)
                            evolveAtBar (r, bb, dens, mut, true);
                }
                simLastBar = b;
            }
            applySlot (s, dens, mut, 120.0);
        }
        traceEnabled_ = prevTrace;

        snapScheduler (ppq);
        seedDirty_ = false;
    }

    enum class MutOp : uint8_t
    {
        Stay = 0, NudgeStart, Stretch, Split, Merge, SwapVoid, PhaseJog, BirthCull
    };

    MutOp drawMutOp (int role, float mut) noexcept
    {
        auto& rng = cells_[static_cast<size_t> (role)].mutateRng;
        if (mut <= 1.0e-4f)
            return MutOp::Stay;
        const float scaled = std::clamp (mut * mutScale (role), 0.0f, 1.0f);
        const float u = rng.nextFloat();
        const float stayP = 0.15f + 0.40f * (1.0f - scaled);
        if (u < stayP) return MutOp::Stay;
        const float v = (u - stayP) / std::max (1.0e-4f, 1.0f - stayP);
        if (role == 0)
        {
            if (v < 0.35f) return MutOp::NudgeStart;
            if (v < 0.65f) return MutOp::Stretch;
            if (v < 0.80f) return MutOp::SwapVoid;
            if (v < 0.90f) return MutOp::PhaseJog;
            return MutOp::BirthCull;
        }
        if (role == 1)
        {
            if (v < 0.28f) return MutOp::NudgeStart;
            if (v < 0.48f) return MutOp::Split;
            if (v < 0.68f) return MutOp::SwapVoid;
            if (v < 0.82f) return MutOp::Stretch;
            if (v < 0.92f) return MutOp::Merge;
            return MutOp::BirthCull;
        }
        if (v < 0.30f) return MutOp::NudgeStart;
        if (v < 0.50f) return MutOp::PhaseJog;
        if (v < 0.70f) return MutOp::SwapVoid;
        if (v < 0.85f) return MutOp::BirthCull;
        return MutOp::Stretch;
    }

    bool applyMutOp (int role, MutOp op) noexcept
    {
        auto& cell = cells_[static_cast<size_t> (role)];
        auto& dna = cell.dna;
        auto& rng = cell.mutateRng;
        if (op == MutOp::Stay || dna.windowCount <= 0)
            return false;
        const int n = dna.lengthCells();
        const int wi = static_cast<int> (rng.nextFloat() * static_cast<float> (dna.windowCount))
                       % dna.windowCount;
        auto& w = dna.windows[static_cast<size_t> (wi)];
        switch (op)
        {
            case MutOp::Stay:
                return false;
            case MutOp::NudgeStart:
            {
                const int d = rng.nextFloat() < 0.5f ? -1 : 1;
                const int ns = (w.startSlot + d + n) % n;
                if (! overlaps (role, ns, w.lengthSlots, wi))
                    w.startSlot = static_cast<int16_t> (ns);
                break;
            }
            case MutOp::Stretch:
            {
                static constexpr int lens[] = { 1, 2, 4, 8 };
                int idx = 0;
                for (int i = 0; i < 4; ++i)
                    if (lens[i] == w.lengthSlots) idx = i;
                // Ghost prefers placement over duration change
                if (role == 2 && rng.nextFloat() < 0.55f)
                    return applyMutOp (role, MutOp::NudgeStart);
                idx = std::clamp (idx + (rng.nextFloat() < 0.5f ? -1 : 1), 0, 3);
                if (role == 0 && lens[idx] == 1) idx = 1;
                if (role == 1 && lens[idx] == 8) idx = 2;
                const int nl = lens[idx];
                if (! overlaps (role, w.startSlot, nl, wi))
                    w.lengthSlots = static_cast<uint8_t> (nl);
                break;
            }
            case MutOp::Split:
            {
                if (w.lengthSlots < 2 || dna.windowCount >= PulseDNA::kMaxWindows)
                    return false;
                const int left = std::max (1, w.lengthSlots / 2);
                const int right = w.lengthSlots - left;
                w.lengthSlots = static_cast<uint8_t> (left);
                auto& nw = dna.windows[static_cast<size_t> (dna.windowCount++)];
                nw.id = static_cast<uint16_t> (cell.nextWindowId++);
                nw.startSlot = static_cast<int16_t> ((w.startSlot + left) % n);
                nw.lengthSlots = static_cast<uint8_t> (right);
                break;
            }
            case MutOp::Merge:
            {
                if (dna.windowCount < 2) return false;
                int other = (wi + 1) % dna.windowCount;
                auto& o = dna.windows[static_cast<size_t> (other)];
                const int gap = (o.startSlot - (w.startSlot + w.lengthSlots) + n) % n;
                if (gap > 2) return false;
                w.lengthSlots = static_cast<uint8_t> (
                    std::min (8, w.lengthSlots + gap + o.lengthSlots));
                dna.windows[static_cast<size_t> (other)] = dna.windows[static_cast<size_t> (dna.windowCount - 1)];
                --dna.windowCount;
                break;
            }
            case MutOp::SwapVoid:
            {
                const int d = rng.nextFloat() < 0.5f ? -2 : 2;
                const int ns = (w.startSlot + d + n) % n;
                if (! overlaps (role, ns, w.lengthSlots, wi))
                    w.startSlot = static_cast<int16_t> (ns);
                break;
            }
            case MutOp::PhaseJog:
                dna.phaseShiftSlots = (dna.phaseShiftSlots + (rng.nextFloat() < 0.5f ? -1 : 1) + 16) % 16;
                break;
            case MutOp::BirthCull:
            {
                float shareA, shareS, shareG;
                roleShares (densSm_.current(), shareA, shareS, shareG);
                const float shares[] = { shareA, shareS, shareG };
                const float mid = colonyBudgetMid (densSm_.current());
                const float lo = mid * shares[role] * 0.70f;
                const float hi = mid * shares[role] * 1.35f;
                if (dna.occupancy() > hi && dna.windowCount > 1)
                {
                    int best = 0;
                    for (int i = 1; i < dna.windowCount; ++i)
                        if (dna.windows[static_cast<size_t> (i)].lengthSlots
                            < dna.windows[static_cast<size_t> (best)].lengthSlots)
                            best = i;
                    dna.windows[static_cast<size_t> (best)] =
                        dna.windows[static_cast<size_t> (dna.windowCount - 1)];
                    --dna.windowCount;
                }
                else if (dna.occupancy() < lo && dna.windowCount < PulseDNA::kMaxWindows)
                {
                    const int len = drawLength (role, densSm_.current());
                    const int s = preferredStart (role, densSm_.current()) % n;
                    if (! overlaps (role, s, len))
                    {
                        auto& nw = dna.windows[static_cast<size_t> (dna.windowCount++)];
                        nw.id = static_cast<uint16_t> (cell.nextWindowId++);
                        nw.startSlot = static_cast<int16_t> (s);
                        nw.lengthSlots = static_cast<uint8_t> (len);
                    }
                }
                break;
            }
        }
        rebuildMask (role);
        dna.lastOp = static_cast<uint32_t> (op);
        return true;
    }

    void evolveAtBar (int role, int bar, float dens, float mut, bool silent) noexcept
    {
        auto& cell = cells_[static_cast<size_t> (role)];
        auto& dna = cell.dna;

        if (cell.prevDensity >= 0.0f)
        {
            const float dd = dens - cell.prevDensity;
            if (std::abs (dd) >= 0.30f)
            {
                regenerateDNA (role, bar);
                if (! silent)
                    pushTrace (static_cast<double> (bar) * 4.0, role,
                               PulseSuppressReason::None, "DENSITY_REBORN");
            }
            else if (std::abs (dd) >= 0.15f)
            {
                applyMutOp (role, MutOp::BirthCull);
                applyMutOp (role, MutOp::Stretch);
                if (! silent)
                    pushTrace (static_cast<double> (bar) * 4.0, role,
                               PulseSuppressReason::None, "DENSITY_ADAPT");
            }
        }
        cell.prevDensity = dens;

        if (mut <= 1.0e-4f)
        {
            dna.lifespanBars = 1000000;
            cell.prevMutation = mut;
            return;
        }
        if (cell.prevMutation >= 0.0f && mut > cell.prevMutation + 0.2f)
            dna.lifespanBars = std::min (dna.lifespanBars, 3);
        cell.prevMutation = mut;

        if (bar - dna.birthBar < dna.lifespanBars)
            return;

        const MutOp op = drawMutOp (role, mut);
        if (op == MutOp::Stay || ! applyMutOp (role, op))
        {
            dna.birthBar = bar;
            dna.lifespanBars = lifespanBarsFor (role, mut);
            if (! silent)
                pushTrace (static_cast<double> (bar) * 4.0, role,
                           PulseSuppressReason::None, "STAY");
            return;
        }
        ++dna.generation;
        dna.birthBar = bar;
        dna.lifespanBars = lifespanBarsFor (role, mut);
        if (! silent)
            pushTrace (static_cast<double> (bar) * 4.0, role,
                       PulseSuppressReason::None, "MUTATE");
    }

    void advanceMusical (double ppq, float dens, float mut, double bpm) noexcept
    {
        if (seedDirty_)
        {
            const int bar = static_cast<int> (std::floor (ppq / 4.0));
            if (bar != lastBar_)
            {
                for (int r = 0; r < kNumRoles; ++r)
                    regenerateDNA (r, bar);
                resetColonyRuntime();
                seedDirty_ = false;
            }
        }

        const int bar = static_cast<int> (std::floor (ppq / 4.0));
        if (bar != lastBar_)
        {
            if (lastBar_ >= 0)
            {
                const int from = lastBar_ + 1;
                for (int b = from; b <= bar; ++b)
                {
                    if (b - lastBar_ > 64)
                        break;
                    for (int r = 0; r < kNumRoles; ++r)
                        evolveAtBar (r, b, dens, mut, false);
                }
            }
            lastBar_ = bar;
        }

        const int slot = static_cast<int> (std::floor (ppq / kSlotBeats));
        if (slot == lastSlot_)
            return;
        if (lastSlot_ >= 0 && slot < lastSlot_)
        {
            lastSlot_ = slot;
            return;
        }
        const int from = lastSlot_ < 0 ? slot : lastSlot_ + 1;
        for (int s = from; s <= slot; ++s)
        {
            if (lastSlot_ >= 0 && s - lastSlot_ > 64)
                break;
            applySlot (s, dens, mut, bpm);
        }
        lastSlot_ = slot;
        updatePanTarget (ppq);
    }

    ColonySnapshot makeSnapshot (int absSlot, float dens, float mut) const noexcept
    {
        ColonySnapshot snap;
        snap.dens = dens;
        snap.mut = mut;
        snap.congestion01 = congestionEma_;
        snap.gapLengthSlots = gapLengthSlots_;
        snap.budgetRemaining = budgetRemaining_;
        snap.lastAcceptedRole = lastAcceptedRole_;
        snap.beatsSinceRole = beatsSinceRole_;
        snap.holding = (holdEndAbsSlot_ >= absSlot && holdOwnerRole_ >= 0);
        snap.holdOwner = holdOwnerRole_;
        snap.holdEndSlot = holdEndAbsSlot_;
        snap.interaction = interactionEnabled_;
        snap.solo = soloRole_;
        snap.callLive = callLive_ && absSlot <= callExpireSlot_;
        snap.callKind = callKind_;
        snap.callExpireSlot = callExpireSlot_;
        snap.callCaller = callCaller_;
        return snap;
    }

    float roleSpatialTarget (int role) noexcept
    {
        auto& rng = cells_[static_cast<size_t> (role)].spatialRng;
        if (role == 0)
            return (rng.nextFloat() * 2.0f - 1.0f) * 0.25f;
        if (role == 1)
            return (rng.nextFloat() < 0.5f ? -1.0f : 1.0f) * (0.35f + rng.nextFloat() * 0.35f);
        return (rng.nextFloat() < 0.5f ? -1.0f : 1.0f) * (0.55f + rng.nextFloat() * 0.40f);
    }

    void proposeFromDna (int role, int absSlot, const ColonySnapshot& snap,
                         std::array<PulseIntent, kMaxProposals>& props, int& nProp) noexcept
    {
        if (snap.solo >= 0 && snap.solo != role)
            return;
        const auto& dna = cells_[static_cast<size_t> (role)].dna;
        const int n = dna.lengthCells();
        if (n <= 0) return;
        int local = (absSlot + dna.phaseShiftSlots) % n;
        if (local < 0) local += n;
        if (dna.mask[static_cast<size_t> (local)] != 1)
            return;

        // Soft dens presence after onset: Skitter/Ghost are not full Stage-1 streams at low dens.
        if (role == 1)
        {
            const float p = std::clamp ((snap.dens - 0.10f) / 0.55f, 0.08f, 1.0f);
            if (arbiterRng_.nextFloat() > p)
                return;
        }
        else if (role == 2)
        {
            const float p = std::clamp ((snap.dens - 0.25f) / 0.60f, 0.05f, 0.85f);
            if (arbiterRng_.nextFloat() > p)
                return;
        }

        int len = 1;
        for (int w = 0; w < dna.windowCount; ++w)
        {
            const auto& win = dna.windows[static_cast<size_t> (w)];
            if ((win.startSlot % n + n) % n == local)
            {
                len = win.lengthSlots;
                break;
            }
        }

        if (nProp >= kMaxProposals)
            return;

        PulseIntent intent;
        intent.role = static_cast<int8_t> (role);
        intent.startAbsSlot = absSlot;
        intent.lengthSlots = static_cast<uint8_t> (len);
        intent.importance = (role == 0) ? 0.90f : (role == 1 ? 0.55f : 0.35f);
        intent.spatialTarget = roleSpatialTarget (role);
        intent.kind = 0;
        props[static_cast<size_t> (nProp++)] = intent;
        ++roleProposeCount_[static_cast<size_t> (role)];
    }

    void proposeInteraction (int absSlot, const ColonySnapshot& snap,
                             std::array<PulseIntent, kMaxProposals>& props, int& nProp) noexcept
    {
        if (! snap.interaction || nProp >= kMaxProposals)
            return;

        // After Anchor accept: probabilistic Skitter answer window
        if (snap.callLive && snap.callKind == 1 && snap.callCaller == 0
            && (snap.solo < 0 || snap.solo == 1))
        {
            if (arbiterRng_.nextFloat() < (0.35f + 0.20f * snap.dens))
            {
                PulseIntent intent;
                intent.role = 1;
                intent.startAbsSlot = absSlot;
                intent.lengthSlots = arbiterRng_.nextFloat() < 0.55f ? 1 : 2;
                intent.importance = 0.45f;
                intent.spatialTarget = roleSpatialTarget (1);
                intent.kind = 1;
                props[static_cast<size_t> (nProp++)] = intent;
                ++roleProposeCount_[1];
            }
        }

        // Ghost after long gaps / hunger
        if ((snap.solo < 0 || snap.solo == 2) && nProp < kMaxProposals)
        {
            const float hungerLo = 8.0f + 16.0f * (1.0f - snap.dens);
            const float hungerHi = 16.0f + 32.0f * (1.0f - snap.dens);
            const float since = snap.beatsSinceRole[2];
            const bool hungry = since >= hungerLo;
            const bool longGap = snap.gapLengthSlots >= 4;
            float p = 0.0f;
            if (hungry && longGap)
                p = 0.35f + 0.35f * std::clamp ((since - hungerLo) / std::max (1.0f, hungerHi - hungerLo), 0.0f, 1.0f);
            else if (hungry)
                p = 0.12f;
            if (snap.callLive && snap.callKind == 2)
                p = std::max (p, 0.25f);
            if (p > 0.0f && arbiterRng_.nextFloat() < p)
            {
                PulseIntent intent;
                intent.role = 2;
                intent.startAbsSlot = absSlot;
                intent.lengthSlots = arbiterRng_.nextFloat() < 0.7f ? 2 : 1;
                intent.importance = 0.40f + 0.2f * std::min (1.0f, since / hungerHi);
                intent.spatialTarget = roleSpatialTarget (2);
                intent.kind = 3;
                props[static_cast<size_t> (nProp++)] = intent;
                ++roleProposeCount_[2];
            }
        }
    }

    PulseSuppressReason evaluateIntent (const PulseIntent& intent, const ColonySnapshot& snap) noexcept
    {
        const int role = intent.role;
        if (snap.solo >= 0 && snap.solo != role)
            return PulseSuppressReason::SoloFilter;

        if (snap.holding)
        {
            if (intent.startAbsSlot <= snap.holdEndSlot)
                return PulseSuppressReason::RedundantOpen;
        }

        // Budget: estimated occupancy if we accept
        const float mid = colonyBudgetMid (snap.dens);
        const float proj = colonyOpenOccupancy() + static_cast<float> (intent.lengthSlots)
                           / static_cast<float> (kOccRingSlots);
        if (proj > mid + 0.12f && role != 0)
            return PulseSuppressReason::GlobalBudget;
        if (proj > 0.78f)
            return PulseSuppressReason::GlobalBudget;

        if (snap.interaction)
        {
            if (snap.congestion01 > 0.65f && role == 2 && snap.beatsSinceRole[2] < 12.0f)
                return PulseSuppressReason::Congestion;
            if (snap.congestion01 > 0.75f && role == 1 && intent.kind == 0
                && intent.lengthSlots > 2)
                return PulseSuppressReason::Congestion;

            // Gap preserve: decorative roles shouldn't erase large voids entirely
            if (role != 0 && snap.gapLengthSlots >= voidMinSlots (snap.dens) + 4
                && intent.lengthSlots >= snap.gapLengthSlots
                && intent.kind == 0)
                return PulseSuppressReason::GapPreserve;

            // Skitter cooldown vs Anchor onset pile-up (incl. answer gestures)
            if (role == 1 && snap.lastAcceptedRole == 0
                && snap.beatsSinceRole[0] < 0.30f)
                return PulseSuppressReason::RoleCooldown;
            // Prevent Skitter sixteenth spam stacks
            if (role == 1 && snap.beatsSinceRole[1] < 0.45f)
                return PulseSuppressReason::RoleCooldown;
            // Prefer Anchor structure when Skitter is already busy vs budget
            if (role == 1 && snap.congestion01 > 0.55f && intent.kind == 0
                && snap.beatsSinceRole[0] > 2.0f)
                return PulseSuppressReason::Congestion;
        }

        return PulseSuppressReason::Accept;
    }

    void applySlot (int absSlot, float dens, float mut, double bpm) noexcept
    {
        const ColonySnapshot snap = makeSnapshot (absSlot, dens, mut);

        std::array<PulseIntent, kMaxProposals> props {};
        int nProp = 0;

        // Phase 1: all roles propose from immutable snapshot
        for (int r = 0; r < kNumRoles; ++r)
            proposeFromDna (r, absSlot, snap, props, nProp);
        proposeInteraction (absSlot, snap, props, nProp);

        // Phase 2: commit in priority order ANCHOR > SKITTER > GHOST (one accept/slot)
        int winner = -1;
        std::array<PulseSuppressReason, kMaxProposals> reasons {};
        reasons.fill (PulseSuppressReason::None);

        if (! snap.holding)
        {
            for (int pri = 0; pri < kNumRoles; ++pri)
            {
                for (int i = 0; i < nProp; ++i)
                {
                    if (props[static_cast<size_t> (i)].role != pri)
                        continue;
                    const auto reason = evaluateIntent (props[static_cast<size_t> (i)], snap);
                    reasons[static_cast<size_t> (i)] = reason;
                    if (reason == PulseSuppressReason::Accept && winner < 0)
                        winner = i;
                    else if (reason != PulseSuppressReason::Accept)
                    {
                        pushTrace (static_cast<double> (absSlot) * kSlotBeats,
                                   props[static_cast<size_t> (i)].role, reason, "SUPPRESS");
                    }
                }
                if (winner >= 0)
                    break;
            }
        }
        else
        {
            for (int i = 0; i < nProp; ++i)
            {
                reasons[static_cast<size_t> (i)] = PulseSuppressReason::RedundantOpen;
                pushTrace (static_cast<double> (absSlot) * kSlotBeats,
                           props[static_cast<size_t> (i)].role,
                           PulseSuppressReason::RedundantOpen, "SUPPRESS");
            }
        }

        bool slotOpen = snap.holding;
        if (winner >= 0)
        {
            for (int i = 0; i < nProp; ++i)
            {
                if (i == winner) continue;
                if (reasons[static_cast<size_t> (i)] == PulseSuppressReason::Accept
                    || reasons[static_cast<size_t> (i)] == PulseSuppressReason::None)
                {
                    pushTrace (static_cast<double> (absSlot) * kSlotBeats,
                               props[static_cast<size_t> (i)].role,
                               PulseSuppressReason::Collision, "SUPPRESS");
                }
            }

            commitAccept (props[static_cast<size_t> (winner)], absSlot, bpm);
            slotOpen = true;
        }
        else if (snap.holding)
        {
            markOccupancy (true);
            gapLengthSlots_ = 0;
            congestionEma_ = congestionEma_ * 0.92f + 0.08f;
            if (absSlot >= holdEndAbsSlot_)
            {
                holdOwnerRole_ = -1;
                holdEndAbsSlot_ = -1;
                // release begins next closed slot via gate hold countdown
            }
        }
        else
        {
            if (holdEndAbsSlot_ >= 0 && absSlot > holdEndAbsSlot_)
            {
                holdOwnerRole_ = -1;
                holdEndAbsSlot_ = -1;
            }
            if (gatePhase_ == 1 || gatePhase_ == 2)
                gatePhase_ = 3;
            markOccupancy (false);
            ++gapLengthSlots_;
            slotOpen = false;
        }

        (void) slotOpen;

        // Advance hunger (musical beats only)
        for (int r = 0; r < kNumRoles; ++r)
            beatsSinceRole_[static_cast<size_t> (r)] += static_cast<float> (kSlotBeats);

        if (callLive_ && absSlot > callExpireSlot_)
            callLive_ = false;

        budgetRemaining_ = std::min (1.0f, budgetRemaining_ + 0.02f);
    }

    void commitAccept (const PulseIntent& intent, int absSlot, double bpm) noexcept
    {
        const int role = intent.role;
        const int len = std::max (1, static_cast<int> (intent.lengthSlots));

        startPulse (len, bpm, static_cast<double> (absSlot) * kSlotBeats, role, intent.spatialTarget);

        holdOwnerRole_ = role;
        holdEndAbsSlot_ = absSlot + len - 1;
        lastAcceptedRole_ = role;
        beatsSinceRole_[static_cast<size_t> (role)] = 0.0f;
        ++roleAcceptCount_[static_cast<size_t> (role)];
        gapLengthSlots_ = 0;
        budgetRemaining_ = std::max (0.0f, budgetRemaining_ - static_cast<float> (len) * 0.04f);

        markOccupancy (true);
        congestionEma_ = congestionEma_ * 0.92f + 0.08f;

        // Call tokens after Anchor
        if (interactionEnabled_ && role == 0 && len >= 4)
        {
            callLive_ = true;
            callKind_ = 1; // CALL_SPINE → Skitter
            callCaller_ = 0;
            callExpireSlot_ = absSlot + static_cast<int> (2.0 / kSlotBeats)
                              + static_cast<int> (arbiterRng_.nextFloat() * (4.0 / kSlotBeats));
        }
        else if (interactionEnabled_ && role == 0 && gapLengthSlots_ == 0)
        {
            // release hole call is emitted when gap grows; light CALL_HOLE on short Anchor
            if (len >= 2 && arbiterRng_.nextFloat() < 0.35f)
            {
                callLive_ = true;
                callKind_ = 2;
                callCaller_ = 0;
                callExpireSlot_ = absSlot + len + static_cast<int> (4.0 / kSlotBeats);
            }
        }

        if (role == 2)
        {
            const float beat = static_cast<float> (absSlot) * static_cast<float> (kSlotBeats);
            if (lastGhostAcceptBeat_ >= 0.0f)
            {
                ghostGapSum_ += beat - lastGhostAcceptBeat_;
                ++ghostGapCount_;
            }
            lastGhostAcceptBeat_ = beat;
        }

        panRoleBias_ = intent.spatialTarget;
        pushTrace (static_cast<double> (absSlot) * kSlotBeats, role,
                   PulseSuppressReason::Accept, "ACCEPT");
    }

    void markOccupancy (bool open) noexcept
    {
        const uint8_t prev = occRing_[static_cast<size_t> (occRingPos_)];
        const uint8_t next = open ? 1 : 0;
        if (prev) --occOpenCount_;
        if (next) ++occOpenCount_;
        occRing_[static_cast<size_t> (occRingPos_)] = next;
        occRingPos_ = (occRingPos_ + 1) % kOccRingSlots;
        if (! open)
            congestionEma_ = congestionEma_ * 0.97f;
    }

    void startPulse (int lengthSlots, double bpm, double beat, int role, float spatial) noexcept
    {
        // Protect true mid-hold rearticulation; allow interrupting pass-through (no owner).
        if ((gatePhase_ == 1 || gatePhase_ == 2) && holdOwnerRole_ >= 0)
            return;

        const double beatsPerSample = (bpm / 60.0) / sampleRate_;
        int pulseSamples = std::max (8, static_cast<int> (
            (static_cast<double> (lengthSlots) * kSlotBeats) / beatsPerSample));
        int att = attN_, rel = relN_;
        const int minHold = std::max (1, static_cast<int> (0.001 * sampleRate_));
        if (pulseSamples < att + rel + minHold)
        {
            const float scale = static_cast<float> (pulseSamples)
                                / static_cast<float> (att + rel + minHold);
            att = std::max (1, static_cast<int> (att * scale));
            rel = std::max (1, static_cast<int> (rel * scale));
        }
        pulseAttN_ = att;
        pulseRelN_ = rel;
        holdLeft_ = std::max (1, pulseSamples - att - rel);
        gatePhase_ = 1;
        gatePos_ = 0;

        panSm_.setTarget (std::clamp (spatial, -1.0f, 1.0f));

        if (traceEnabled_ && opens_.size() < 8192)
        {
            PulseOpenEvent e;
            e.beat = beat;
            e.durationBeats = static_cast<float> (lengthSlots) * static_cast<float> (kSlotBeats);
            e.gain = 1.0f;
            e.pan = panSm_.current();
            e.role = static_cast<int8_t> (role);
            opens_.push_back (e);
        }
    }

    void tickGate() noexcept
    {
        if (gatePhase_ == 1)
        {
            ++gatePos_;
            const float t = static_cast<float> (gatePos_) / static_cast<float> (std::max (1, pulseAttN_));
            gateEnv_ = 0.5f * (1.0f - std::cos (std::min (1.0f, t) * 3.14159265f));
            if (gatePos_ >= pulseAttN_)
            {
                gateEnv_ = 1.0f;
                gatePhase_ = 2;
                gatePos_ = 0;
            }
        }
        else if (gatePhase_ == 2)
        {
            gateEnv_ = 1.0f;
            if (--holdLeft_ <= 0)
                gatePhase_ = 3;
        }
        else if (gatePhase_ == 3)
        {
            ++gatePos_;
            const float t = static_cast<float> (gatePos_) / static_cast<float> (std::max (1, pulseRelN_));
            gateEnv_ = 0.5f * (1.0f + std::cos (std::min (1.0f, t) * 3.14159265f));
            if (gatePos_ >= pulseRelN_)
            {
                gateEnv_ = 0.0f;
                gatePhase_ = 0;
                gatePos_ = 0;
            }
        }
        else
        {
            gateEnv_ = 0.0f;
        }
    }

    void updatePanTarget (double ppq) noexcept
    {
        const int bar = static_cast<int> (std::floor (ppq / 4.0));
        if (bar != lastPanBar_)
        {
            lastPanBar_ = bar;
            // Mild LFO around last role bias; uses Anchor spatial stream only for continuity
            panAnchor_ = panRoleBias_ * 0.65f
                         + cells_[0].spatialRng.nextFloat() * 0.15f - 0.075f;
        }
        const double phase = std::fmod (ppq / 8.0, 1.0);
        const float base = std::sin (static_cast<float> (phase * 2.0 * 3.14159265));
        panSm_.setTarget (std::clamp (base * 0.35f + panAnchor_ * 0.65f, -1.0f, 1.0f));
    }

    static void applyMotion (float& l, float& r, float motion, float pan) noexcept
    {
        const float m = std::clamp (motion, 0.0f, 1.0f);
        if (m <= 1.0e-5f)
            return;
        constexpr float kMaxAngle = 0.85f * 1.5707963f;
        const float x = std::clamp (m * pan, -1.0f, 1.0f);
        if (x >= 0.0f)
            l *= std::cos (x * kMaxAngle);
        else
            r *= std::cos ((-x) * kMaxAngle);
    }

    void pushTrace (double beat, int role, PulseSuppressReason reason, const char* detail) noexcept
    {
        if (! traceEnabled_ || events_.size() >= 4096)
            return;
        PulseTraceEvent e;
        e.beat = beat;
        char buf[192];
        if (reason == PulseSuppressReason::None)
        {
            std::snprintf (buf, sizeof (buf), "%s role=%s gen=%d occ=%.2f",
                           detail, roleName (role),
                           cells_[static_cast<size_t> (std::clamp (role, 0, kNumRoles - 1))].dna.generation,
                           colonyOpenOccupancy());
        }
        else
        {
            std::snprintf (buf, sizeof (buf), "%s role=%s reason=%s colonyOcc=%.2f",
                           detail, roleName (role), suppressName (reason), colonyOpenOccupancy());
        }
        e.detail = buf;
        events_.push_back (e);
    }

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 1024;
    uint64_t masterSeed_ = 2002;
    bool seedDirty_ = false;

    std::array<PulseCell, kNumRoles> cells_ {};
    pfl::generative::DeterministicRNG arbiterRng_ {};

    bool interactionEnabled_ = true;
    int soloRole_ = -1;

    float congestionEma_ = 0.0f;
    int gapLengthSlots_ = 0;
    std::array<float, kNumRoles> beatsSinceRole_ {};
    float budgetRemaining_ = 1.0f;
    int lastAcceptedRole_ = -1;
    int holdOwnerRole_ = -1;
    int holdEndAbsSlot_ = -1;
    bool callLive_ = false;
    int callKind_ = 0;
    int callExpireSlot_ = -1;
    int callCaller_ = -1;

    std::array<uint8_t, kOccRingSlots> occRing_ {};
    int occRingPos_ = 0;
    int occOpenCount_ = 0;

    std::array<int, kNumRoles> roleAcceptCount_ {};
    std::array<int, kNumRoles> roleProposeCount_ {};
    float ghostGapSum_ = 0.0f;
    int ghostGapCount_ = 0;
    float lastGhostAcceptBeat_ = -1.0f;

    ParamSmoother mixSm_, densSm_, mutSm_, motionSm_, outSm_, panSm_;
    DCBlocker dcL_, dcR_;
    SafetyLimiter limL_, limR_;

    float gateEnv_ = 1.0f;
    int gatePhase_ = 2;
    int gatePos_ = 0;
    int holdLeft_ = 0;
    int attN_ = 144, relN_ = 192;
    int pulseAttN_ = 144, pulseRelN_ = 192;
    float panAnchor_ = 0.0f;
    float panRoleBias_ = 0.0f;

    double lastPpq_ = -1.0e9;
    bool lastTransportPlaying_ = false;
    int lastBar_ = -1;
    int lastSlot_ = -1;
    int lastPanBar_ = -1;

    bool traceEnabled_ = false;
    std::vector<PulseTraceEvent> events_;
    std::vector<PulseOpenEvent> opens_;
};

} // namespace pfl::dsp

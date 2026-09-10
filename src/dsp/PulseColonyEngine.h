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
};

/**
 * Pulse Colony Stage 1 — one PulseCell rhythmic gate + stereo motion.
 * Algorithm v1. No audio memory, no feedback, no multi-cell.
 */
class PulseColonyEngine
{
public:
    static constexpr int kAlgorithmVersion = 1;
    static constexpr double kSlotBeats = 0.25; // sixteenth

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
        gateEnv_ = 1.0f; // pass-through until DNA schedules
        gatePhase_ = 2; // hold-open until first musical decision while playing
        holdLeft_ = 0;
        lastPpq_ = -1.0e9;
        lastTransportPlaying_ = false;
        lastBar_ = -1;
        lastSlot_ = -1;
        lastPanBar_ = -1;
        prevDensity_ = -1.0f;
        prevMutation_ = -1.0f;
        events_.clear();
        opens_.clear();
        rebuildRng();
        regenerateDNA (0);
        snapScheduler (0.0);
    }

    void setSeed (uint64_t seed) noexcept
    {
        const uint64_t s = seed == 0 ? 1ull : seed;
        if (s == masterSeed_)
            return;
        masterSeed_ = s;
        seedDirty_ = true; // apply at next bar
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

    const PulseDNA& dna() const noexcept { return dna_; }
    int generation() const noexcept { return dna_.generation; }
    float gateEnv() const noexcept { return gateEnv_; }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }
    const std::vector<PulseTraceEvent>& traces() const noexcept { return events_; }
    const std::vector<PulseOpenEvent>& opens() const noexcept { return opens_; }
    void clearTraces() noexcept { events_.clear(); opens_.clear(); }

    /** Rebuild PulseDNA at current params (call after prepare + param sync). */
    void forceRebuild (int bar = 0) noexcept
    {
        prevDensity_ = -1.0f;
        prevMutation_ = -1.0f;
        regenerateDNA (bar);
        snapScheduler (static_cast<double> (bar) * 4.0);
    }

    void process (float* left, float* right, int numSamples,
                  bool transportPlaying, double ppqStart, double bpm) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;

        const double safeBpm = bpm > 1.0 ? bpm : 120.0;
        const double beatsPerSample = (safeBpm / 60.0) / sampleRate_;

        // Seek / discontinuity → reconstruct from absolute PPQ
        if (lastPpq_ > -1.0e8)
        {
            const double jump = ppqStart - lastPpq_;
            const double maxBlockBeats =
                (static_cast<double> (maxBlockSize_) / sampleRate_) * (safeBpm / 60.0) * 2.5 + 0.05;
            if (jump < -0.01 || jump > maxBlockBeats)
            {
                reconstructAt (ppqStart, densSm_.current(), mutSm_.current());
                // Align gate to DNA at landing slot without flash-open
                syncGateToPpq (ppqStart);
            }
        }

        if (! transportPlaying && lastTransportPlaying_)
            beginPassThroughRamp(); // stop → pass-through with click-safe open
        if (transportPlaying && ! lastTransportPlaying_)
        {
            // Resume: leave pass-through; next slot edges drive the gate
            if (gatePhase_ == 2 && holdLeft_ <= 0)
                holdLeft_ = std::max (1, static_cast<int> (0.05 * sampleRate_));
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
                // Pass-through while stopped (ramp handled on stop edge)
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

            // Stereo motion (true stereo balance; MOTION 0 = unity)
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
    void rebuildRng() noexcept
    {
        initialRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x50494E49ull); // PINI
        mutateRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x504D5554ull); // PMUT
        durationRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x50445552ull); // PDUR
        spatialRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x50535041ull); // PSPA
    }

    void snapScheduler (double ppq) noexcept
    {
        // Land before current slot so advanceMusical applies the landing slot once.
        lastSlot_ = static_cast<int> (std::floor (ppq / kSlotBeats)) - 1;
        lastBar_ = static_cast<int> (std::floor (ppq / 4.0)) - 1;
    }

    void beginSafeOpen() noexcept
    {
        gatePhase_ = 2;
        gateEnv_ = 1.0f;
        holdLeft_ = std::max (1, static_cast<int> (0.25 * sampleRate_));
    }

    void beginPassThroughRamp() noexcept
    {
        gatePhase_ = 1;
        gatePos_ = 0;
        pulseAttN_ = attN_;
        pulseRelN_ = relN_;
        holdLeft_ = std::max (1, static_cast<int> (0.5 * sampleRate_));
    }

    void syncGateToPpq (double ppq) noexcept
    {
        const int n = dna_.lengthCells();
        if (n <= 0)
        {
            gatePhase_ = 0;
            gateEnv_ = 0.0f;
            return;
        }
        const int absSlot = static_cast<int> (std::floor (ppq / kSlotBeats));
        int local = (absSlot + dna_.phaseShiftSlots) % n;
        if (local < 0) local += n;
        const uint8_t m = dna_.mask[static_cast<size_t> (local)];
        if (m == 0)
        {
            gatePhase_ = 0;
            gateEnv_ = 0.0f;
            holdLeft_ = 0;
        }
        else
        {
            gatePhase_ = 2;
            gateEnv_ = 1.0f;
            holdLeft_ = std::max (1, static_cast<int> (0.25 * sampleRate_));
        }
    }

    static void occupancyBand (float dens, float& lo, float& hi) noexcept
    {
        dens = std::clamp (dens, 0.0f, 1.0f);
        if (dens <= 0.2f)
        {
            const float t = dens / 0.2f;
            lo = 0.08f + (0.15f - 0.08f) * t;
            hi = 0.20f + (0.35f - 0.20f) * t;
        }
        else if (dens <= 0.5f)
        {
            const float t = (dens - 0.2f) / 0.3f;
            lo = 0.15f + (0.35f - 0.15f) * t;
            hi = 0.35f + (0.65f - 0.35f) * t;
        }
        else
        {
            const float t = (dens - 0.5f) / 0.5f;
            lo = 0.35f + (0.60f - 0.35f) * t;
            hi = 0.65f + (0.85f - 0.65f) * t;
        }
    }

    static int voidMin (float dens) noexcept
    {
        if (dens <= 0.3f) return 4;
        if (dens <= 0.7f) return 2;
        return 1;
    }

    int lifespanBarsFor (float mut) noexcept
    {
        if (mut <= 1.0e-4f)
            return 1000000;
        const float t = std::clamp (mut, 0.0f, 1.0f);
        const int lo = std::max (1, static_cast<int> (std::lround (2.0 - t)));
        const int hi = std::max (lo, static_cast<int> (std::lround (3.0 + 10.0 * (1.0 - t))));
        const float u = mutateRng_.nextFloat();
        return lo + static_cast<int> (u * static_cast<float> (hi - lo + 1));
    }

    void rebuildMask() noexcept
    {
        const int n = dna_.lengthCells();
        dna_.mask.fill (0);
        for (int w = 0; w < dna_.windowCount; ++w)
        {
            const auto& win = dna_.windows[static_cast<size_t> (w)];
            for (int k = 0; k < win.lengthSlots; ++k)
            {
                int s = (win.startSlot + k) % n;
                if (s < 0) s += n;
                dna_.mask[static_cast<size_t> (s)] = (k == 0) ? 1 : 2;
            }
        }
    }

    int drawLength (float dens) noexcept
    {
        static constexpr int lens[] = { 1, 2, 4, 8 };
        float w[4];
        if (dens < 0.35f) { w[0]=0.15f; w[1]=0.35f; w[2]=0.40f; w[3]=0.10f; }
        else if (dens < 0.7f) { w[0]=0.25f; w[1]=0.35f; w[2]=0.30f; w[3]=0.10f; }
        else { w[0]=0.35f; w[1]=0.30f; w[2]=0.25f; w[3]=0.10f; }
        float u = durationRng_.nextFloat();
        for (int i = 0; i < 4; ++i)
        {
            u -= w[i];
            if (u <= 0.0f) return lens[i];
        }
        return 2;
    }

    int preferredStart (float dens) noexcept
    {
        // Soft downbeat avoidance: prefer off-16ths
        static constexpr int prefs[] = { 2, 3, 6, 7, 10, 11, 14, 15, 4, 12, 1, 5, 9, 13, 8, 0 };
        const float allowDown = dens > 0.85f ? 0.35f : 0.12f;
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const int idx = static_cast<int> (initialRng_.nextFloat() * 16.0f) % 16;
            const int s = prefs[idx];
            if (s % 16 == 0 && initialRng_.nextFloat() > allowDown)
                continue;
            return s;
        }
        return 6;
    }

    bool overlaps (int start, int len, int ignore = -1) const noexcept
    {
        const int n = dna_.lengthCells();
        for (int w = 0; w < dna_.windowCount; ++w)
        {
            if (w == ignore) continue;
            const auto& o = dna_.windows[static_cast<size_t> (w)];
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

    void regenerateDNA (int bar) noexcept
    {
        float dens = densSm_.current();
        if (dens < 0.0f) dens = densSm_.target();
        float lo = 0.2f, hi = 0.5f;
        occupancyBand (dens, lo, hi);
        const float targetOcc = lo + (hi - lo) * 0.5f;

        dna_ = {};
        // Phrase length bias toward 2 bars
        const float ub = initialRng_.nextFloat();
        if (ub < 0.15f) dna_.lengthBars = 1;
        else if (ub < 0.70f) dna_.lengthBars = 2;
        else if (ub < 0.90f) dna_.lengthBars = 3;
        else dna_.lengthBars = 4;

        dna_.phaseShiftSlots = static_cast<int> (initialRng_.nextFloat() * 16.0f) % 16;
        dna_.generation = 0;
        dna_.birthBar = bar;
        dna_.lifespanBars = lifespanBarsFor (mutSm_.current() >= 0.0f ? mutSm_.current() : mutSm_.target());
        nextWindowId_ = 1;

        const int n = dna_.lengthCells();
        int budget = std::max (1, static_cast<int> (std::lround (targetOcc * static_cast<float> (n))));
        int guard = 0;
        while (budget > 0 && dna_.windowCount < PulseDNA::kMaxWindows && guard++ < 64)
        {
            int len = drawLength (dens);
            len = std::min (len, budget);
            len = std::max (1, len);
            int start = preferredStart (dens) % n;
            // try a few starts
            bool placed = false;
            for (int t = 0; t < 12; ++t)
            {
                const int s = (start + t * 3) % n;
                if (! overlaps (s, len))
                {
                    auto& w = dna_.windows[static_cast<size_t> (dna_.windowCount++)];
                    w.id = static_cast<uint16_t> (nextWindowId_++);
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
        rebuildMask();
        // Ensure void exists
        if (dna_.occupancy() > 0.90f && dna_.windowCount > 1)
        {
            --dna_.windowCount;
            rebuildMask();
        }
        pushTrace (static_cast<double> (bar) * 4.0, "DNA_BIRTH");
    }

    void reconstructAt (double ppq, float dens, float mut) noexcept
    {
        densSm_.setCurrentAndTarget (dens);
        mutSm_.setCurrentAndTarget (mut);
        prevDensity_ = -1.0f;
        prevMutation_ = -1.0f;
        rebuildRng();
        const int bar = std::max (0, static_cast<int> (std::floor (ppq / 4.0)));
        regenerateDNA (0);
        for (int b = 0; b <= bar; ++b)
            evolveAtBar (b, dens, mut, true);
        snapScheduler (ppq);
        seedDirty_ = false;
    }

    enum class MutOp : uint8_t
    {
        Stay = 0, NudgeStart, Stretch, Split, Merge, SwapVoid, PhaseJog, BirthCull
    };

    MutOp drawMutOp (float mut) noexcept
    {
        if (mut <= 1.0e-4f)
            return MutOp::Stay;
        const float u = mutateRng_.nextFloat();
        // Stay more common at low mut
        const float stayP = 0.15f + 0.35f * (1.0f - mut);
        if (u < stayP) return MutOp::Stay;
        const float v = (u - stayP) / std::max (1.0e-4f, 1.0f - stayP);
        if (v < 0.28f) return MutOp::NudgeStart;
        if (v < 0.48f) return MutOp::Stretch;
        if (v < 0.62f) return MutOp::SwapVoid;
        if (v < 0.74f) return MutOp::Split;
        if (v < 0.84f) return MutOp::Merge;
        if (v < 0.92f) return MutOp::PhaseJog;
        return MutOp::BirthCull;
    }

    bool applyMutOp (MutOp op) noexcept
    {
        if (op == MutOp::Stay || dna_.windowCount <= 0)
            return false;
        const int n = dna_.lengthCells();
        const int wi = static_cast<int> (mutateRng_.nextFloat() * static_cast<float> (dna_.windowCount))
                       % dna_.windowCount;
        auto& w = dna_.windows[static_cast<size_t> (wi)];
        switch (op)
        {
            case MutOp::NudgeStart:
            {
                const int d = mutateRng_.nextFloat() < 0.5f ? -1 : 1;
                const int ns = (w.startSlot + d + n) % n;
                if (! overlaps (ns, w.lengthSlots, wi))
                    w.startSlot = static_cast<int16_t> (ns);
                break;
            }
            case MutOp::Stretch:
            {
                static constexpr int lens[] = { 1, 2, 4, 8 };
                int idx = 0;
                for (int i = 0; i < 4; ++i)
                    if (lens[i] == w.lengthSlots) idx = i;
                idx = std::clamp (idx + (mutateRng_.nextFloat() < 0.5f ? -1 : 1), 0, 3);
                const int nl = lens[idx];
                if (! overlaps (w.startSlot, nl, wi))
                    w.lengthSlots = static_cast<uint8_t> (nl);
                break;
            }
            case MutOp::Split:
            {
                if (w.lengthSlots < 2 || dna_.windowCount >= PulseDNA::kMaxWindows)
                    return false;
                const int left = std::max (1, w.lengthSlots / 2);
                const int right = w.lengthSlots - left;
                w.lengthSlots = static_cast<uint8_t> (left);
                auto& nw = dna_.windows[static_cast<size_t> (dna_.windowCount++)];
                nw.id = static_cast<uint16_t> (nextWindowId_++);
                nw.startSlot = static_cast<int16_t> ((w.startSlot + left) % n);
                nw.lengthSlots = static_cast<uint8_t> (right);
                break;
            }
            case MutOp::Merge:
            {
                if (dna_.windowCount < 2) return false;
                int other = (wi + 1) % dna_.windowCount;
                auto& o = dna_.windows[static_cast<size_t> (other)];
                const int gap = (o.startSlot - (w.startSlot + w.lengthSlots) + n) % n;
                if (gap > 2) return false;
                w.lengthSlots = static_cast<uint8_t> (
                    std::min (8, w.lengthSlots + gap + o.lengthSlots));
                // remove other
                dna_.windows[static_cast<size_t> (other)] = dna_.windows[static_cast<size_t> (dna_.windowCount - 1)];
                --dna_.windowCount;
                break;
            }
            case MutOp::SwapVoid:
            {
                const int d = mutateRng_.nextFloat() < 0.5f ? -2 : 2;
                const int ns = (w.startSlot + d + n) % n;
                if (! overlaps (ns, w.lengthSlots, wi))
                    w.startSlot = static_cast<int16_t> (ns);
                break;
            }
            case MutOp::PhaseJog:
                dna_.phaseShiftSlots = (dna_.phaseShiftSlots + (mutateRng_.nextFloat() < 0.5f ? -1 : 1) + 16) % 16;
                break;
            case MutOp::BirthCull:
            {
                float lo, hi;
                occupancyBand (densSm_.current(), lo, hi);
                if (dna_.occupancy() > hi && dna_.windowCount > 1)
                {
                    // cull shortest
                    int best = 0;
                    for (int i = 1; i < dna_.windowCount; ++i)
                        if (dna_.windows[static_cast<size_t> (i)].lengthSlots
                            < dna_.windows[static_cast<size_t> (best)].lengthSlots)
                            best = i;
                    dna_.windows[static_cast<size_t> (best)] =
                        dna_.windows[static_cast<size_t> (dna_.windowCount - 1)];
                    --dna_.windowCount;
                }
                else if (dna_.occupancy() < lo && dna_.windowCount < PulseDNA::kMaxWindows)
                {
                    const int len = drawLength (densSm_.current());
                    const int s = preferredStart (densSm_.current()) % n;
                    if (! overlaps (s, len))
                    {
                        auto& nw = dna_.windows[static_cast<size_t> (dna_.windowCount++)];
                        nw.id = static_cast<uint16_t> (nextWindowId_++);
                        nw.startSlot = static_cast<int16_t> (s);
                        nw.lengthSlots = static_cast<uint8_t> (len);
                    }
                }
                break;
            }
            default:
                return false;
        }
        rebuildMask();
        dna_.lastOp = static_cast<uint32_t> (op);
        return true;
    }

    void evolveAtBar (int bar, float dens, float mut, bool silent) noexcept
    {
        // Density live adapt
        if (prevDensity_ >= 0.0f)
        {
            const float dd = dens - prevDensity_;
            if (std::abs (dd) >= 0.30f)
            {
                regenerateDNA (bar);
                if (! silent)
                    pushTrace (static_cast<double> (bar) * 4.0, "DENSITY_REBORN");
            }
            else if (std::abs (dd) >= 0.15f)
            {
                applyMutOp (MutOp::BirthCull);
                applyMutOp (MutOp::Stretch);
                if (! silent)
                    pushTrace (static_cast<double> (bar) * 4.0, "DENSITY_ADAPT");
            }
        }
        prevDensity_ = dens;

        // Mutation lifespan
        if (mut <= 1.0e-4f)
        {
            dna_.lifespanBars = 1000000;
            prevMutation_ = mut;
            return;
        }
        if (prevMutation_ >= 0.0f && mut > prevMutation_ + 0.2f)
            dna_.lifespanBars = std::min (dna_.lifespanBars, 3);
        prevMutation_ = mut;

        if (bar - dna_.birthBar < dna_.lifespanBars)
            return;

        const MutOp op = drawMutOp (mut);
        if (op == MutOp::Stay || ! applyMutOp (op))
        {
            dna_.birthBar = bar;
            dna_.lifespanBars = lifespanBarsFor (mut);
            if (! silent)
                pushTrace (static_cast<double> (bar) * 4.0, "STAY");
            return;
        }
        ++dna_.generation;
        dna_.birthBar = bar;
        dna_.lifespanBars = lifespanBarsFor (mut);
        if (! silent)
            pushTrace (static_cast<double> (bar) * 4.0, "MUTATE");
    }

    void advanceMusical (double ppq, float dens, float mut, double bpm) noexcept
    {
        if (seedDirty_)
        {
            const int bar = static_cast<int> (std::floor (ppq / 4.0));
            if (bar != lastBar_)
            {
                regenerateDNA (bar);
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
                    evolveAtBar (b, dens, mut, false);
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
            applySlot (s, dens, bpm);
        }
        lastSlot_ = slot;

        // Spatial target updates slowly with DNA
        updatePanTarget (ppq);
    }

    void applySlot (int absSlot, float /*dens*/, double bpm) noexcept
    {
        const int n = dna_.lengthCells();
        if (n <= 0) return;
        int local = (absSlot + dna_.phaseShiftSlots) % n;
        if (local < 0) local += n;
        const uint8_t m = dna_.mask[static_cast<size_t> (local)];

        if (m == 1) // onset
        {
            // Find window length for this onset
            int len = 1;
            for (int w = 0; w < dna_.windowCount; ++w)
            {
                const auto& win = dna_.windows[static_cast<size_t> (w)];
                if ((win.startSlot % n + n) % n == local)
                {
                    len = win.lengthSlots;
                    break;
                }
            }
            startPulse (len, bpm, static_cast<double> (absSlot) * kSlotBeats);
        }
        else if (m == 0)
        {
            if (gatePhase_ == 1 || gatePhase_ == 2)
                gatePhase_ = 3; // release
        }
        // m==2 hold: stay open, no retrigger
    }

    void startPulse (int lengthSlots, double bpm, double beat) noexcept
    {
        if (gatePhase_ == 1 || gatePhase_ == 2)
            return; // no mid-hold retrigger

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
        gatePhase_ = 1; // attack
        gatePos_ = 0;

        if (traceEnabled_ && opens_.size() < 8192)
        {
            PulseOpenEvent e;
            e.beat = beat;
            e.durationBeats = static_cast<float> (lengthSlots) * static_cast<float> (kSlotBeats);
            e.gain = 1.0f;
            e.pan = panSm_.current();
            opens_.push_back (e);
        }
    }

    void tickGate() noexcept
    {
        if (gatePhase_ == 1) // attack raised-cosine
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
        else if (gatePhase_ == 2) // hold
        {
            gateEnv_ = 1.0f;
            if (--holdLeft_ <= 0)
                gatePhase_ = 3;
        }
        else if (gatePhase_ == 3) // release
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
            panAnchor_ = (spatialRng_.nextFloat() * 2.0f - 1.0f) * 0.85f;
        }
        const double phase = std::fmod (ppq / 8.0, 1.0);
        const float base = std::sin (static_cast<float> (phase * 2.0 * 3.14159265));
        panSm_.setTarget (std::clamp (base * 0.65f + panAnchor_ * 0.35f, -1.0f, 1.0f));
    }

    static void applyMotion (float& l, float& r, float motion, float pan) noexcept
    {
        const float m = std::clamp (motion, 0.0f, 1.0f);
        if (m <= 1.0e-5f)
            return;
        constexpr float kMaxAngle = 0.85f * 1.5707963f;
        const float x = std::clamp (m * pan, -1.0f, 1.0f);
        if (x >= 0.0f)
        {
            l *= std::cos (x * kMaxAngle);
        }
        else
        {
            r *= std::cos ((-x) * kMaxAngle);
        }
    }

    void pushTrace (double beat, const char* detail) noexcept
    {
        if (! traceEnabled_ || events_.size() >= 4096)
            return;
        PulseTraceEvent e;
        e.beat = beat;
        e.detail = detail;
        char buf[128];
        std::snprintf (buf, sizeof (buf), "%s gen=%d occ=%.2f bars=%d",
                       detail, dna_.generation, dna_.occupancy(), dna_.lengthBars);
        e.detail = buf;
        events_.push_back (e);
    }

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 1024;
    uint64_t masterSeed_ = 2002;
    bool seedDirty_ = false;

    PulseDNA dna_;
    int nextWindowId_ = 1;
    ParamSmoother mixSm_, densSm_, mutSm_, motionSm_, outSm_, panSm_;
    DCBlocker dcL_, dcR_;
    SafetyLimiter limL_, limR_;
    pfl::generative::DeterministicRNG initialRng_, mutateRng_, durationRng_, spatialRng_;

    float gateEnv_ = 1.0f;
    int gatePhase_ = 2; // 0 idle 1 attack 2 hold 3 release
    int gatePos_ = 0;
    int holdLeft_ = 0;
    int attN_ = 144, relN_ = 192;
    int pulseAttN_ = 144, pulseRelN_ = 192;
    float panAnchor_ = 0.0f;

    double lastPpq_ = -1.0e9;
    bool lastTransportPlaying_ = false;
    int lastBar_ = -1;
    int lastSlot_ = -1;
    int lastPanBar_ = -1;
    float prevDensity_ = -1.0f;
    float prevMutation_ = -1.0f;

    bool traceEnabled_ = false;
    std::vector<PulseTraceEvent> events_;
    std::vector<PulseOpenEvent> opens_;
};

} // namespace pfl::dsp

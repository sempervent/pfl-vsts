#pragma once

#include "AudioHistoryRing.h"
#include "DCBlocker.h"
#include "MemoryEcology.h"
#include "ParamSmoother.h"
#include "SafetyLimiter.h"

#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace pfl::dsp
{

struct MemoryRecallEvent
{
    double eventBeat = 0.0;
    double sourceBeat = 0.0;
    double lookbackBeats = 0.0;
    double fragmentBeats = 0.0;
    double durationBeats = 0.0;
    int loops = 0;
    float hunger = 0.0f;
    float memory = 0.0f;
    bool fromStored = false;
    int memoryId = -1;
    int generation = 0;
};

/**
 * Memory Eater Stage 4: Stage 3 ecology + performance intervention.
 * Algorithm v4. Recalled wet is NOT written back into the ring.
 * Descendants come from explicit capture of the internal wet recall path only,
 * or from manual MUTATE structural children.
 * Send-first: MIX=1.0 on Ableton Return is the canonical workflow.
 */
class MemoryEaterEngine
{
public:
    static constexpr int kAlgorithmVersion = 4;
    static constexpr float kMaxHistoryBeats = 32.0f;
    static constexpr float kMinDesignBpm = 40.0f;
    static constexpr double kOpportunityBeats = 0.5;

    void prepare (double sampleRate, int maxBlockSize = 1024) noexcept
    {
        const double sr = sampleRate > 1.0 ? sampleRate : 44100.0;
        const bool srChanged = std::abs (sr - sampleRate_) > 1.0e-6 || history_.capacity() == 0;
        sampleRate_ = sr;
        maxBlockSize_ = std::max (64, maxBlockSize);
        if (srChanged)
        {
            history_.prepare (sampleRate_, maxBlockSize_, kMaxHistoryBeats, kMinDesignBpm);
            ecology_.prepare (sampleRate_);
            const int n = ecology_.maxFragSamples();
            captureL_.assign (static_cast<size_t> (std::max (1, n)), 0.0f);
            captureR_.assign (static_cast<size_t> (std::max (1, n)), 0.0f);
            events_.clear();
            events_.reserve (4096);
            reset();
        }
        mixSmooth_.prepare (sampleRate_, 0.05f);
        hungerSmooth_.prepare (sampleRate_, 0.08f);
        memorySmooth_.prepare (sampleRate_, 0.08f);
        outSmooth_.prepare (sampleRate_, 0.05f);
        dcL_.prepare (sampleRate_);
        dcR_.prepare (sampleRate_);
        limL_.prepare (sampleRate_);
        limR_.prepare (sampleRate_);
        attackSamples_ = std::max (1, static_cast<int> (0.003 * sampleRate_));
        releaseSamples_ = std::max (1, static_cast<int> (0.008 * sampleRate_));
        xfadeSamples_ = std::max (1, static_cast<int> (0.005 * sampleRate_));
    }

    void reset() noexcept
    {
        history_.clear();
        ecology_.clearAll();
        voiceActive_ = false;
        sourceMode_ = 0;
        voiceSlot_ = -1;
        cancelCapture();
        lastEvalIndex_ = -1;
        lastRecallBeat_ = -1.0e9;
        lastTransportPlaying_ = false;
        lastPpq_ = -1.0e9;
        events_.clear();
        inputActivity_ = 0.0f;
        ringWriteEnabled_ = true;
        lifecycleEnabled_ = true;
        promoteEnabled_ = true;
        scheduleEnabled_ = true;
        captureEnabled_ = true;
        collapseRememberBias_ = false;
        silenceActive_ = false;
        silenceGain_ = 1.0f;
        silenceGainT_ = 1.0f;
        ecology_.setDecayMultiplier (1.0f);
        rebuildRng();
    }

    void setSeed (uint64_t seed) noexcept
    {
        if (seed == masterSeed_)
            return;
        masterSeed_ = seed == 0 ? 1ull : seed;
        rebuildRng();
        // Preserve ring + ecology/lineage; stop active recall/capture safely
        if (voiceActive_)
            beginRelease();
        cancelCapture();
    }

    uint64_t seed() const noexcept { return masterSeed_; }

    void setMix (float v) noexcept { mixSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setHunger (float v) noexcept { hungerSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setMemory (float v) noexcept { memorySmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }
    void setOutput (float v) noexcept { outSmooth_.setTarget (std::clamp (v, 0.0f, 1.0f)); }

    void snapMacros() noexcept
    {
        mixSmooth_.setCurrentAndTarget (mixSmooth_.target());
        hungerSmooth_.setCurrentAndTarget (hungerSmooth_.target());
        memorySmooth_.setCurrentAndTarget (memorySmooth_.target());
        outSmooth_.setCurrentAndTarget (outSmooth_.target());
    }

    int historyCapacityFrames() const noexcept { return history_.capacity(); }
    int historyFilledFrames() const noexcept { return history_.filled(); }
    bool voiceActive() const noexcept { return voiceActive_; }
    const std::vector<MemoryRecallEvent>& events() const noexcept { return events_; }
    void clearEvents() noexcept { events_.clear(); }
    void setTraceEnabled (bool e) noexcept
    {
        traceEnabled_ = e;
        ecology_.setTraceEnabled (e);
    }

    MemoryEcology& ecology() noexcept { return ecology_; }
    const MemoryEcology& ecology() const noexcept { return ecology_; }

    size_t historyRamBytes() const noexcept
    {
        return static_cast<size_t> (history_.capacity()) * 2u * sizeof (float);
    }

    size_t totalRamBytes() const noexcept
    {
        return historyRamBytes() + ecology_.slotRamBytes()
               + captureL_.size() * sizeof (float) + captureR_.size() * sizeof (float);
    }

    int descendantsCreated() const noexcept { return ecology_.descendants(); }
    bool captureArmed() const noexcept { return captureArmed_; }

    // --- Stage 4 performance hooks (driven by MemoryEaterPerformanceController) ---
    void setRingWriteEnabled (bool e) noexcept { ringWriteEnabled_ = e; }
    void setLifecycleEnabled (bool e) noexcept { lifecycleEnabled_ = e; }
    void setPromoteEnabled (bool e) noexcept { promoteEnabled_ = e; }
    void setScheduleEnabled (bool e) noexcept { scheduleEnabled_ = e; }
    void setCaptureEnabled (bool e) noexcept { captureEnabled_ = e; }
    void setCollapseRememberBias (bool e) noexcept { collapseRememberBias_ = e; }
    void setLifecycleDecayMultiplier (float m) noexcept { ecology_.setDecayMultiplier (m); }

    void setSilenceActive (bool on) noexcept
    {
        silenceActive_ = on;
        silenceGainT_ = on ? 0.0f : 1.0f;
        if (on)
            silenceCoeff_ = 1.0f - std::exp (-1.0f / static_cast<float> (std::max (1.0, 0.008 * sampleRate_)));
    }

    void beginUnsilenceFade() noexcept
    {
        silenceActive_ = false;
        silenceGainT_ = 1.0f;
        silenceCoeff_ = 1.0f - std::exp (-1.0f / static_cast<float> (std::max (1.0, 0.012 * sampleRate_)));
    }

    float silenceGain() const noexcept { return silenceGain_; }
    bool ringWriteEnabled() const noexcept { return ringWriteEnabled_; }
    bool scheduleEnabled() const noexcept { return scheduleEnabled_; }

    void clearShortTermRing() noexcept { history_.clear(); }

    /** Snap opportunity scheduler to current PPQ without processing missed indices. */
    void snapScheduler (double ppq) noexcept
    {
        lastEvalIndex_ = static_cast<int> (std::floor (ppq / kOpportunityBeats));
        lastRecallBeat_ = std::min (lastRecallBeat_, ppq);
    }

    /** Avoid lifecycle catch-up after a pause (FREEZE/SILENCE). */
    void resyncEcologyTimeline (double ppq) noexcept
    {
        ecology_.resyncTimeline (ppq);
    }

    void terminateActiveRecall() noexcept
    {
        if (voiceActive_)
            beginRelease();
        cancelCapture();
    }

    void cancelActiveCapture() noexcept { cancelCapture(); }

    /** Manual MUTATE: one bounded structural child. Returns child memoryId or -1. */
    int forceManualDescendant (double beat, float memoryParam,
                               pfl::generative::DeterministicRNG& selRng,
                               pfl::generative::DeterministicRNG& childRng) noexcept
    {
        const int parent = selectMutateParent (beat, selRng);
        if (parent < 0)
            return -1;
        const float offU = childRng.nextFloat();
        const float lenU = childRng.nextFloat();
        return ecology_.forceStructuralChild (beat, parent, offU, lenU, memoryParam);
    }

    int forceForgetSteps (double beat, int steps) noexcept
    {
        return ecology_.forceForgetSteps (beat, steps);
    }

    int enforceResidue (double beat) noexcept
    {
        return ecology_.enforceResidue (beat);
    }

    void clearAllMemories (double beat) noexcept
    {
        terminateActiveRecall();
        ecology_.clearAllMemories (beat);
    }

    float currentMemoryParam() const noexcept { return memorySmooth_.current(); }

    void process (float* left, float* right, int numSamples,
                  bool transportPlaying, double ppqStart, double bpm) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;

        const double safeBpm = bpm > 1.0 ? bpm : 120.0;
        const double beatsPerSample = (safeBpm / 60.0) / sampleRate_;

        // Timeline discontinuity → clear Stage 1 ring only; preserve ecology.
        if (lastPpq_ > -1.0e8)
        {
            const double jump = ppqStart - lastPpq_;
            const double maxBlockBeats =
                (static_cast<double> (maxBlockSize_) / sampleRate_) * (safeBpm / 60.0) * 2.5 + 0.05;
            if (jump < -0.01 || jump > maxBlockBeats)
            {
                history_.clear();
                if (voiceActive_)
                    beginRelease();
                voiceActive_ = false;
                ecology_.resyncTimeline (ppqStart);
                sourceMode_ = 0;
                voiceSlot_ = -1;
                cancelCapture(); // never promote partial child across seek
                lastEvalIndex_ = -1;
                lastRecallBeat_ = -1.0e9;
            }
        }

        if (! transportPlaying && lastTransportPlaying_)
        {
            if (voiceActive_)
                beginRelease();
            cancelCapture(); // cancel incomplete descendant on stop
        }
        lastTransportPlaying_ = transportPlaying;

        for (int i = 0; i < numSamples; ++i)
        {
            const double ppq = ppqStart + static_cast<double> (i) * beatsPerSample;
            float inL = left[i];
            float inR = right[i];
            if (! std::isfinite (inL)) inL = 0.0f;
            if (! std::isfinite (inR)) inR = 0.0f;

            const float mix = mixSmooth_.getNext();
            const float hunger = hungerSmooth_.getNext();
            const float memory = memorySmooth_.getNext();
            const float outG = outSmooth_.getNext();

            {
                const float e = std::min (1.0f, (std::abs (inL) + std::abs (inR)) * 4.0f);
                inputActivity_ += (e - inputActivity_) * 0.02f;
            }

            if (transportPlaying)
            {
                if (ringWriteEnabled_)
                    history_.write (inL, inR); // original input only — never wet
                if (lifecycleEnabled_)
                    ecology_.advanceLifecycle (ppq, memory);
                if (scheduleEnabled_)
                    advanceScheduler (ppq, hunger, memory, safeBpm);
            }

            float wetL = 0.0f, wetR = 0.0f;
            renderVoice (wetL, wetR);

            wetL = dcL_.processSample (wetL);
            wetR = dcR_.processSample (wetR);
            wetL = limL_.processSample (wetL);
            wetR = limR_.processSample (wetR);

            // Stage 3: explicit descendant capture from internal wet (pre MIX/OUTPUT)
            if (transportPlaying && captureEnabled_)
                tickCapture (wetL, wetR, ppq, memory);

            float outL = inL * (1.0f - mix) + wetL * mix;
            float outR = inR * (1.0f - mix) + wetR * mix;
            outL *= outG;
            outR *= outG;
            silenceGain_ += (silenceGainT_ - silenceGain_) * silenceCoeff_;
            outL *= silenceGain_;
            outR *= silenceGain_;
            outL = std::clamp (outL, -0.99f, 0.99f);
            outR = std::clamp (outR, -0.99f, 0.99f);
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
    int selectMutateParent (double beat, pfl::generative::DeterministicRNG& selRng) noexcept
    {
        float weights[MemoryEcology::kNumSlots] {};
        float total = 0.0f;
        int eligible = 0;
        for (int i = 0; i < MemoryEcology::kNumSlots; ++i)
        {
            const auto& s = ecology_.slot (i);
            if (! s.valid || s.generation >= MemoryEcology::kMaxGeneration)
                continue;
            const float age = static_cast<float> (std::max (0.0, beat - s.promoteBeat));
            const float w = (0.25f + 0.55f * s.strength) * (1.0f - 0.35f * s.fatigue)
                            * (1.0f + 0.15f * std::min (age, 64.0f) / 64.0f)
                            * (1.0f - 0.08f * static_cast<float> (s.generation));
            weights[i] = std::max (0.02f, w);
            total += weights[i];
            ++eligible;
        }
        if (eligible == 0 || total <= 0.0f)
            return -1;
        float u = selRng.nextFloat() * total;
        for (int i = 0; i < MemoryEcology::kNumSlots; ++i)
        {
            if (weights[i] <= 0.0f)
                continue;
            u -= weights[i];
            if (u <= 0.0f)
                return i;
        }
        for (int i = MemoryEcology::kNumSlots - 1; i >= 0; --i)
            if (weights[i] > 0.0f)
                return i;
        return -1;
    }

    void rebuildRng() noexcept
    {
        opportunityRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x4F50504Full);
        lookbackRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x4C4F4F4Bull);
        fragmentRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x46524147ull);
        durationRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x44555241ull);
        descendantRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x44455343ull); // DESC
        captureWindowRng_ = pfl::generative::DeterministicRNG::derived (masterSeed_, 0x57494E44ull); // WIND
        ecology_.reseed (masterSeed_);
    }

    static float smoothstep01 (float x) noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    void advanceScheduler (double ppq, float hunger, float memory, double bpm) noexcept
    {
        const int evalIndex = static_cast<int> (std::floor (ppq / kOpportunityBeats));
        if (evalIndex == lastEvalIndex_)
            return;
        if (lastEvalIndex_ >= 0 && evalIndex < lastEvalIndex_)
        {
            lastEvalIndex_ = evalIndex;
            return;
        }
        const int from = lastEvalIndex_ < 0 ? evalIndex : lastEvalIndex_ + 1;
        for (int idx = from; idx <= evalIndex; ++idx)
        {
            if (idx - lastEvalIndex_ > 64 && lastEvalIndex_ >= 0)
                break;
            tryScheduleAt (idx, hunger, memory, bpm);
        }
        lastEvalIndex_ = evalIndex;
    }

    void tryScheduleAt (int evalIndex, float hunger, float memory, double bpm) noexcept
    {
        if (voiceActive_)
            return;
        if (hunger < 1.0e-4f)
            return;

        const double beat = static_cast<double> (evalIndex) * kOpportunityBeats;
        const float minGap = 8.0f + (1.5f - 8.0f) * std::pow (hunger, 0.85f);
        if (beat - lastRecallBeat_ < static_cast<double> (minGap))
            return;

        const float u = opportunityRng_.nextFloat();
        const float pRaw = 0.004f + 0.10f * std::pow (hunger, 1.5f);
        const float silenceBoost = smoothstep01 (static_cast<float> ((beat - lastRecallBeat_) - minGap) / 8.0f);
        const float pEff = std::clamp (pRaw * (0.55f + 0.90f * silenceBoost), 0.0f, 0.35f);
        if (u >= pEff)
            return;

        const float beatsPerSample = static_cast<float> ((bpm / 60.0) / sampleRate_);
        const bool haveRecent = history_.maxSafeLookback() >= static_cast<int> (1.0f / beatsPerSample);
        const bool haveStored = ecology_.occupiedCount() > 0;

        if (! haveRecent && ! haveStored)
            return;
        if (haveRecent && inputActivity_ < 0.02f && history_.filled() < static_cast<int> (2.0f / beatsPerSample)
            && ! haveStored)
            return;

        startRecall (beat, hunger, memory, bpm, haveRecent, haveStored);
    }

    void chooseFragmentDuration (float& fragBeats, float& durBeats) noexcept
    {
        static constexpr float kFrags[] = { 0.125f, 0.25f, 0.5f, 1.0f };
        static constexpr float kDurs[] = { 0.25f, 0.5f, 1.0f, 2.0f };

        const float uf = fragmentRng_.nextFloat();
        fragBeats = kFrags[1];
        if (uf < 0.28f) fragBeats = kFrags[0];
        else if (uf < 0.58f) fragBeats = kFrags[1];
        else if (uf < 0.85f) fragBeats = kFrags[2];
        else fragBeats = kFrags[3];

        const float ud = durationRng_.nextFloat();
        durBeats = kDurs[1];
        if (ud < 0.30f) durBeats = kDurs[0];
        else if (ud < 0.60f) durBeats = kDurs[1];
        else if (ud < 0.88f) durBeats = kDurs[2];
        else durBeats = kDurs[3];
        if (durBeats < fragBeats)
            durBeats = fragBeats;
        if (durBeats > fragBeats * 4.0f)
            durBeats = fragBeats * 4.0f;
    }

    void startRecall (double beat, float hunger, float memory, double bpm,
                      bool haveRecent, bool haveStored) noexcept
    {
        float fragBeats = 0.25f, durBeats = 0.5f;
        chooseFragmentDuration (fragBeats, durBeats);
        const float beatsPerSample = static_cast<float> ((bpm / 60.0) / sampleRate_);
        const float durSamples = durBeats / beatsPerSample;

        // Selection: stored vs short-term (ecology RNG isolated from opportunity timing)
        bool useStored = false;
        int slot = -1;
        float memSel = memory;
        if (collapseRememberBias_)
            memSel = std::min (1.0f, memory + 0.35f);
        if (haveStored && (ecology_.shouldTryStored (memSel) || collapseRememberBias_))
        {
            slot = ecology_.selectStoredSlot (beat, memSel);
            useStored = slot >= 0;
        }

        if (useStored)
        {
            const auto& s = ecology_.slot (slot);
            sourceMode_ = 1;
            voiceSlot_ = slot;
            ecology_.setActiveSlot (slot);
            voiceActive_ = true;
            voicePhase_ = 0;
            voiceSamplesPlayed_ = 0;
            voiceDurationSamples_ = std::max (1, static_cast<int> (durSamples));
            fragLenSamples_ = std::max (1, s.lengthSamples);
            fragLookbackStart_ = 0.0f;
            fragPos_ = 0.0f;
            lastRecallBeat_ = beat;
            if (lifecycleEnabled_)
                ecology_.noteStoredRecall (slot, beat);

            const int loops = std::max (1, static_cast<int> (std::ceil (
                durBeats / std::max (0.01f, s.fragmentBeats))));
            pushRecallEvent (beat, s.originBeat, beat - s.originBeat, s.fragmentBeats, durBeats,
                             loops, hunger, memory, true, s.memoryId, s.generation);
            maybeArmDescendantCapture (slot, beat, memory, bpm);
            return;
        }

        if (! haveRecent)
            return;

        // Short-term ring recall (Stage 1 path)
        const float maxLbBeats = 1.0f + (kMaxHistoryBeats - 1.0f) * std::pow (memory, 1.2f);
        const float minLbBeats = 1.0f;
        const float ul = lookbackRng_.nextFloat();
        const float skew = 1.8f - 1.2f * memory;
        float lookbackBeats = minLbBeats + (maxLbBeats - minLbBeats) * std::pow (ul, skew);
        lookbackBeats = std::clamp (lookbackBeats, minLbBeats, maxLbBeats);

        const float lookbackSamples = lookbackBeats / beatsPerSample;
        float fragSamplesF = fragBeats / beatsPerSample;
        // Keep stored copies within ecology fragment capacity (1 beat @ 40 BPM).
        fragSamplesF = std::min (fragSamplesF, static_cast<float> (ecology_.maxFragSamples()));
        const int safe = history_.maxSafeLookback();
        if (lookbackSamples + fragSamplesF > static_cast<float> (safe))
        {
            // Retry with a shallower lookback before giving up — keeps promotion possible.
            const float maxLb = std::max (1.0f, static_cast<float> (safe) - fragSamplesF - 8.0f);
            if (maxLb < 1.0f / beatsPerSample)
                return;
            lookbackBeats = std::min (lookbackBeats, maxLb * beatsPerSample);
        }
        const float lookbackSamples2 = lookbackBeats / beatsPerSample;
        if (lookbackSamples2 + fragSamplesF > static_cast<float> (safe))
            return;
        if (inputActivity_ < 0.015f && lookbackBeats < 4.0f)
            return;

        sourceMode_ = 0;
        voiceSlot_ = -1;
        ecology_.clearActiveSlot();
        voiceActive_ = true;
        voicePhase_ = 0;
        voiceSamplesPlayed_ = 0;
        voiceDurationSamples_ = std::max (1, static_cast<int> (durSamples));
        fragLenSamples_ = std::max (1, static_cast<int> (fragSamplesF));
        fragLookbackStart_ = lookbackSamples2;
        fragPos_ = 0.0f;
        lastRecallBeat_ = beat;

        const int loops = std::max (1, static_cast<int> (std::ceil (durBeats / fragBeats)));
        const double sourceBeat = beat - lookbackBeats;
        pushRecallEvent (beat, sourceBeat, lookbackBeats, fragBeats, durBeats, loops,
                         hunger, memory, false, -1, 0);

        if (promoteEnabled_)
            ecology_.tryPromote (history_, beat, sourceBeat, lookbackSamples2, fragLenSamples_,
                                 fragBeats, memory, inputActivity_);
    }

    void pushRecallEvent (double beat, double sourceBeat, double lookback, float frag, float dur,
                          int loops, float hunger, float memory, bool stored, int mid, int gen) noexcept
    {
        if (! traceEnabled_ || events_.size() >= events_.capacity())
            return;
        MemoryRecallEvent ev;
        ev.eventBeat = beat;
        ev.sourceBeat = sourceBeat;
        ev.lookbackBeats = lookback;
        ev.fragmentBeats = frag;
        ev.durationBeats = dur;
        ev.loops = loops;
        ev.hunger = hunger;
        ev.memory = memory;
        ev.fromStored = stored;
        ev.memoryId = mid;
        ev.generation = gen;
        events_.push_back (ev);
    }

    void cancelCapture() noexcept
    {
        captureArmed_ = false;
        captureWriting_ = false;
        capturePos_ = 0;
        captureTarget_ = 0;
        captureParentSlot_ = -1;
        captureFragBeats_ = 0.0f;
        captureSkipRemaining_ = 0;
    }

    void maybeArmDescendantCapture (int parentSlot, double beat, float memory, double bpm) noexcept
    {
        cancelCapture();
        if (! promoteEnabled_ || ! captureEnabled_)
            return;
        if (parentSlot < 0)
            return;
        const auto& p = ecology_.slot (parentSlot);
        if (! p.valid)
            return;
        if (p.generation >= MemoryEcology::kMaxGeneration)
            return;
        if (p.recallCount < 2) // first reinforce only; anti-cascade
            return;
        if (beat - p.promoteBeat < MemoryEcology::kGenCooldownBeats)
            return;
        if (beat - p.lastChildBirthBeat < MemoryEcology::kGenCooldownBeats)
            return;
        if (beat - ecology_.lastChildPromoteBeat() < MemoryEcology::kGlobalChildCooldownBeats)
            return;

        const float mem = std::clamp (memory, 0.0f, 1.0f);
        float prob = (0.14f + 0.20f * mem) * (1.0f - 0.45f * p.fatigue)
                     * std::pow (0.75f, static_cast<float> (p.generation));
        prob = std::clamp (prob, 0.0f, 0.32f);
        if (descendantRng_.nextFloat() >= prob)
            return;

        // Structural mutation: offset + shorter window into parent length
        const float offU = captureWindowRng_.nextFloat();
        const float lenU = captureWindowRng_.nextFloat();
        const int parentLen = std::max (8, p.lengthSamples);
        captureSkipRemaining_ = static_cast<int> (offU * 0.28f * static_cast<float> (parentLen));
        int target = static_cast<int> ((0.55f + 0.37f * lenU) * static_cast<float> (parentLen));
        target = std::clamp (target, 8, ecology_.maxFragSamples());
        // Also bound by remaining sustain after attack
        const int sustainBudget = std::max (8, voiceDurationSamples_ - attackSamples_);
        target = std::min (target, sustainBudget);

        captureArmed_ = true;
        captureWriting_ = false;
        capturePos_ = 0;
        captureTarget_ = target;
        captureParentSlot_ = parentSlot;
        captureFragBeats_ = p.fragmentBeats * (static_cast<float> (target) / static_cast<float> (parentLen));
        captureMemoryParam_ = mem;
        captureBpm_ = bpm;
        if (traceEnabled_ && ecology_.traces().size() < 8192)
        {
            // lightweight: reuse ecology push via temporary slot snapshot not available;
            // recall event already logged; descendant promote logged on finalize
        }
    }

    void tickCapture (float wetL, float wetR, double beat, float memory) noexcept
    {
        if (! captureArmed_)
            return;
        // Only capture during sustain of stored voice
        if (! voiceActive_ || sourceMode_ != 1 || voicePhase_ != 1)
        {
            if (voicePhase_ == 2 || ! voiceActive_)
            {
                // End of recall without enough samples → cancel
                if (capturePos_ > 0 && capturePos_ >= captureTarget_ / 2)
                    finalizeCapture (beat, memory);
                else
                    cancelCapture();
            }
            return;
        }

        if (captureSkipRemaining_ > 0)
        {
            --captureSkipRemaining_;
            return;
        }

        captureWriting_ = true;
        if (capturePos_ < captureTarget_ && capturePos_ < static_cast<int> (captureL_.size()))
        {
            captureL_[static_cast<size_t> (capturePos_)] = wetL;
            captureR_[static_cast<size_t> (capturePos_)] = wetR;
            ++capturePos_;
        }
        if (capturePos_ >= captureTarget_)
            finalizeCapture (beat, memory);
    }

    void finalizeCapture (double beat, float memory) noexcept
    {
        if (! captureArmed_ || captureParentSlot_ < 0 || capturePos_ < 8)
        {
            cancelCapture();
            return;
        }
        const int n = capturePos_;
        ecology_.tryPromoteDescendant (beat, captureParentSlot_,
                                       captureL_.data(), captureR_.data(),
                                       n, captureFragBeats_, memory);
        cancelCapture();
    }

    void beginRelease() noexcept
    {
        if (! voiceActive_)
            return;
        voicePhase_ = 2;
        releasePos_ = 0;
    }

    void renderVoice (float& outL, float& outR) noexcept
    {
        outL = outR = 0.0f;
        if (! voiceActive_)
            return;

        float env = 1.0f;
        if (voicePhase_ == 0)
        {
            env = static_cast<float> (voiceSamplesPlayed_) / static_cast<float> (attackSamples_);
            if (voiceSamplesPlayed_ >= attackSamples_)
                voicePhase_ = 1;
        }
        else if (voicePhase_ == 2)
        {
            env = 1.0f - static_cast<float> (releasePos_) / static_cast<float> (releaseSamples_);
            ++releasePos_;
            if (releasePos_ >= releaseSamples_)
            {
                voiceActive_ = false;
                ecology_.clearActiveSlot();
                voiceSlot_ = -1;
                sourceMode_ = 0;
                // Incomplete capture already handled in tickCapture; ensure cancelled
                if (captureArmed_ && capturePos_ < captureTarget_)
                    cancelCapture();
                return;
            }
        }

        float pos = fragPos_;
        if (pos >= static_cast<float> (fragLenSamples_))
            pos = std::fmod (pos, static_cast<float> (fragLenSamples_));

        float gainA = 1.0f, gainB = 0.0f;
        float posB = 0.0f;
        if (pos >= static_cast<float> (fragLenSamples_ - xfadeSamples_) && fragLenSamples_ > xfadeSamples_ * 2)
        {
            const float t = (pos - static_cast<float> (fragLenSamples_ - xfadeSamples_))
                            / static_cast<float> (xfadeSamples_);
            const float w = 0.5f * (1.0f - std::cos (t * 3.14159265f));
            gainA = std::sqrt (1.0f - w);
            gainB = std::sqrt (w);
            posB = pos - static_cast<float> (fragLenSamples_ - xfadeSamples_);
        }

        float aL = 0, aR = 0, bL = 0, bR = 0;
        if (sourceMode_ == 1 && voiceSlot_ >= 0)
        {
            ecology_.readSlotSample (voiceSlot_, pos, aL, aR);
            if (gainB > 1.0e-5f)
                ecology_.readSlotSample (voiceSlot_, posB, bL, bR);
        }
        else
        {
            history_.readAtLookback (fragLookbackStart_ - pos, aL, aR);
            if (gainB > 1.0e-5f)
                history_.readAtLookback (fragLookbackStart_ - posB, bL, bR);
        }

        outL = (aL * gainA + bL * gainB) * env;
        outR = (aR * gainA + bR * gainB) * env;

        fragPos_ += 1.0f;
        ++voiceSamplesPlayed_;
        if (voiceSamplesPlayed_ >= voiceDurationSamples_ && voicePhase_ != 2)
            beginRelease();
    }

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 1024;
    uint64_t masterSeed_ = 3003;
    AudioHistoryRing history_;
    MemoryEcology ecology_;
    ParamSmoother mixSmooth_, hungerSmooth_, memorySmooth_, outSmooth_;
    DCBlocker dcL_, dcR_;
    SafetyLimiter limL_, limR_;
    pfl::generative::DeterministicRNG opportunityRng_, lookbackRng_, fragmentRng_, durationRng_;
    pfl::generative::DeterministicRNG descendantRng_, captureWindowRng_;

    int lastEvalIndex_ = -1;
    double lastRecallBeat_ = -1.0e9;
    double lastPpq_ = -1.0e9;
    bool lastTransportPlaying_ = false;
    float inputActivity_ = 0.0f;

    bool voiceActive_ = false;
    int sourceMode_ = 0; // 0 = ring, 1 = slot
    int voiceSlot_ = -1;
    int voicePhase_ = 0;
    int voiceSamplesPlayed_ = 0;
    int voiceDurationSamples_ = 0;
    int releasePos_ = 0;
    float fragPos_ = 0.0f;
    float fragLookbackStart_ = 0.0f;
    int fragLenSamples_ = 1;
    int attackSamples_ = 144;
    int releaseSamples_ = 384;
    int xfadeSamples_ = 240;

    // Stage 3 descendant capture (preallocated scratch; never wet→ring)
    bool captureArmed_ = false;
    bool captureWriting_ = false;
    int capturePos_ = 0;
    int captureTarget_ = 0;
    int captureParentSlot_ = -1;
    int captureSkipRemaining_ = 0;
    float captureFragBeats_ = 0.0f;
    float captureMemoryParam_ = 0.5f;
    double captureBpm_ = 120.0;
    std::vector<float> captureL_, captureR_;

    // Stage 4 performance gates (defaults preserve Stage 3 behaviour)
    bool ringWriteEnabled_ = true;
    bool lifecycleEnabled_ = true;
    bool promoteEnabled_ = true;
    bool scheduleEnabled_ = true;
    bool captureEnabled_ = true;
    bool collapseRememberBias_ = false;
    bool silenceActive_ = false;
    float silenceGain_ = 1.0f;
    float silenceGainT_ = 1.0f;
    float silenceCoeff_ = 0.05f;

    bool traceEnabled_ = false;
    std::vector<MemoryRecallEvent> events_;
};

} // namespace pfl::dsp

#pragma once

#include "AudioHistoryRing.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pfl::dsp
{

struct MemorySlot
{
    bool valid = false;
    int memoryId = 0;
    int lengthSamples = 0;
    float fragmentBeats = 0.0f;
    double originBeat = 0.0;   // musical time of fragment start when captured
    double promoteBeat = 0.0;
    double lastRecallBeat = -1.0e9;
    double lastChildBirthBeat = -1.0e9;
    int recallCount = 0;
    int generation = 0;        // 0 = live-input; 1+ = descendant
    int parentMemoryId = 0;    // 0 if root
    int rootMemoryId = 0;      // = memoryId for gen0
    float strength = 0.0f;
    float fatigue = 0.0f;
    std::vector<float> bufferL;
    std::vector<float> bufferR;
};

struct EcologyTraceEvent
{
    enum class Kind { Promote, Recall, Decay, Forget, Replace, DescendantCapture, DescendantPromote };
    Kind kind = Kind::Promote;
    double beat = 0.0;
    int memoryId = 0;
    int slot = -1;
    float strength = 0.0f;
    float fatigue = 0.0f;
    double sourceBeat = 0.0;
    float fragmentBeats = 0.0f;
    int generation = 0;
    int parentMemoryId = 0;
    int rootMemoryId = 0;
};

/**
 * Stage 2/3 fixed memory ecology: bounded slots owning preallocated fragment audio.
 * Lives beyond the short-term ring. Stage 3 adds generational descendants.
 * No processBlock allocation.
 */
class MemoryEcology
{
public:
    static constexpr int kNumSlots = 6;
    static constexpr int kMaxGeneration = 3; // gens 0..3
    static constexpr float kMaxFragmentBeats = 1.0f;
    static constexpr float kMinDesignBpm = 40.0f;
    static constexpr float kGenCooldownBeats = 12.0f;
    static constexpr float kGlobalChildCooldownBeats = 8.0f;
    static constexpr float kForgetThreshold = 0.045f;
    static constexpr float kBaseReinforce = 0.22f;
    static constexpr float kFatigueOnRecall = 1.0f;
    static constexpr float kFatigueRecoverPerBeat = 0.06f;
    static constexpr float kDecayPerBeatBase = 0.0065f;

    /** Stage 4 collapse: scale strength decay (1 = normal). */
    void setDecayMultiplier (float m) noexcept { decayMul_ = std::clamp (m, 0.0f, 24.0f); }
    float decayMultiplier() const noexcept { return decayMul_; }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        const double seconds = static_cast<double> (kMaxFragmentBeats) * 60.0
                               / static_cast<double> (kMinDesignBpm);
        maxFragSamples_ = std::max (64, static_cast<int> (std::ceil (sampleRate_ * seconds)) + 8);
        for (auto& s : slots_)
        {
            s.bufferL.assign (static_cast<size_t> (maxFragSamples_), 0.0f);
            s.bufferR.assign (static_cast<size_t> (maxFragSamples_), 0.0f);
            invalidate (s);
        }
        mutateScratchL_.assign (static_cast<size_t> (maxFragSamples_), 0.0f);
        mutateScratchR_.assign (static_cast<size_t> (maxFragSamples_), 0.0f);
        nextMemoryId_ = 1;
        lastEcologyBeat_ = 0.0;
        activeSlot_ = -1;
        traces_.clear();
        traces_.reserve (8192);
        promotions_ = recallsStored_ = forgotten_ = replacements_ = descendants_ = 0;
        lastChildPromoteBeat_ = -1.0e9;
    }

    void clearAll() noexcept
    {
        for (auto& s : slots_)
            invalidate (s);
        nextMemoryId_ = 1;
        activeSlot_ = -1;
        lastEcologyBeat_ = 0.0;
        traces_.clear();
        promotions_ = recallsStored_ = forgotten_ = replacements_ = descendants_ = 0;
        lastChildPromoteBeat_ = -1.0e9;
        decayMul_ = 1.0f;
    }

    /** After seek/loop: keep slots, but do not decay across the timeline jump. */
    void resyncTimeline (double beat) noexcept
    {
        lastEcologyBeat_ = beat;
        activeSlot_ = -1;
    }

    int maxFragSamples() const noexcept { return maxFragSamples_; }
    int numSlots() const noexcept { return kNumSlots; }
    const MemorySlot& slot (int i) const noexcept { return slots_[static_cast<size_t> (i)]; }
    int activeSlot() const noexcept { return activeSlot_; }
    void setActiveSlot (int s) noexcept { activeSlot_ = s; }
    void clearActiveSlot() noexcept { activeSlot_ = -1; }

    size_t slotRamBytes() const noexcept
    {
        return static_cast<size_t> (kNumSlots) * static_cast<size_t> (maxFragSamples_)
               * 2u * sizeof (float);
    }

    int promotions() const noexcept { return promotions_; }
    int recallsStored() const noexcept { return recallsStored_; }
    int forgotten() const noexcept { return forgotten_; }
    int replacements() const noexcept { return replacements_; }
    int descendants() const noexcept { return descendants_; }
    double lastChildPromoteBeat() const noexcept { return lastChildPromoteBeat_; }
    void setLastChildPromoteBeat (double b) noexcept { lastChildPromoteBeat_ = b; }

    int lineageCount (int rootId) const noexcept
    {
        if (rootId <= 0) return 0;
        int n = 0;
        for (const auto& s : slots_)
            if (s.valid && s.rootMemoryId == rootId)
                ++n;
        return n;
    }

    int maxLineageOccupancy() const noexcept
    {
        int best = 0;
        for (const auto& s : slots_)
        {
            if (! s.valid) continue;
            best = std::max (best, lineageCount (s.rootMemoryId));
        }
        return best;
    }

    int countByGeneration (int gen) const noexcept
    {
        int n = 0;
        for (const auto& s : slots_)
            if (s.valid && s.generation == gen)
                ++n;
        return n;
    }
    int occupiedCount() const noexcept
    {
        int n = 0;
        for (const auto& s : slots_)
            if (s.valid)
                ++n;
        return n;
    }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }
    const std::vector<EcologyTraceEvent>& traces() const noexcept { return traces_; }
    void clearTraces() noexcept { traces_.clear(); }

    void reseed (uint64_t masterSeed) noexcept
    {
        promoteRng_ = pfl::generative::DeterministicRNG::derived (masterSeed, 0x50524F4Dull); // PROM
        slotRng_ = pfl::generative::DeterministicRNG::derived (masterSeed, 0x534C4F54ull);     // SLOT
        ecologyRecallRng_ = pfl::generative::DeterministicRNG::derived (masterSeed, 0x45524543ull); // EREC
        lifetimeRng_ = pfl::generative::DeterministicRNG::derived (masterSeed, 0x4C494645ull); // LIFE
    }

    /** Advance decay/fatigue in musical beats while transport playing. */
    void advanceLifecycle (double beat, float memoryParam) noexcept
    {
        if (beat < lastEcologyBeat_)
            lastEcologyBeat_ = beat;
        const double dt = beat - lastEcologyBeat_;
        if (dt < 1.0e-6)
            return;
        lastEcologyBeat_ = beat;

        const float mem = std::clamp (memoryParam, 0.0f, 1.0f);
        // High MEMORY slows decay modestly (longer-lived memories).
        const float decayScale = 1.0f - 0.45f * mem;
        const float decayPerBeat = kDecayPerBeatBase * decayScale * decayMul_;

        for (int i = 0; i < kNumSlots; ++i)
        {
            auto& s = slots_[static_cast<size_t> (i)];
            if (! s.valid)
                continue;

            s.fatigue = std::max (0.0f, s.fatigue - kFatigueRecoverPerBeat * static_cast<float> (dt));
            const float before = s.strength;
            s.strength = std::max (0.0f, s.strength - decayPerBeat * static_cast<float> (dt));

            if (traceEnabled_ && (static_cast<int> (beat) % 8 == 0)
                && std::abs (before - s.strength) > 0.02f
                && traces_.size() < traces_.capacity())
            {
                pushTrace (EcologyTraceEvent::Kind::Decay, beat, s, i);
            }

            if (s.strength < kForgetThreshold && i != activeSlot_)
            {
                if (traceEnabled_)
                    pushTrace (EcologyTraceEvent::Kind::Forget, beat, s, i);
                invalidate (s);
                ++forgotten_;
            }
        }
    }

    /** Probability that a recall opportunity chooses a stored memory vs short-term. */
    float storedSelectionProbability (float memoryParam) const noexcept
    {
        const float mem = std::clamp (memoryParam, 0.0f, 1.0f);
        const int occ = occupiedCount();
        if (occ <= 0)
            return 0.0f;
        // Low MEMORY → mostly recent; high MEMORY → substantial stored share.
        return std::clamp (0.08f + 0.55f * std::pow (mem, 1.15f), 0.0f, 0.72f);
    }

    bool shouldTryStored (float memoryParam) noexcept
    {
        const float p = storedSelectionProbability (memoryParam);
        return ecologyRecallRng_.nextFloat() < p;
    }

    /** Weighted pick among valid non-fatigued slots. Returns slot index or -1. */
    int selectStoredSlot (double beat, float memoryParam) noexcept
    {
        float weights[kNumSlots] {};
        float sum = 0.0f;
        const float mem = std::clamp (memoryParam, 0.0f, 1.0f);
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto& s = slots_[static_cast<size_t> (i)];
            if (! s.valid)
                continue;
            const float age = static_cast<float> (std::max (0.0, beat - s.originBeat));
            // Prefer stronger, less fatigued; high MEMORY favors older origin age.
            const float ageBias = 0.35f + 0.65f * std::pow (std::min (age / 96.0f, 1.0f), 0.7f + 0.6f * mem);
            const int locc = lineageCount (s.rootMemoryId);
            const float lineagePen = 1.0f / (1.0f + 0.35f * static_cast<float> (std::max (0, locc - 1)));
            // Mild preference against deepest gens unless MEMORY is high
            const float genBias = 1.0f - 0.12f * static_cast<float> (s.generation) * (1.0f - 0.7f * mem);
            const float w = (0.08f + s.strength) * (1.0f - 0.92f * s.fatigue) * ageBias * lineagePen * genBias;
            weights[i] = std::max (0.0f, w);
            sum += weights[i];
        }
        if (sum <= 1.0e-8f)
            return -1;
        float u = slotRng_.nextFloat() * sum;
        for (int i = 0; i < kNumSlots; ++i)
        {
            u -= weights[i];
            if (u <= 0.0f)
                return i;
        }
        return -1;
    }

    void noteStoredRecall (int slotIndex, double beat) noexcept
    {
        if (slotIndex < 0 || slotIndex >= kNumSlots)
            return;
        auto& s = slots_[static_cast<size_t> (slotIndex)];
        if (! s.valid)
            return;
        // Diminishing-return reinforcement
        const float headroom = 1.0f - s.strength;
        s.strength = std::clamp (s.strength + kBaseReinforce * headroom, 0.0f, 0.98f);
        s.fatigue = std::max (s.fatigue, kFatigueOnRecall);
        s.lastRecallBeat = beat;
        ++s.recallCount;
        ++recallsStored_;
        if (traceEnabled_)
            pushTrace (EcologyTraceEvent::Kind::Recall, beat, s, slotIndex);
    }

    /**
     * Sparse promotion from ring lookback into a slot.
     * Copies bounded audio; never allocates.
     */
    bool tryPromote (const AudioHistoryRing& ring, double beat, double originBeat,
                     float lookbackSamples, int fragSamples, float fragBeats,
                     float memoryParam, float activity) noexcept
    {
        if (fragSamples <= 0 || fragSamples > maxFragSamples_)
            return false;
        if (activity < 0.012f)
            return false;

        // Sparse but frequent enough to populate a 6-slot cast within a section.
        const float mem = std::clamp (memoryParam, 0.0f, 1.0f);
        const float pPromote = 0.32f + 0.28f * mem;
        if (promoteRng_.nextFloat() >= pPromote)
            return false;

        const int safe = ring.maxSafeLookback();
        if (lookbackSamples + static_cast<float> (fragSamples) > static_cast<float> (safe))
            return false;

        int dest = findEmptySlot();
        if (dest < 0)
            dest = chooseVictim (beat);
        if (dest < 0)
            return false;

        auto& s = slots_[static_cast<size_t> (dest)];
        if (s.valid)
        {
            if (traceEnabled_)
                pushTrace (EcologyTraceEvent::Kind::Replace, beat, s, dest);
            ++replacements_;
        }

        // Copy fragment: lookbackStart is older end; sample i is lookbackStart - i
        for (int i = 0; i < fragSamples; ++i)
        {
            float L = 0, R = 0;
            ring.readAtLookback (lookbackSamples - static_cast<float> (i), L, R);
            s.bufferL[static_cast<size_t> (i)] = L;
            s.bufferR[static_cast<size_t> (i)] = R;
        }
        for (int i = fragSamples; i < maxFragSamples_; ++i)
        {
            s.bufferL[static_cast<size_t> (i)] = 0.0f;
            s.bufferR[static_cast<size_t> (i)] = 0.0f;
        }

        s.valid = true;
        s.memoryId = nextMemoryId_++;
        s.lengthSamples = fragSamples;
        s.fragmentBeats = fragBeats;
        s.originBeat = originBeat;
        s.promoteBeat = beat;
        s.lastRecallBeat = -1.0e9;
        s.recallCount = 0;
        s.strength = 0.28f + 0.12f * mem + 0.05f * lifetimeRng_.nextFloat();
        s.fatigue = 0.15f;
        s.generation = 0;
        s.parentMemoryId = 0;
        s.rootMemoryId = s.memoryId;
        s.lastChildBirthBeat = -1.0e9;
        ++promotions_;
        lastPromotedSlot_ = dest;

        if (traceEnabled_)
            pushTrace (EcologyTraceEvent::Kind::Promote, beat, s, dest);
        return true;
    }

    /**
     * Promote a descendant from pre-captured wet scratch (Stage 3).
     * Applies mild generation-scaled copy-loss at birth. Never allocates.
     */
    bool tryPromoteDescendant (double beat, int parentSlot, const float* srcL, const float* srcR,
                               int fragSamples, float fragBeats, float memoryParam) noexcept
    {
        if (parentSlot < 0 || parentSlot >= kNumSlots || srcL == nullptr || srcR == nullptr)
            return false;
        if (fragSamples < 8 || fragSamples > maxFragSamples_)
            return false;
        const auto& parent = slots_[static_cast<size_t> (parentSlot)];
        if (! parent.valid)
            return false;
        if (parent.generation >= kMaxGeneration)
            return false;

        const int rootId = parent.rootMemoryId > 0 ? parent.rootMemoryId : parent.memoryId;
        const int locc = lineageCount (rootId);
        const int occ = occupiedCount();
        const int lineageCap = (occ >= 4) ? 2 : 3;

        int dest = -1;
        if (locc >= lineageCap)
        {
            // Replace weakest same-lineage slot (not active / not parent)
            float bestScore = 1.0e9f;
            for (int i = 0; i < kNumSlots; ++i)
            {
                if (i == activeSlot_ || i == parentSlot) continue;
                const auto& s = slots_[static_cast<size_t> (i)];
                if (! s.valid || s.rootMemoryId != rootId) continue;
                const float score = s.strength * 2.0f - 0.01f * static_cast<float> (beat - s.lastRecallBeat);
                if (score < bestScore) { bestScore = score; dest = i; }
            }
            if (dest < 0)
                return false;
        }
        else
        {
            dest = findEmptySlot();
            if (dest < 0)
                dest = chooseVictim (beat);
            if (dest < 0)
                return false;
        }

        auto& s = slots_[static_cast<size_t> (dest)];

        // Activity gate on SOURCE before touching dest (never corrupt a victim slot)
        float srcPeak = 0.0f;
        for (int i = 0; i < fragSamples; ++i)
            srcPeak = std::max (srcPeak, std::max (std::abs (srcL[i]), std::abs (srcR[i])));
        if (srcPeak < 0.01f)
            return false;

        const int childGen = parent.generation + 1;
        const float g = static_cast<float> (childGen);
        // Mild attenuation only — never amplify (tanh soft-sat after gain < 1)
        const float gain = std::clamp (1.0f - 0.04f * g, 0.85f, 1.0f);
        const float drive = 1.0f + 0.06f * g;

        if (s.valid)
        {
            if (traceEnabled_)
                pushTrace (EcologyTraceEvent::Kind::Replace, beat, s, dest);
            ++replacements_;
        }

        for (int i = 0; i < fragSamples; ++i)
        {
            float L = srcL[i] * gain;
            float R = srcR[i] * gain;
            // Soft clip without makeup gain (avoids small-signal amplification)
            L = std::tanh (L * drive) / drive;
            R = std::tanh (R * drive) / drive;
            if (! std::isfinite (L)) L = 0.0f;
            if (! std::isfinite (R)) R = 0.0f;
            s.bufferL[static_cast<size_t> (i)] = L;
            s.bufferR[static_cast<size_t> (i)] = R;
        }
        for (int i = fragSamples; i < maxFragSamples_; ++i)
        {
            s.bufferL[static_cast<size_t> (i)] = 0.0f;
            s.bufferR[static_cast<size_t> (i)] = 0.0f;
        }

        const float mem = std::clamp (memoryParam, 0.0f, 1.0f);
        s.valid = true;
        s.memoryId = nextMemoryId_++;
        s.lengthSamples = fragSamples;
        s.fragmentBeats = fragBeats;
        s.originBeat = parent.originBeat;
        s.promoteBeat = beat;
        s.lastRecallBeat = -1.0e9;
        s.lastChildBirthBeat = -1.0e9;
        s.recallCount = 0;
        s.generation = childGen;
        s.parentMemoryId = parent.memoryId;
        s.rootMemoryId = rootId;
        s.strength = 0.18f + 0.08f * mem; // weaker than typical parent
        s.fatigue = 0.55f; // cooldown before immediate re-pick
        ++descendants_;
        ++promotions_;
        lastPromotedSlot_ = dest;
        lastChildPromoteBeat_ = beat;

        // Update parent's last child birth (parent may still be active)
        slots_[static_cast<size_t> (parentSlot)].lastChildBirthBeat = beat;

        if (traceEnabled_)
            pushTrace (EcologyTraceEvent::Kind::DescendantPromote, beat, s, dest);
        return true;
    }


    int lastPromotedSlot() const noexcept { return lastPromotedSlot_; }

    /** Force-forget weakest non-active slot. Returns forgotten memoryId or -1. */
    int forceForgetWeakest (double beat, int protectSlot = -1) noexcept
    {
        int best = -1;
        float bestScore = 1.0e9f;
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (i == activeSlot_ || i == protectSlot) continue;
            const auto& s = slots_[static_cast<size_t> (i)];
            if (! s.valid) continue;
            const float score = s.strength * 2.0f + 0.15f * static_cast<float> (s.generation)
                                - 0.01f * static_cast<float> (beat - s.lastRecallBeat);
            if (score < bestScore) { bestScore = score; best = i; }
        }
        if (best < 0) return -1;
        const int mid = slots_[static_cast<size_t> (best)].memoryId;
        if (traceEnabled_)
            pushTrace (EcologyTraceEvent::Kind::Forget, beat, slots_[static_cast<size_t> (best)], best);
        invalidate (slots_[static_cast<size_t> (best)]);
        ++forgotten_;
        return mid;
    }

    /** Keep at most one strongest valid slot (residue). Returns residue memoryId or -1. */
    int enforceResidue (double beat) noexcept
    {
        int best = -1;
        float bestStrength = -1.0f;
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (i == activeSlot_) continue;
            const auto& s = slots_[static_cast<size_t> (i)];
            if (! s.valid) continue;
            const float score = s.strength + 0.05f * static_cast<float> (s.recallCount)
                                - 0.02f * static_cast<float> (s.generation);
            if (score > bestStrength) { bestStrength = score; best = i; }
        }
        // Also consider active as residue candidate
        if (activeSlot_ >= 0 && slots_[static_cast<size_t> (activeSlot_)].valid)
        {
            const auto& s = slots_[static_cast<size_t> (activeSlot_)];
            const float score = s.strength + 0.05f * static_cast<float> (s.recallCount);
            if (score > bestStrength) { bestStrength = score; best = activeSlot_; }
        }
        if (best < 0)
            return -1;
        const int keepId = slots_[static_cast<size_t> (best)].memoryId;
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (i == best) continue;
            if (! slots_[static_cast<size_t> (i)].valid) continue;
            if (traceEnabled_)
                pushTrace (EcologyTraceEvent::Kind::Forget, beat, slots_[static_cast<size_t> (i)], i);
            invalidate (slots_[static_cast<size_t> (i)]);
            ++forgotten_;
        }
        return keepId;
    }

    /** Wipe all stored memories (performance amnesia). Cancels active membership. */
    void clearAllMemories (double beat) noexcept
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (! slots_[static_cast<size_t> (i)].valid) continue;
            if (traceEnabled_)
                pushTrace (EcologyTraceEvent::Kind::Forget, beat, slots_[static_cast<size_t> (i)], i);
            invalidate (slots_[static_cast<size_t> (i)]);
            ++forgotten_;
        }
        activeSlot_ = -1;
    }

    /**
     * Manual/performance structural child from parent slot buffers (no wet capture).
     * Uses same copy-loss as descendants. Returns child memoryId or -1.
     */
    int forceStructuralChild (double beat, int parentSlot, float offsetU, float lenU,
                              float memoryParam) noexcept
    {
        if (parentSlot < 0 || parentSlot >= kNumSlots) return -1;
        const auto& parent = slots_[static_cast<size_t> (parentSlot)];
        if (! parent.valid || parent.generation >= kMaxGeneration) return -1;
        const int parentLen = std::max (8, parent.lengthSamples);
        const int skip = static_cast<int> (offsetU * 0.28f * static_cast<float> (parentLen));
        int target = static_cast<int> ((0.55f + 0.37f * lenU) * static_cast<float> (parentLen));
        target = std::clamp (target, 8, maxFragSamples_);
        if (skip + target > parentLen)
            target = std::max (8, parentLen - skip);
        if (static_cast<int> (mutateScratchL_.size()) < target)
            return -1;
        for (int i = 0; i < target; ++i)
        {
            mutateScratchL_[static_cast<size_t> (i)] = parent.bufferL[static_cast<size_t> (skip + i)];
            mutateScratchR_[static_cast<size_t> (i)] = parent.bufferR[static_cast<size_t> (skip + i)];
        }
        const float fragBeats = parent.fragmentBeats
            * (static_cast<float> (target) / static_cast<float> (parentLen));
        if (! tryPromoteDescendant (beat, parentSlot, mutateScratchL_.data(), mutateScratchR_.data(),
                                    target, fragBeats, memoryParam))
            return -1;
        return slots_[static_cast<size_t> (lastPromotedSlot_)].memoryId;
    }

    /**
     * Collapse devour step: force-forget weakest slots once per call (block/tick rate).
     * Strength decay itself uses setDecayMultiplier + advanceLifecycle.
     */
    int forceForgetSteps (double beat, int steps) noexcept
    {
        int n = 0;
        for (int k = 0; k < steps; ++k)
        {
            if (occupiedCount() <= 1)
                break;
            if (forceForgetWeakest (beat) >= 0)
                ++n;
        }
        return n;
    }


    void readSlotSample (int slotIndex, float pos, float& outL, float& outR) const noexcept
    {
        outL = outR = 0.0f;
        if (slotIndex < 0 || slotIndex >= kNumSlots)
            return;
        const auto& s = slots_[static_cast<size_t> (slotIndex)];
        if (! s.valid || s.lengthSamples < 2)
            return;
        float p = pos;
        const float len = static_cast<float> (s.lengthSamples);
        while (p < 0.0f)
            p += len;
        while (p >= len)
            p -= len;
        const int i0 = static_cast<int> (p) % s.lengthSamples;
        const int i1 = (i0 + 1) % s.lengthSamples;
        const float frac = p - static_cast<float> (static_cast<int> (p));
        const float a0 = s.bufferL[static_cast<size_t> (i0)];
        const float a1 = s.bufferL[static_cast<size_t> (i1)];
        const float b0 = s.bufferR[static_cast<size_t> (i0)];
        const float b1 = s.bufferR[static_cast<size_t> (i1)];
        outL = a0 + (a1 - a0) * frac;
        outR = b0 + (b1 - b0) * frac;
    }

private:
    void invalidate (MemorySlot& s) noexcept
    {
        s.valid = false;
        s.memoryId = 0;
        s.lengthSamples = 0;
        s.strength = 0.0f;
        s.fatigue = 0.0f;
        s.recallCount = 0;
        s.generation = 0;
        s.parentMemoryId = 0;
        s.rootMemoryId = 0;
        s.lastChildBirthBeat = -1.0e9;
    }

    int findEmptySlot() noexcept
    {
        for (int i = 0; i < kNumSlots; ++i)
            if (! slots_[static_cast<size_t> (i)].valid)
            {
                lastPromotedSlot_ = i;
                return i;
            }
        return -1;
    }

    int chooseVictim (double beat) noexcept
    {
        int best = -1;
        float bestScore = 1.0e9f;
        for (int i = 0; i < kNumSlots; ++i)
        {
            if (i == activeSlot_)
                continue; // never overwrite active playback
            const auto& s = slots_[static_cast<size_t> (i)];
            if (! s.valid)
                continue;
            const float ageSinceRecall = static_cast<float> (beat - s.lastRecallBeat);
            // Prefer weak, tired, long-unrecalled
            const float score = s.strength * 2.0f + (1.0f - s.fatigue) * 0.3f
                                - 0.01f * std::min (ageSinceRecall, 256.0f)
                                + 0.002f * static_cast<float> (s.recallCount);
            if (score < bestScore)
            {
                bestScore = score;
                best = i;
            }
        }
        lastPromotedSlot_ = best;
        return best;
    }

    void pushTrace (EcologyTraceEvent::Kind kind, double beat, const MemorySlot& s, int slot) noexcept
    {
        if (! traceEnabled_ || traces_.size() >= traces_.capacity())
            return;
        EcologyTraceEvent ev;
        ev.kind = kind;
        ev.beat = beat;
        ev.memoryId = s.memoryId;
        ev.slot = slot;
        ev.strength = s.strength;
        ev.fatigue = s.fatigue;
        ev.sourceBeat = s.originBeat;
        ev.fragmentBeats = s.fragmentBeats;
        ev.generation = s.generation;
        ev.parentMemoryId = s.parentMemoryId;
        ev.rootMemoryId = s.rootMemoryId;
        traces_.push_back (ev);
    }

    double sampleRate_ = 44100.0;
    int maxFragSamples_ = 0;
    MemorySlot slots_[kNumSlots] {};
    int nextMemoryId_ = 1;
    int activeSlot_ = -1;
    int lastPromotedSlot_ = -1;
    double lastEcologyBeat_ = 0.0;
    bool traceEnabled_ = false;
    std::vector<EcologyTraceEvent> traces_;
    int promotions_ = 0, recallsStored_ = 0, forgotten_ = 0, replacements_ = 0, descendants_ = 0;
    double lastChildPromoteBeat_ = -1.0e9;
    pfl::generative::DeterministicRNG promoteRng_, slotRng_, ecologyRecallRng_, lifetimeRng_;
    std::vector<float> mutateScratchL_, mutateScratchR_;
    float decayMul_ = 1.0f;
};

} // namespace pfl::dsp

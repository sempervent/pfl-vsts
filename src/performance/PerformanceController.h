#pragma once

#include "PerformanceCommand.h"
#include "generative/Composer.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pfl::performance
{

struct PerformanceOutputs
{
    float silenceGain = 1.0f;
    float densityOverride = -1.0f; // <0 = use UI density
    float driftBoost = 0.0f;
    float dirtBoost = 0.0f;
    float spaceBoost = 0.0f;
    int maxActiveVoices = -1; // <0 = use composer activity
    bool compositionLocked = false;
    float reseedFade = 1.0f;
};

/**
 * Performance state machine above Composer.
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 *
 * Collapse duration: 8 bars (2 bars per stage: DESTABILIZE, THIN, DECAY, RESIDUE).
 * FREEZE: immediate composition lock (next block); no queued missed evolution.
 * MUTATE: immediate while FROZEN/COLLAPSED; else next bar boundary.
 * SILENCE: ~5 ms safety ramp; pauses composition.
 * RESEED: deterministic next seed; ~0.5–1 bar crossfade when not silenced.
 */
class PerformanceController
{
public:
    static constexpr int kCollapseBars = 8;

    void reset (uint64_t masterSeed) noexcept
    {
        mode_ = Mode::Normal;
        collapseBarsRemaining_ = 0;
        collapseStage_ = 0;
        reseedSamplesRemaining_ = 0;
        reseedTotalSamples_ = 0;
        pendingMutate_ = false;
        frozenBeforeSilence_ = false;
        collapseWasActive_ = false;
        reseedCount_ = 0;
        silenceGain_ = 1.0f;
        silenceTarget_ = 1.0f;
        currentSeed_ = masterSeed;
        seedDirty_ = false;
        events_.clear();
        manualRng_ = pfl::generative::DeterministicRNG::derived (masterSeed, hashTag ("manualMutation"));
        reseedRng_ = pfl::generative::DeterministicRNG::derived (masterSeed, hashTag ("reseed"));
    }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }
    const std::vector<PerformanceEvent>& events() const noexcept { return events_; }
    void clearEvents() noexcept { events_.clear(); }

    Mode mode() const noexcept { return mode_; }
    uint64_t reseedCount() const noexcept { return reseedCount_; }
    uint64_t currentSeed() const noexcept { return currentSeed_; }

    bool takeSeedDirty (uint64_t& outSeed) noexcept
    {
        if (! seedDirty_)
            return false;
        seedDirty_ = false;
        outSeed = currentSeed_;
        return true;
    }

    void trigger (Command cmd, double ppq, pfl::generative::Composer& composer) noexcept
    {
        switch (cmd)
        {
            case Command::SilenceOn:
                enterSilence (ppq, composer);
                break;
            case Command::SilenceOff:
                exitSilence (ppq, composer);
                break;
            case Command::Collapse:
                if (mode_ != Mode::Silenced)
                    beginCollapse (ppq, composer);
                break;
            case Command::FreezeOn:
                if (mode_ == Mode::Normal)
                    enterFreeze (ppq, composer);
                break;
            case Command::FreezeOff:
                if (mode_ == Mode::Frozen)
                    exitFreeze (ppq, composer);
                break;
            case Command::Mutate:
                requestMutate (ppq, composer);
                break;
            case Command::Reseed:
                beginReseed (ppq, composer);
                break;
        }
    }

    void onBar (int /*barIndex*/, double ppq, pfl::generative::Composer& composer) noexcept
    {
        if (mode_ == Mode::Silenced)
            return;

        if (pendingMutate_)
            flushMutateAtBoundary (ppq, composer);

        if (mode_ == Mode::Collapsing)
        {
            if (collapseBarsRemaining_ > 0)
                --collapseBarsRemaining_;

            const int elapsed = kCollapseBars - collapseBarsRemaining_;
            collapseStage_ = std::clamp (elapsed / 2, 0, 3);

            if (collapseBarsRemaining_ <= 0)
            {
                mode_ = Mode::Collapsed;
                composer.setCompositionLocked (true);
            }
        }
    }

    void flushMutateAtBoundary (double ppq, pfl::generative::Composer& composer) noexcept
    {
        if (! pendingMutate_)
            return;
        if (mode_ == Mode::Collapsing || mode_ == Mode::Silenced)
        {
            pendingMutate_ = false;
            return;
        }
        pendingMutate_ = false;
        applyMutate (ppq, composer);
    }

    void processSample (double sampleRate, PerformanceOutputs& out) noexcept
    {
        const float coeff = 1.0f - std::exp (-1.0f / std::max (1.0f, 0.005f * static_cast<float> (sampleRate)));
        silenceGain_ += (silenceTarget_ - silenceGain_) * coeff;

        out.silenceGain = silenceGain_;
        out.densityOverride = -1.0f;
        out.driftBoost = 0.0f;
        out.dirtBoost = 0.0f;
        out.spaceBoost = 0.0f;
        out.maxActiveVoices = -1;
        out.reseedFade = 1.0f;

        out.compositionLocked = (mode_ == Mode::Frozen || mode_ == Mode::Collapsing
                                  || mode_ == Mode::Collapsed || mode_ == Mode::Silenced
                                  || mode_ == Mode::Reseeding);

        if (mode_ == Mode::Collapsing || mode_ == Mode::Collapsed)
        {
            switch (collapseStage_)
            {
                case 0:
                    out.driftBoost = 0.35f;
                    out.dirtBoost = 0.25f;
                    out.spaceBoost = 0.15f;
                    out.densityOverride = 0.55f;
                    out.maxActiveVoices = 3;
                    break;
                case 1:
                    out.driftBoost = 0.45f;
                    out.dirtBoost = 0.35f;
                    out.densityOverride = 0.25f;
                    out.maxActiveVoices = 2;
                    break;
                case 2:
                    out.driftBoost = 0.55f;
                    out.dirtBoost = 0.2f;
                    out.spaceBoost = 0.35f;
                    out.densityOverride = 0.0f;
                    out.maxActiveVoices = 1;
                    break;
                default:
                    out.driftBoost = 0.25f;
                    out.dirtBoost = 0.05f;
                    out.spaceBoost = 0.45f;
                    out.densityOverride = 0.0f;
                    out.maxActiveVoices = 1;
                    break;
            }
        }

        if (mode_ == Mode::Reseeding && reseedTotalSamples_ > 0)
        {
            if (reseedSamplesRemaining_ > 0)
                --reseedSamplesRemaining_;
            const float t = 1.0f - static_cast<float> (reseedSamplesRemaining_)
                                      / static_cast<float> (reseedTotalSamples_);
            out.reseedFade = (t < 0.5f) ? (1.0f - 2.0f * t) : (2.0f * (t - 0.5f));
            out.reseedFade = std::clamp (out.reseedFade, 0.0f, 1.0f);
            if (reseedSamplesRemaining_ <= 0)
                finishReseed();
        }
    }

    void syncComposerLock (pfl::generative::Composer& composer) noexcept
    {
        const bool lock = (mode_ == Mode::Frozen || mode_ == Mode::Collapsing
                           || mode_ == Mode::Collapsed || mode_ == Mode::Silenced
                           || mode_ == Mode::Reseeding);
        composer.setCompositionLocked (lock);
    }

    void armReseedFade (int samples) noexcept
    {
        reseedTotalSamples_ = std::max (1, samples);
        reseedSamplesRemaining_ = reseedTotalSamples_;
    }

private:
    void finishReseed() noexcept
    {
        mode_ = Mode::Normal;
        reseedSamplesRemaining_ = 0;
    }

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

    void record (double ppq, Command cmd, uint64_t seed) noexcept
    {
        if (! traceEnabled_)
            return;
        events_.push_back ({ ppq, cmd, seed });
    }

    void enterSilence (double ppq, pfl::generative::Composer& composer) noexcept
    {
        frozenBeforeSilence_ = (mode_ == Mode::Frozen);
        collapseWasActive_ = (mode_ == Mode::Collapsing || mode_ == Mode::Collapsed);
        mode_ = Mode::Silenced;
        silenceTarget_ = 0.0f;
        pendingMutate_ = false;
        composer.setCompositionLocked (true);
        record (ppq, Command::SilenceOn, currentSeed_);
    }

    void exitSilence (double ppq, pfl::generative::Composer& composer) noexcept
    {
        if (mode_ != Mode::Silenced)
            return;
        silenceTarget_ = 1.0f;
        if (collapseWasActive_)
        {
            mode_ = Mode::Collapsed;
            composer.setCompositionLocked (true);
        }
        else if (frozenBeforeSilence_)
        {
            mode_ = Mode::Frozen;
            composer.setCompositionLocked (true);
        }
        else
        {
            mode_ = Mode::Normal;
            composer.setCompositionLocked (false);
        }
        record (ppq, Command::SilenceOff, currentSeed_);
    }

    void enterFreeze (double ppq, pfl::generative::Composer& composer) noexcept
    {
        mode_ = Mode::Frozen;
        composer.setCompositionLocked (true);
        record (ppq, Command::FreezeOn, currentSeed_);
    }

    void exitFreeze (double ppq, pfl::generative::Composer& composer) noexcept
    {
        mode_ = Mode::Normal;
        composer.setCompositionLocked (false);
        record (ppq, Command::FreezeOff, currentSeed_);
    }

    void requestMutate (double ppq, pfl::generative::Composer& composer) noexcept
    {
        if (mode_ == Mode::Collapsing || mode_ == Mode::Silenced)
            return;

        if (mode_ == Mode::Frozen || mode_ == Mode::Collapsed)
        {
            applyMutate (ppq, composer);
            return;
        }

        pendingMutate_ = true;
    }

    void applyMutate (double ppq, pfl::generative::Composer& composer) noexcept
    {
        composer.applyManualMutation (manualRng_);
        record (ppq, Command::Mutate, currentSeed_);
    }

    void beginCollapse (double ppq, pfl::generative::Composer& composer) noexcept
    {
        mode_ = Mode::Collapsing;
        collapseBarsRemaining_ = kCollapseBars;
        collapseStage_ = 0;
        pendingMutate_ = false;
        composer.setCompositionLocked (true);
        record (ppq, Command::Collapse, currentSeed_);
    }

    void beginReseed (double ppq, pfl::generative::Composer& composer) noexcept
    {
        const uint64_t next = nextSeedFrom (currentSeed_);
        ++reseedCount_;
        currentSeed_ = next;
        seedDirty_ = true;
        pendingMutate_ = false;

        if (reseedTotalSamples_ <= 0)
            armReseedFade (24000);

        const bool staySilent = (mode_ == Mode::Silenced);
        const double bpb = std::max (1.0e-9, composer.clock().beatsPerBar());
        const int bar = static_cast<int> (std::floor (ppq / bpb));
        const double barPpq = static_cast<double> (bar) * bpb;
        composer.applySeedAtBar (next, bar, barPpq);
        manualRng_ = pfl::generative::DeterministicRNG::derived (next, hashTag ("manualMutation"));

        if (staySilent)
        {
            mode_ = Mode::Silenced;
            composer.setCompositionLocked (true);
            silenceTarget_ = 0.0f;
        }
        else
        {
            mode_ = Mode::Reseeding;
            composer.setCompositionLocked (true);
        }

        record (ppq, Command::Reseed, next);
    }

    uint64_t nextSeedFrom (uint64_t current) noexcept
    {
        const uint64_t r = reseedRng_.nextU64();
        uint64_t z = current ^ (r + 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z = z ^ (z >> 31);
        return z % 1000000ull;
    }

    Mode mode_ = Mode::Normal;
    int collapseBarsRemaining_ = 0;
    int collapseStage_ = 0;
    int reseedSamplesRemaining_ = 0;
    int reseedTotalSamples_ = 0;
    bool pendingMutate_ = false;
    bool frozenBeforeSilence_ = false;
    bool collapseWasActive_ = false;
    bool seedDirty_ = false;
    uint64_t reseedCount_ = 0;
    uint64_t currentSeed_ = 1001;
    float silenceGain_ = 1.0f;
    float silenceTarget_ = 1.0f;
    bool traceEnabled_ = false;
    std::vector<PerformanceEvent> events_;
    pfl::generative::DeterministicRNG manualRng_;
    pfl::generative::DeterministicRNG reseedRng_;
};

} // namespace pfl::performance

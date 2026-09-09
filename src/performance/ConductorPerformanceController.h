#pragma once

#include "ConductorPerformanceTypes.h"
#include "generative/ConductorEngine.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace pfl::conductor_perf
{

/**
 * Broken Conductor Stage 5 performance state machine.
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 *
 * FREEZE = lock evolution; continue performing frozen Rhythm/Phrase DNA.
 * COLLAPSE = 24 beats (4×6) Destabilize→Thin→Fragment→Residue; stays Collapsed.
 * MUTATE ignored while Collapsing/Silenced; allowed while Frozen (stays frozen).
 * RESEED derives next seed from currentSeed + reseedCount (stored for projection sync).
 */
class ConductorPerformanceController
{
public:
    static constexpr double kCollapseBeats = 24.0;
    static constexpr double kCollapsePhaseBeats = 6.0;

    void reset (uint64_t masterSeed) noexcept
    {
        state_ = {};
        state_.currentSeed = masterSeed;
        pendingMutate_ = false;
        collapseWasActive_ = false;
        lastTickBar_ = -1;
        events_.clear();
        manualRng_ = pfl::generative::DeterministicRNG::derived (masterSeed, hashTag ("manualMutation"));
        reseedRng_ = pfl::generative::DeterministicRNG::derived (masterSeed, hashTag ("reseed"));
    }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }
    const std::vector<PerfTraceEvent>& events() const noexcept { return events_; }
    void clearEvents() noexcept { events_.clear(); }

    const ConductorPerfState& state() const noexcept { return state_; }
    Mode mode() const noexcept { return state_.mode; }
    uint64_t currentSeed() const noexcept { return state_.currentSeed; }
    uint64_t reseedCount() const noexcept { return state_.reseedCount; }

    bool takeSeedDirty (uint64_t& outSeed) noexcept
    {
        if (! seedDirty_)
            return false;
        seedDirty_ = false;
        outSeed = state_.currentSeed;
        return true;
    }

    void trigger (Command cmd, double ppq, pfl::generative::ConductorEngine& engine) noexcept
    {
        switch (cmd)
        {
            case Command::SilenceOn:
                enterSilence (ppq, engine);
                break;
            case Command::SilenceOff:
                exitSilence (ppq, engine);
                break;
            case Command::Collapse:
                if (state_.mode != Mode::Silenced)
                    beginCollapse (ppq, engine);
                break;
            case Command::FreezeOn:
                if (state_.mode == Mode::Normal)
                    enterFreeze (ppq, engine);
                break;
            case Command::FreezeOff:
                if (state_.mode == Mode::Frozen)
                    exitFreeze (ppq, engine);
                break;
            case Command::Mutate:
                requestMutate (ppq, engine);
                break;
            case Command::Reseed:
                beginReseed (ppq, engine);
                break;
        }
        syncEngine (engine);
    }

    /** Advance collapse phases from musical time; flush bar-queued mutate. */
    void tick (double ppq, int barIndex, pfl::generative::ConductorEngine& engine) noexcept
    {
        if (state_.mode == Mode::Silenced)
        {
            syncEngine (engine);
            return;
        }

        if (barIndex != lastTickBar_)
        {
            if (pendingMutate_)
                flushMutateAtBoundary (ppq, engine);
            lastTickBar_ = barIndex;
        }

        if (state_.mode == Mode::Collapsing)
        {
            const double elapsed = ppq - state_.collapseStartPpq;
            if (elapsed >= kCollapseBeats)
            {
                state_.mode = Mode::Collapsed;
                state_.collapsePhase = CollapsePhase::Residue;
            }
            else
            {
                const int phaseIdx = std::clamp (static_cast<int> (elapsed / kCollapsePhaseBeats), 0, 3);
                state_.collapsePhase = static_cast<CollapsePhase> (phaseIdx + 1);
            }
        }

        syncEngine (engine);
    }

    void syncEngine (pfl::generative::ConductorEngine& engine) noexcept
    {
        const bool silenced = (state_.mode == Mode::Silenced);
        const bool collapsing = (state_.mode == Mode::Collapsing);
        const bool collapsed = (state_.mode == Mode::Collapsed);
        const bool frozen = (state_.mode == Mode::Frozen);

        state_.silenceActive = silenced;
        state_.evolutionLocked = frozen || collapsing || collapsed || silenced;
        state_.hungerPaused = frozen || collapsing || collapsed || silenced;

        const int phase = (collapsing || collapsed) ? static_cast<int> (state_.collapsePhase) : 0;
        engine.setPerformanceFlags (state_.evolutionLocked, state_.silenceActive,
                                    state_.hungerPaused, phase);
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

    void record (double ppq, Command cmd, int mutateRole, const char* detail) noexcept
    {
        if (! traceEnabled_)
            return;
        PerfTraceEvent e;
        e.ppq = ppq;
        e.command = cmd;
        e.seedAfter = state_.currentSeed;
        e.mutateRole = mutateRole;
        if (detail != nullptr)
            e.detail = detail;
        events_.push_back (std::move (e));
    }

    void enterSilence (double ppq, pfl::generative::ConductorEngine& engine) noexcept
    {
        state_.frozenBeforeSilence = (state_.mode == Mode::Frozen);
        collapseWasActive_ = (state_.mode == Mode::Collapsing || state_.mode == Mode::Collapsed);
        state_.mode = Mode::Silenced;
        pendingMutate_ = false;
        engine.panic (ppq);
        record (ppq, Command::SilenceOn, -1, "silence");
    }

    void exitSilence (double ppq, pfl::generative::ConductorEngine& /*engine*/) noexcept
    {
        if (state_.mode != Mode::Silenced)
            return;
        if (collapseWasActive_)
        {
            state_.mode = Mode::Collapsed;
            state_.collapsePhase = CollapsePhase::Residue;
        }
        else if (state_.frozenBeforeSilence)
        {
            state_.mode = Mode::Frozen;
            state_.collapsePhase = CollapsePhase::None;
        }
        else
        {
            state_.mode = Mode::Normal;
            state_.collapsePhase = CollapsePhase::None;
        }
        record (ppq, Command::SilenceOff, -1, "resume");
    }

    void enterFreeze (double ppq, pfl::generative::ConductorEngine& /*engine*/) noexcept
    {
        state_.mode = Mode::Frozen;
        record (ppq, Command::FreezeOn, -1, "freeze evolution");
    }

    void exitFreeze (double ppq, pfl::generative::ConductorEngine& /*engine*/) noexcept
    {
        state_.mode = Mode::Normal;
        record (ppq, Command::FreezeOff, -1, "unfreeze");
    }

    void requestMutate (double ppq, pfl::generative::ConductorEngine& engine) noexcept
    {
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Silenced)
            return;

        if (state_.mode == Mode::Frozen || state_.mode == Mode::Collapsed)
        {
            applyMutate (ppq, engine);
            return;
        }

        pendingMutate_ = true;
    }

    void flushMutateAtBoundary (double ppq, pfl::generative::ConductorEngine& engine) noexcept
    {
        if (! pendingMutate_)
            return;
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Silenced)
        {
            pendingMutate_ = false;
            return;
        }
        pendingMutate_ = false;
        applyMutate (ppq, engine);
    }

    void applyMutate (double ppq, pfl::generative::ConductorEngine& engine) noexcept
    {
        char detail[96];
        const double beatsPerBar = 4.0;
        const int bar = static_cast<int> (std::floor (ppq / beatsPerBar));
        const int role = engine.applyManualMutation (manualRng_, bar, detail, sizeof detail);
        state_.lastMutateRole = role;
        ++state_.mutateCount;
        record (ppq, Command::Mutate, role, detail);
    }

    void beginCollapse (double ppq, pfl::generative::ConductorEngine& /*engine*/) noexcept
    {
        // COLLAPSE overrides FREEZE
        state_.mode = Mode::Collapsing;
        state_.collapsePhase = CollapsePhase::Destabilize;
        state_.collapseStartPpq = ppq;
        state_.collapseEndPpq = ppq + kCollapseBeats;
        pendingMutate_ = false;
        record (ppq, Command::Collapse, -1, "collapse 24 beats");
    }

    void beginReseed (double ppq, pfl::generative::ConductorEngine& engine) noexcept
    {
        ++state_.reseedCount;
        // Deterministic next seed in UI range [0, 999999]
        const uint64_t mix = state_.currentSeed
                             ^ (state_.reseedCount * 0x9E3779B97F4A7C15ull)
                             ^ hashTag ("conductor/reseed");
        reseedRng_ = pfl::generative::DeterministicRNG::derived (mix, hashTag ("reseed"));
        const uint64_t next = reseedRng_.nextU64() % 1000000ull;
        state_.currentSeed = next;
        seedDirty_ = true;

        engine.panic (ppq);
        engine.reseed (next);
        manualRng_ = pfl::generative::DeterministicRNG::derived (next, hashTag ("manualMutation"));

        state_.mode = Mode::Normal;
        state_.collapsePhase = CollapsePhase::None;
        state_.collapseStartPpq = -1.0;
        state_.collapseEndPpq = -1.0;
        pendingMutate_ = false;
        collapseWasActive_ = false;
        state_.frozenBeforeSilence = false;
        state_.mutateCount = 0;
        state_.lastMutateRole = -1;

        char detail[64];
        std::snprintf (detail, sizeof detail, "reseed→%llu", static_cast<unsigned long long> (next));
        record (ppq, Command::Reseed, -1, detail);
        syncEngine (engine);
    }

    ConductorPerfState state_{};
    bool pendingMutate_ = false;
    bool collapseWasActive_ = false;
    bool seedDirty_ = false;
    bool traceEnabled_ = false;
    int lastTickBar_ = -1;
    std::vector<PerfTraceEvent> events_;
    pfl::generative::DeterministicRNG manualRng_{};
    pfl::generative::DeterministicRNG reseedRng_{};
};

} // namespace pfl::conductor_perf

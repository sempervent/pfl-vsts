#pragma once

#include "SignalParasitePerformanceTypes.h"
#include "dsp/SignalParasiteEngine.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace pfl::parasite_perf
{

/**
 * Signal Parasite Stage 3 performance state machine.
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 *
 * FREEZE:   hold DNA evolution *and* the relationship state. The parasite
 *           keeps listening and keeps answering, in the manner it was holding.
 * MUTATE:   one bounded DNA op. Works while frozen or dormant; ignored while
 *           a collapse arc runs or while silenced; never unfreezes.
 * COLLAPSE: 24 beats of CLING → FEVER → WITHDRAW → DORMANT, six beats each.
 *           Overrides freeze, ignores retrigger, ends in a stable dormancy
 *           that only RESEED leaves.
 * RESEED:   a new parasite on the same source. Relationship back to LURKING,
 *           dormancy over, freeze latch preserved, allowed under silence.
 * SILENCE:  the processor mutes; here it means no answers and no backlog.
 *           The collapse clock and DNA evolution pause with it.
 */
class SignalParasitePerformanceController
{
public:
    using Engine = pfl::dsp::SignalParasiteEngine;

    static constexpr double kCollapseBeats = 24.0;
    static constexpr double kCollapsePhaseBeats = 6.0;
    static constexpr int kPerformanceEngineVersion = parasite_perf::kPerformanceEngineVersion;

    void reset (uint64_t masterSeed) noexcept
    {
        state_ = {};
        state_.currentSeed = masterSeed == 0 ? 1ull : masterSeed;
        pendingMutate_ = false;
        dnaEdited_ = false;
        seedDirty_ = false;
        events_.clear();
        reseedRng_ = pfl::generative::DeterministicRNG::derived (state_.currentSeed,
                                                                 0x52534544ull); // RSED
    }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }
    const std::vector<PerfTraceEvent>& events() const noexcept { return events_; }
    void clearEvents() noexcept { events_.clear(); }

    const ParasitePerfState& state() const noexcept { return state_; }
    Mode mode() const noexcept { return state_.mode; }
    CollapsePhase collapsePhase() const noexcept { return state_.collapsePhase; }
    bool dormant() const noexcept { return state_.dormant; }
    uint64_t currentSeed() const noexcept { return state_.currentSeed; }
    bool dnaEdited() const noexcept { return dnaEdited_; }

    bool takeSeedDirty (uint64_t& outSeed) noexcept
    {
        if (! seedDirty_)
            return false;
        seedDirty_ = false;
        outSeed = state_.currentSeed;
        return true;
    }

    void trigger (Command cmd, double ppq, Engine& engine) noexcept
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
                if (state_.mode == Mode::Silenced)
                    push (ppq, Command::Collapse, "COLLAPSE_IGNORED_UNDER_SILENCE");
                else
                    beginCollapse (ppq, engine);
                break;

            case Command::FreezeOn:
                state_.freezeLatched = true;
                if (state_.mode == Mode::Normal || state_.mode == Mode::Collapsed)
                    enterFreeze (ppq, engine);
                else if (state_.mode == Mode::Silenced)
                    push (ppq, Command::FreezeOn, "FREEZE_LATCH_UNDER_SILENCE");
                else if (state_.mode == Mode::Collapsing)
                    push (ppq, Command::FreezeOn, "FREEZE_LATCH_UNDER_COLLAPSE");
                break;

            case Command::FreezeOff:
                state_.freezeLatched = false;
                state_.frozenBeforeSilence = false;
                if (state_.mode == Mode::Frozen)
                    exitFreeze (ppq, engine);
                else
                    push (ppq, Command::FreezeOff, "FREEZE_UNLATCH");
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

    /** Advance the collapse arc with musical time and flush a queued mutate. */
    void tick (double ppq, bool playing, Engine& engine) noexcept
    {
        // Seek or loop wrap. A collapse cannot be resumed from a position it
        // never played through, so it settles where it was always going.
        if (state_.lastTickPpq >= 0.0)
        {
            const double jump = ppq - state_.lastTickPpq;
            if (jump < -0.01 || jump > 8.0)
            {
                if (state_.mode == Mode::Collapsing)
                {
                    push (ppq, Command::Collapse, "COLLAPSE_SETTLE_DORMANT_ON_SEEK");
                    settleDormant (engine);
                }
                pendingMutate_ = false;
            }
        }

        if (state_.mode == Mode::Silenced)
        {
            // The arc keeps its place: silence pauses the clock, it does not
            // spend it.
            state_.lastTickPpq = ppq;
            syncEngine (engine);
            return;
        }

        if (playing)
        {
            if (pendingMutate_
                && std::floor (ppq / 4.0) != std::floor (state_.lastTickPpq / 4.0))
                flushMutate (ppq, engine);

            if (state_.mode == Mode::Collapsing)
            {
                double delta = 0.0;
                if (state_.lastTickPpq >= 0.0)
                {
                    delta = ppq - state_.lastTickPpq;
                    if (delta < 0.0 || delta > 8.0)
                        delta = 0.0;
                }
                state_.collapseElapsedBeats += delta;
                updateCollapsePhase (ppq, engine);

                if (state_.collapseElapsedBeats >= kCollapseBeats)
                    finishCollapse (ppq, engine);
            }
        }

        state_.lastTickPpq = ppq;
        syncEngine (engine);
    }

    /**
     * Restore the stable half of a saved session: a dormancy and a freeze
     * latch, never a collapse arc in flight.
     */
    void restoreStableMode (bool dormant, bool freezeLatched, Engine& engine) noexcept
    {
        state_.freezeLatched = freezeLatched;
        state_.dormant = dormant;
        if (dormant)
        {
            state_.mode = freezeLatched ? Mode::Frozen : Mode::Collapsed;
            state_.collapsePhase = CollapsePhase::Dormant;
            state_.collapseElapsedBeats = kCollapseBeats;
            engine.applyCollapsePhase (4);
            dnaEdited_ = true;
        }
        else if (freezeLatched)
        {
            state_.mode = Mode::Frozen;
        }
        engine.snapDnaBaselines();
        syncEngine (engine);
    }

private:
    void push (double ppq, Command c, const char* detail) noexcept
    {
        if (! traceEnabled_)
            return;
        PerfTraceEvent e;
        e.ppq = ppq;
        e.command = c;
        e.detail = detail;
        events_.push_back (e);
    }

    /** The single place the engine learns what mode it is being played in. */
    void syncEngine (Engine& engine) noexcept
    {
        const bool silenced = state_.mode == Mode::Silenced;
        const bool frozen = state_.mode == Mode::Frozen;
        const bool collapsing = state_.mode == Mode::Collapsing;
        const bool collapsed = state_.mode == Mode::Collapsed;

        engine.setDnaEvolutionPaused (frozen || silenced || collapsing || collapsed
                                      || state_.dormant);
        // A collapse arc owns manner while it runs, so it is the one thing
        // that unpins a frozen relationship — and only for its own duration.
        engine.setRelationshipFrozen (state_.freezeLatched && ! collapsing);
        engine.setRelationshipTransitionsPaused (collapsing || state_.dormant);
        engine.setResponsesEnabled (! silenced && ! state_.dormant);
        state_.silenceActive = silenced;
    }

    void enterFreeze (double ppq, Engine& engine) noexcept
    {
        state_.mode = Mode::Frozen;
        state_.freezeLatched = true;
        engine.setDnaEvolutionPaused (true);
        engine.setRelationshipFrozen (true);
        char buf[128];
        std::snprintf (buf, sizeof (buf), "FREEZE gen=%u state=%s",
                       static_cast<unsigned> (engine.dnaGeneration()),
                       pfl::dsp::parasiteRelationshipName (engine.relationshipState()));
        push (ppq, Command::FreezeOn, buf);
    }

    void exitFreeze (double ppq, Engine& engine) noexcept
    {
        state_.freezeLatched = false;
        engine.setRelationshipFrozen (false);
        if (state_.dormant)
        {
            // Unfreezing a dormant parasite does not wake it. Only RESEED does.
            state_.mode = Mode::Collapsed;
            engine.setDnaEvolutionPaused (true);
            push (ppq, Command::FreezeOff, "UNFREEZE_TO_DORMANT");
            return;
        }
        state_.mode = Mode::Normal;
        engine.setDnaEvolutionPaused (false);
        engine.snapDnaBaselines();
        push (ppq, Command::FreezeOff, "UNFREEZE");
    }

    void beginCollapse (double ppq, Engine& engine) noexcept
    {
        if (state_.mode == Mode::Collapsing || state_.dormant)
        {
            push (ppq, Command::Collapse, "COLLAPSE_RETRIGGER_IGNORED");
            return;
        }

        if (state_.mode == Mode::Frozen)
            push (ppq, Command::Collapse, "COLLAPSE_OVERRIDES_FREEZE");

        state_.mode = Mode::Collapsing;
        state_.collapsePhase = CollapsePhase::Cling;
        state_.collapseElapsedBeats = 0.0;
        state_.lastTickPpq = ppq;
        pendingMutate_ = false;

        engine.setDnaEvolutionPaused (true);
        engine.setRelationshipFrozen (false);
        engine.setRelationshipTransitionsPaused (true);
        engine.applyCollapsePhase (1);
        push (ppq, Command::Collapse, "COLLAPSE_START CLING");
    }

    void updateCollapsePhase (double ppq, Engine& engine) noexcept
    {
        const int phaseIdx = std::clamp (
            static_cast<int> (state_.collapseElapsedBeats / kCollapsePhaseBeats), 0, 3);
        const auto phase = static_cast<CollapsePhase> (phaseIdx + 1);
        if (phase == state_.collapsePhase)
            return;

        state_.collapsePhase = phase;
        engine.applyCollapsePhase (static_cast<uint8_t> (phaseIdx + 1));
        if (phase == CollapsePhase::Dormant)
        {
            // The last six beats are already the dormancy, not a run-up to it.
            state_.dormant = true;
            engine.clearPendingResponse();
            engine.stopVoiceSafely();
        }
        char buf[96];
        std::snprintf (buf, sizeof (buf), "COLLAPSE_%s", collapsePhaseName (phase));
        push (ppq, Command::Collapse, buf);
    }

    void finishCollapse (double ppq, Engine& engine) noexcept
    {
        settleDormant (engine);
        push (ppq, Command::Collapse, "DORMANT");
    }

    /** Land in the stable dormancy, whether the arc ran out or was cut short. */
    void settleDormant (Engine& engine) noexcept
    {
        state_.dormant = true;
        state_.collapsePhase = CollapsePhase::Dormant;
        state_.collapseElapsedBeats = kCollapseBeats;
        state_.mode = state_.freezeLatched ? Mode::Frozen : Mode::Collapsed;
        dnaEdited_ = true;
        pendingMutate_ = false;
        engine.applyCollapsePhase (4);
        engine.clearPendingResponse();
        engine.stopVoiceSafely();
        engine.setDnaEvolutionPaused (true);
    }

    void requestMutate (double ppq, Engine& engine) noexcept
    {
        if (state_.mode == Mode::Collapsing)
        {
            push (ppq, Command::Mutate, "MUTATE_IGNORED_MID_COLLAPSE");
            return;
        }
        if (state_.mode == Mode::Silenced)
        {
            push (ppq, Command::Mutate, "MUTATE_IGNORED_UNDER_SILENCE");
            return;
        }
        if (state_.mode == Mode::Frozen || state_.mode == Mode::Collapsed || state_.dormant)
        {
            // Nothing is evolving, so there is no bar line worth waiting for.
            flushMutate (ppq, engine);
            return;
        }
        pendingMutate_ = true;
    }

    void flushMutate (double ppq, Engine& engine) noexcept
    {
        pendingMutate_ = false;
        state_.lastMutateOp = engine.manualMutate();
        dnaEdited_ = true;
        char buf[128];
        std::snprintf (buf, sizeof (buf), "MUTATE op=%s gen=%u", engine.lastManualMutateOpName(),
                       static_cast<unsigned> (engine.dnaGeneration()));
        push (ppq, Command::Mutate, buf);
        // Does not unfreeze and does not wake a dormant parasite.
    }

    void beginReseed (double ppq, Engine& engine) noexcept
    {
        ++state_.reseedCount;
        const uint64_t oldSeed = state_.currentSeed;
        const uint64_t next = 1ull + static_cast<uint64_t> (reseedRng_.nextFloat() * 999998.0f);
        state_.currentSeed = next;
        seedDirty_ = true;
        reseedRng_ = pfl::generative::DeterministicRNG::derived (next, 0x52534544ull);

        const int bar = std::max (0, static_cast<int> (std::floor (ppq / 4.0)));
        // Unpin first: a new parasite has no history with this source, even if
        // the performer is holding the freeze switch down.
        engine.setRelationshipFrozen (false);
        engine.setRelationshipTransitionsPaused (false);
        engine.clearCollapseMods();
        engine.applyPerformanceReseed (next, bar);

        pendingMutate_ = false;
        dnaEdited_ = false;
        state_.dormant = false;
        state_.collapsePhase = CollapsePhase::None;
        state_.collapseElapsedBeats = 0.0;
        state_.dormantBeforeSilence = false;
        state_.collapsingBeforeSilence = false;
        state_.lastMutateOp = -1;

        if (state_.mode == Mode::Silenced)
        {
            // Reborn behind the mute; it will be heard when the mute lifts.
        }
        else if (state_.freezeLatched)
        {
            state_.mode = Mode::Frozen;
            engine.setDnaEvolutionPaused (true);
            engine.setRelationshipFrozen (true);
        }
        else
        {
            state_.mode = Mode::Normal;
            engine.setDnaEvolutionPaused (false);
            engine.snapDnaBaselines();
        }

        char buf[128];
        std::snprintf (buf, sizeof (buf), "RESEED old=%llu new=%llu",
                       static_cast<unsigned long long> (oldSeed),
                       static_cast<unsigned long long> (next));
        push (ppq, Command::Reseed, buf);
    }

    void enterSilence (double ppq, Engine& engine) noexcept
    {
        if (state_.mode == Mode::Silenced)
            return;
        state_.frozenBeforeSilence = state_.freezeLatched;
        state_.collapsingBeforeSilence = state_.mode == Mode::Collapsing;
        state_.dormantBeforeSilence = state_.dormant;
        state_.mode = Mode::Silenced;
        state_.silenceActive = true;
        pendingMutate_ = false;

        engine.setResponsesEnabled (false);
        engine.clearPendingResponse();
        engine.stopVoiceSafely();
        engine.setDnaEvolutionPaused (true);
        push (ppq, Command::SilenceOn, "SILENCE");
    }

    void exitSilence (double ppq, Engine& engine) noexcept
    {
        if (state_.mode != Mode::Silenced)
        {
            push (ppq, Command::SilenceOff, "UNSILENCE_NOOP");
            return;
        }
        state_.silenceActive = false;

        if (state_.collapsingBeforeSilence)
        {
            state_.mode = Mode::Collapsing;
            engine.applyCollapsePhase (
                static_cast<uint8_t> (static_cast<int> (state_.collapsePhase)));
            engine.setRelationshipTransitionsPaused (true);
            engine.setDnaEvolutionPaused (true);
            push (ppq, Command::SilenceOff, "UNSILENCE_COLLAPSE_RESUME");
        }
        else if (state_.dormant)
        {
            state_.mode = state_.freezeLatched ? Mode::Frozen : Mode::Collapsed;
            engine.applyCollapsePhase (4);
            engine.setDnaEvolutionPaused (true);
            push (ppq, Command::SilenceOff, "UNSILENCE_DORMANT");
        }
        else if (state_.freezeLatched)
        {
            state_.mode = Mode::Frozen;
            engine.setDnaEvolutionPaused (true);
            engine.snapDnaBaselines();
            push (ppq, Command::SilenceOff, "UNSILENCE_FROZEN");
        }
        else
        {
            state_.mode = Mode::Normal;
            engine.setDnaEvolutionPaused (false);
            engine.snapDnaBaselines();
            push (ppq, Command::SilenceOff, "UNSILENCE");
        }

        state_.frozenBeforeSilence = false;
        state_.collapsingBeforeSilence = false;
        state_.dormantBeforeSilence = false;
        state_.lastTickPpq = ppq;
        syncEngine (engine);
    }

    ParasitePerfState state_;
    bool pendingMutate_ = false;
    bool seedDirty_ = false;
    bool traceEnabled_ = false;
    bool dnaEdited_ = false;
    std::vector<PerfTraceEvent> events_;
    pfl::generative::DeterministicRNG reseedRng_;
};

} // namespace pfl::parasite_perf

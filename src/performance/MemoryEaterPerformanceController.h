#pragma once

#include "MemoryEaterPerformanceTypes.h"
#include "dsp/MemoryEaterEngine.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace pfl::memory_perf
{

/**
 * Memory Eater Stage 4 performance state machine.
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 *
 * FREEZE: lock ecology library; keep recalling.
 * SILENCE: mute all plugin output; ring keeps listening; ecology paused.
 * MUTATE: one bounded manual descendant (works while frozen).
 * COLLAPSE: 24-beat REMEMBER→ERODE→DEVOUR→RESIDUE forgetting arc.
 * RESEED: new personality; preserve ecology/genealogy.
 */
class MemoryEaterPerformanceController
{
public:
    static constexpr double kCollapseBeats = 24.0;
    static constexpr double kCollapsePhaseBeats = 6.0;
    static constexpr int kPerformanceEngineVersion = memory_perf::kPerformanceEngineVersion;

    void reset (uint64_t masterSeed) noexcept
    {
        state_ = {};
        state_.currentSeed = masterSeed == 0 ? 1ull : masterSeed;
        pendingMutate_ = false;
        events_.clear();
        seedDirty_ = false;
        lastDevourForgetBeat_ = -1.0e9;
        mutateSelRng_ = pfl::generative::DeterministicRNG::derived (state_.currentSeed, 0x4D53454Cull); // MSEL
        mutateChildRng_ = pfl::generative::DeterministicRNG::derived (state_.currentSeed, 0x4D434850ull); // MCHP
        collapseRng_ = pfl::generative::DeterministicRNG::derived (state_.currentSeed, 0x434C5053ull); // CLPS
        reseedRng_ = pfl::generative::DeterministicRNG::derived (state_.currentSeed, 0x52534544ull); // RSED
    }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }
    const std::vector<PerfTraceEvent>& events() const noexcept { return events_; }
    void clearEvents() noexcept { events_.clear(); }

    const MemoryPerfState& state() const noexcept { return state_; }
    Mode mode() const noexcept { return state_.mode; }
    uint64_t currentSeed() const noexcept { return state_.currentSeed; }

    bool takeSeedDirty (uint64_t& outSeed) noexcept
    {
        if (! seedDirty_)
            return false;
        seedDirty_ = false;
        outSeed = state_.currentSeed;
        return true;
    }

    void trigger (Command cmd, double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        switch (cmd)
        {
            case Command::SilenceOn: enterSilence (ppq, engine); break;
            case Command::SilenceOff: exitSilence (ppq, engine); break;
            case Command::Collapse:
                if (state_.mode != Mode::Silenced)
                    beginCollapse (ppq, engine);
                break;
            case Command::FreezeOn:
                state_.freezeLatched = true;
                if (state_.mode == Mode::Normal || state_.mode == Mode::Collapsed)
                    enterFreeze (ppq, engine);
                else if (state_.mode == Mode::Silenced)
                    push (ppq, Command::FreezeOn, "FREEZE_LATCH_UNDER_SILENCE");
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

    /** Advance collapse with musical delta while playing; flush queued mutate. */
    void tick (double ppq, bool playing, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        // Seek / large discontinuity: cancel mid-collapse; keep surviving ecology.
        if (state_.lastTickPpq >= 0.0)
        {
            const double jump = ppq - state_.lastTickPpq;
            if (jump < -0.01 || jump > 8.0)
            {
                if (state_.mode == Mode::Collapsing)
                {
                    push (ppq, Command::Collapse, "COLLAPSE_CANCEL_ON_SEEK");
                    state_.mode = state_.freezeLatched ? Mode::Frozen : Mode::Normal;
                    state_.collapsePhase = CollapsePhase::None;
                    state_.collapseElapsedBeats = 0.0;
                    engine.setLifecycleDecayMultiplier (1.0f);
                    engine.setCollapseRememberBias (false);
                }
                pendingMutate_ = false;
            }
        }

        if (state_.mode == Mode::Silenced)
        {
            state_.lastTickPpq = ppq;
            syncEngine (engine);
            return;
        }

        if (playing)
        {
            if (pendingMutate_ && (state_.mode == Mode::Frozen || state_.mode == Mode::Collapsed
                                   || state_.mode == Mode::Normal))
            {
                flushMutate (ppq, engine);
            }

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

    void syncEngine (pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        const bool silenced = state_.mode == Mode::Silenced;
        const bool frozen = state_.mode == Mode::Frozen;
        const bool collapsing = state_.mode == Mode::Collapsing;
        const bool collapsed = state_.mode == Mode::Collapsed;

        // FREEZE / SILENCE / COLLAPSE pause autonomous learning.
        const bool lockLearning = frozen || silenced || collapsing || collapsed;
        engine.setPromoteEnabled (! lockLearning);
        engine.setCaptureEnabled (! lockLearning);

        // FREEZE: stop ring writing. SILENCE: keep listening.
        if (silenced)
            engine.setRingWriteEnabled (true);
        else if (frozen || collapsing || collapsed)
            engine.setRingWriteEnabled (false);
        else
            engine.setRingWriteEnabled (true);

        // Lifecycle: paused under freeze/silence/collapsed; active under collapse (boosted) and normal.
        if (silenced || frozen || collapsed)
        {
            engine.setLifecycleEnabled (false);
            engine.setLifecycleDecayMultiplier (1.0f);
        }
        else if (collapsing)
        {
            engine.setLifecycleEnabled (true);
            applyCollapseDecay (engine);
        }
        else
        {
            engine.setLifecycleEnabled (true);
            engine.setLifecycleDecayMultiplier (1.0f);
        }

        // SILENCE pauses scheduling; FREEZE keeps recalling.
        engine.setScheduleEnabled (! silenced);

        engine.setCollapseRememberBias (collapsing
                                        && state_.collapsePhase == CollapsePhase::Remember);

        if (state_.silenceActive)
            engine.setSilenceActive (true);
    }

    void applyCollapseDecay (pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        // Prefer explicit force-forget for audible population loss over mass strength wipe.
        switch (state_.collapsePhase)
        {
            case CollapsePhase::Remember:
                engine.setLifecycleDecayMultiplier (0.15f);
                break;
            case CollapsePhase::Erode:
                engine.setLifecycleDecayMultiplier (1.8f);
                break;
            case CollapsePhase::Devour:
                engine.setLifecycleDecayMultiplier (2.5f);
                break;
            case CollapsePhase::Residue:
                engine.setLifecycleDecayMultiplier (1.0f);
                break;
            default:
                engine.setLifecycleDecayMultiplier (1.0f);
                break;
        }
    }

    void enterFreeze (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        state_.mode = Mode::Frozen;
        state_.freezeLatched = true;
        engine.cancelActiveCapture();
        // If current recall is from the ring, terminate — ring will be cleared.
        if (engine.voiceActive() && engine.ecology().activeSlot() < 0)
            engine.terminateActiveRecall();
        engine.clearShortTermRing();
        engine.resyncEcologyTimeline (ppq);
        char buf[96];
        std::snprintf (buf, sizeof (buf), "FREEZE slots=%d", engine.ecology().occupiedCount());
        push (ppq, Command::FreezeOn, buf);
    }

    void exitFreeze (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        state_.mode = Mode::Normal;
        state_.freezeLatched = false;
        engine.clearShortTermRing(); // resume capture fresh; no backlog of frozen-time audio
        engine.resyncEcologyTimeline (ppq);
        engine.snapScheduler (ppq);
        push (ppq, Command::FreezeOff, "UNFREEZE");
    }

    void beginCollapse (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Collapsed)
            return;

        if (state_.mode == Mode::Frozen)
        {
            // Override freeze for the arc; keep latch so residue can re-freeze if host still wants it.
            push (ppq, Command::Collapse, "COLLAPSE_OVERRIDES_FREEZE");
        }

        state_.mode = Mode::Collapsing;
        state_.collapsePhase = CollapsePhase::Remember;
        state_.collapseElapsedBeats = 0.0;
        state_.lastTickPpq = ppq;
        state_.residueMemoryId = -1;
        pendingMutate_ = false;
        lastDevourForgetBeat_ = ppq;
        engine.cancelActiveCapture();
        if (engine.voiceActive() && engine.ecology().activeSlot() < 0)
            engine.terminateActiveRecall();
        engine.clearShortTermRing();
        engine.resyncEcologyTimeline (ppq);

        char buf[96];
        std::snprintf (buf, sizeof (buf), "COLLAPSE_START memories=%d",
                       engine.ecology().occupiedCount());
        push (ppq, Command::Collapse, buf);
    }

    void updateCollapsePhase (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        const int phaseIdx = std::clamp (
            static_cast<int> (state_.collapseElapsedBeats / kCollapsePhaseBeats), 0, 3);
        const auto phase = static_cast<CollapsePhase> (phaseIdx + 1);
        if (phase != state_.collapsePhase)
        {
            state_.collapsePhase = phase;
            char buf[96];
            std::snprintf (buf, sizeof (buf), "COLLAPSE_%s memories=%d",
                           collapsePhaseName (phase), engine.ecology().occupiedCount());
            push (ppq, Command::Collapse, buf);
        }

        // Devour: periodic force-forget so population contracts audibly
        if (state_.collapsePhase == CollapsePhase::Devour
            && ppq - lastDevourForgetBeat_ >= 1.5)
        {
            lastDevourForgetBeat_ = ppq;
            if (engine.ecology().occupiedCount() > 2)
                engine.forceForgetSteps (ppq, 1);
        }
        else if (state_.collapsePhase == CollapsePhase::Erode
                 && engine.ecology().occupiedCount() > 4
                 && ppq - lastDevourForgetBeat_ >= 3.0)
        {
            lastDevourForgetBeat_ = ppq;
            engine.forceForgetSteps (ppq, 1);
        }
    }

    void finishCollapse (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        const int before = engine.ecology().occupiedCount();
        int residue = -1;
        if (before > 0)
        {
            // Deterministic chance of total amnesia (~8%) only for tiny ecologies
            const float u = collapseRng_.nextFloat();
            if (u < 0.08f && before == 1)
            {
                engine.clearAllMemories (ppq);
                residue = -1;
            }
            else
            {
                residue = engine.enforceResidue (ppq);
            }
        }

        state_.residueMemoryId = residue;
        state_.collapsePhase = CollapsePhase::Residue;
        state_.collapseElapsedBeats = kCollapseBeats;
        engine.setLifecycleDecayMultiplier (1.0f);
        engine.setCollapseRememberBias (false);
        engine.clearShortTermRing();
        engine.resyncEcologyTimeline (ppq);
        engine.snapScheduler (ppq);

        // Re-freeze residue if FREEZE remains latched (host toggle still on).
        if (state_.freezeLatched)
            state_.mode = Mode::Frozen;
        else
            state_.mode = Mode::Collapsed;

        char buf[96];
        std::snprintf (buf, sizeof (buf), "RESIDUE memory=%d", residue);
        push (ppq, Command::Collapse, buf);
    }

    void requestMutate (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Silenced)
            return;
        flushMutate (ppq, engine);
    }

    void flushMutate (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        pendingMutate_ = false;
        if (engine.ecology().occupiedCount() <= 0)
        {
            push (ppq, Command::Mutate, "MUTATE NO_ELIGIBLE_MEMORY");
            state_.lastMutateParentId = -1;
            state_.lastMutateChildId = -1;
            return;
        }

        // Snapshot parent ids before child birth
        int parentId = -1;
        // Selection happens inside forceManualDescendant; re-select for logging by peeking after
        const int child = engine.forceManualDescendant (
            ppq, engine.currentMemoryParam(), mutateSelRng_, mutateChildRng_);
        if (child < 0)
        {
            push (ppq, Command::Mutate, "MUTATE NO_ELIGIBLE_MEMORY");
            state_.lastMutateParentId = -1;
            state_.lastMutateChildId = -1;
            return;
        }

        // Find parent from child metadata
        for (int i = 0; i < engine.ecology().numSlots(); ++i)
        {
            const auto& s = engine.ecology().slot (i);
            if (s.valid && s.memoryId == child)
            {
                parentId = s.parentMemoryId;
                break;
            }
        }

        state_.lastMutateParentId = parentId;
        state_.lastMutateChildId = child;
        char buf[128];
        std::snprintf (buf, sizeof (buf), "MUTATE parent=%d child=%d", parentId, child);
        push (ppq, Command::Mutate, buf);
        // Does not unfreeze
    }

    void beginReseed (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        ++state_.reseedCount;
        const uint64_t oldSeed = state_.currentSeed;
        const uint64_t next = 1ull + static_cast<uint64_t> (reseedRng_.nextFloat() * 999998.0f);
        state_.currentSeed = next;
        seedDirty_ = true;

        engine.cancelActiveCapture();
        if (engine.voiceActive())
            engine.terminateActiveRecall();
        engine.setSeed (next);

        mutateSelRng_ = pfl::generative::DeterministicRNG::derived (next, 0x4D53454Cull);
        mutateChildRng_ = pfl::generative::DeterministicRNG::derived (next, 0x4D434850ull);
        collapseRng_ = pfl::generative::DeterministicRNG::derived (next, 0x434C5053ull);
        reseedRng_ = pfl::generative::DeterministicRNG::derived (next, 0x52534544ull);

        // Exit collapse residue into Normal (preserve freeze/silence)
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Collapsed)
        {
            state_.mode = Mode::Normal;
            state_.collapsePhase = CollapsePhase::None;
            state_.collapseElapsedBeats = 0.0;
            engine.setLifecycleDecayMultiplier (1.0f);
            engine.setCollapseRememberBias (false);
        }

        if (state_.freezeLatched && state_.mode == Mode::Normal)
            state_.mode = Mode::Frozen;

        char buf[128];
        std::snprintf (buf, sizeof (buf), "RESEED old=%llu new=%llu",
                       static_cast<unsigned long long> (oldSeed),
                       static_cast<unsigned long long> (next));
        push (ppq, Command::Reseed, buf);
    }

    void enterSilence (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        state_.frozenBeforeSilence = (state_.mode == Mode::Frozen) || state_.freezeLatched;
        state_.collapsingBeforeSilence = (state_.mode == Mode::Collapsing);
        collapsedBeforeSilence_ = (state_.mode == Mode::Collapsed);
        state_.silenceActive = true;
        state_.mode = Mode::Silenced;
        pendingMutate_ = false;
        engine.terminateActiveRecall();
        engine.cancelActiveCapture();
        engine.setSilenceActive (true);
        engine.resyncEcologyTimeline (ppq);
        engine.snapScheduler (ppq);
        push (ppq, Command::SilenceOn, "SILENCE");
    }

    void exitSilence (double ppq, pfl::dsp::MemoryEaterEngine& engine) noexcept
    {
        state_.silenceActive = false;
        engine.beginUnsilenceFade();
        engine.resyncEcologyTimeline (ppq);
        engine.snapScheduler (ppq); // no missed-opportunity backlog

        if (state_.collapsingBeforeSilence)
        {
            state_.mode = Mode::Collapsing; // resume same phase / elapsed
            push (ppq, Command::SilenceOff, "UNSILENCE_COLLAPSE_RESUME");
        }
        else if (collapsedBeforeSilence_)
        {
            state_.mode = Mode::Collapsed;
            push (ppq, Command::SilenceOff, "UNSILENCE");
        }
        else if (state_.freezeLatched)
        {
            state_.mode = Mode::Frozen;
            push (ppq, Command::SilenceOff, "UNSILENCE");
        }
        else
        {
            state_.mode = Mode::Normal;
            push (ppq, Command::SilenceOff, "UNSILENCE");
        }

        state_.frozenBeforeSilence = false;
        state_.collapsingBeforeSilence = false;
        collapsedBeforeSilence_ = false;
    }

    MemoryPerfState state_;
    bool pendingMutate_ = false;
    bool seedDirty_ = false;
    bool traceEnabled_ = false;
    bool collapsedBeforeSilence_ = false;
    double lastDevourForgetBeat_ = -1.0e9;
    std::vector<PerfTraceEvent> events_;
    pfl::generative::DeterministicRNG mutateSelRng_, mutateChildRng_, collapseRng_, reseedRng_;
};

} // namespace pfl::memory_perf

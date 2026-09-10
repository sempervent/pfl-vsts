#pragma once

#include "RuinEnginePerformanceTypes.h"
#include "dsp/RuinEngine.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace pfl::ruin_perf
{

/**
 * Ruin Engine Stage 4 performance state machine.
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 */
class RuinEnginePerformanceController
{
public:
    static constexpr double kCollapseBeats = 24.0;
    static constexpr double kCollapsePhaseBeats = 6.0;
    static constexpr int kPerformanceEngineVersion = ruin_perf::kPerformanceEngineVersion;

    void reset (uint64_t masterSeed) noexcept
    {
        state_ = {};
        state_.currentSeed = masterSeed == 0 ? 1ull : masterSeed;
        pendingMutate_ = false;
        events_.clear();
        seedDirty_ = false;
        manualRng_ = pfl::generative::DeterministicRNG::derived (state_.currentSeed, 0x4D555441ull); // MUTA
        reseedRng_ = pfl::generative::DeterministicRNG::derived (state_.currentSeed, 0x52534544ull); // RSED
    }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }
    const std::vector<PerfTraceEvent>& events() const noexcept { return events_; }
    void clearEvents() noexcept { events_.clear(); }

    const RuinPerfState& state() const noexcept { return state_; }
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

    void trigger (Command cmd, double ppq, pfl::dsp::RuinEngine& engine) noexcept
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
                if (state_.mode == Mode::Normal)
                    enterFreeze (ppq, engine);
                else if (state_.mode == Mode::Silenced)
                    push (ppq, Command::FreezeOn, "FREEZE_LATCH_UNDER_SILENCE");
                break;
            case Command::FreezeOff:
                state_.freezeLatched = false;
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
    void tick (double ppq, bool playing, pfl::dsp::RuinEngine& engine) noexcept
    {
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
                // Immediate when frozen/collapsed; on eval boundary when normal (approx: every tick OK if pending)
                if (state_.mode != Mode::Normal || std::floor (ppq / 4.0) != std::floor (state_.lastTickPpq / 4.0))
                    flushMutate (ppq, engine);
            }

            if (state_.mode == Mode::Collapsing)
            {
                double delta = 0.0;
                if (state_.lastTickPpq >= 0.0)
                {
                    delta = ppq - state_.lastTickPpq;
                    if (delta < 0.0 || delta > 8.0) // seek: do not fabricate
                        delta = 0.0;
                }
                state_.collapseElapsedBeats += delta;
                if (delta > 0.0)
                {
                    const int phaseIdx = std::clamp (
                        static_cast<int> (state_.collapseElapsedBeats / kCollapsePhaseBeats), 0, 3);
                    engine.addCollapseWear (static_cast<float> (delta),
                                           0.35f + 0.15f * static_cast<float> (phaseIdx));
                }
                updateCollapsePhase (ppq, engine);

                if (state_.collapseElapsedBeats >= kCollapseBeats)
                {
                    state_.mode = Mode::Collapsed;
                    state_.collapsePhase = CollapsePhase::Residue;
                    engine.setCollapsePhase (CollapsePhase::Residue, 1.0f);
                    push (ppq, Command::Collapse, "COLLAPSE_RESIDUE");
                }
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

    void syncEngine (pfl::dsp::RuinEngine& engine) noexcept
    {
        const bool lock = state_.mode == Mode::Frozen
                       || state_.mode == Mode::Collapsing
                       || state_.mode == Mode::Collapsed
                       || state_.mode == Mode::Silenced;
        state_.evolutionLocked = lock;
        engine.setPerformanceLock (lock && state_.mode != Mode::Collapsing);
        // During collapsing, lock autonomous evolution but allow collapse drive
        if (state_.mode == Mode::Collapsing)
            engine.setPerformanceLock (true); // still lock autonomous; collapse drive separate

        engine.setSilenceActive (state_.silenceActive);

        const bool wearPause = lock || state_.silenceActive;
        engine.setWearPaused (wearPause);

        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Collapsed)
        {
            const float t = static_cast<float> (
                std::clamp (state_.collapseElapsedBeats / kCollapsePhaseBeats
                                - static_cast<double> (static_cast<int> (state_.collapsePhase) - 1),
                            0.0, 1.0));
            engine.setCollapsePhase (state_.collapsePhase, t);
        }
        else if (state_.mode != Mode::Silenced)
        {
            engine.setCollapsePhase (CollapsePhase::None, 0.0f);
        }
    }

    void enterFreeze (double ppq, pfl::dsp::RuinEngine& engine) noexcept
    {
        state_.mode = Mode::Frozen;
        state_.freezeLatched = true;
        engine.discardPendingStructure();
        push (ppq, Command::FreezeOn, "FREEZE");
        (void) engine;
    }

    void exitFreeze (double ppq, pfl::dsp::RuinEngine& engine) noexcept
    {
        state_.mode = Mode::Normal;
        state_.freezeLatched = false;
        engine.discardPendingStructure();
        push (ppq, Command::FreezeOff, "UNFREEZE");
    }

    void beginCollapse (double ppq, pfl::dsp::RuinEngine& engine) noexcept
    {
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Collapsed)
            return; // ignore re-entry

        if (state_.mode == Mode::Frozen)
        {
            state_.freezeLatched = false;
            push (ppq, Command::Collapse, "COLLAPSE_OVERRIDES_FREEZE");
        }

        state_.mode = Mode::Collapsing;
        state_.collapsePhase = CollapsePhase::Destabilize;
        state_.collapseElapsedBeats = 0.0;
        state_.lastTickPpq = ppq;
        pendingMutate_ = false;
        engine.discardPendingStructure();
        engine.setCollapsePhase (CollapsePhase::Destabilize, 0.0f);
        push (ppq, Command::Collapse, "COLLAPSE_START");
    }

    void updateCollapsePhase (double ppq, pfl::dsp::RuinEngine& engine) noexcept
    {
        const int phaseIdx = std::clamp (
            static_cast<int> (state_.collapseElapsedBeats / kCollapsePhaseBeats), 0, 3);
        const auto phase = static_cast<CollapsePhase> (phaseIdx + 1);
        if (phase != state_.collapsePhase)
        {
            state_.collapsePhase = phase;
            push (ppq, Command::Collapse, collapsePhaseName (phase));
        }
        const float t = static_cast<float> (
            std::fmod (state_.collapseElapsedBeats, kCollapsePhaseBeats) / kCollapsePhaseBeats);
        engine.setCollapsePhase (phase, t);
    }

    void requestMutate (double ppq, pfl::dsp::RuinEngine& engine) noexcept
    {
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Silenced)
            return;
        if (state_.mode == Mode::Frozen || state_.mode == Mode::Collapsed)
            flushMutate (ppq, engine);
        else
            pendingMutate_ = true;
    }

    void flushMutate (double ppq, pfl::dsp::RuinEngine& engine) noexcept
    {
        pendingMutate_ = false;
        const int axis = engine.applyBoundedMutation (manualRng_);
        state_.lastMutateAxis = axis;
        char buf[64];
        std::snprintf (buf, sizeof (buf), "MUTATE_AXIS_%d", axis);
        push (ppq, Command::Mutate, buf);
    }

    void beginReseed (double ppq, pfl::dsp::RuinEngine& engine) noexcept
    {
        ++state_.reseedCount;
        // Deterministic next seed in UI range
        const uint64_t next = 1ull + static_cast<uint64_t> (
            reseedRng_.nextFloat() * 999998.0f);
        state_.currentSeed = next;
        seedDirty_ = true;

        // Preserve wear via engine.setSeed
        engine.beginReseedFade();
        engine.clearMutationOffsets();
        engine.setSeed (next);
        manualRng_ = pfl::generative::DeterministicRNG::derived (next, 0x4D555441ull);
        reseedRng_ = pfl::generative::DeterministicRNG::derived (next, 0x52534544ull);

        // Exit collapse residue into Normal (or keep freeze/silence latches)
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Collapsed)
        {
            state_.mode = Mode::Normal;
            state_.collapsePhase = CollapsePhase::None;
            state_.collapseElapsedBeats = 0.0;
            engine.setCollapsePhase (CollapsePhase::None, 0.0f);
        }
        else if (state_.mode == Mode::Silenced)
        {
            // stay silenced under new seed
        }
        else if (state_.mode == Mode::Frozen)
        {
            // stay frozen
        }
        else
        {
            state_.mode = Mode::Normal;
        }

        // Re-apply freeze latch after collapse exit / normal reseed
        if (state_.freezeLatched && state_.mode == Mode::Normal)
        {
            state_.mode = Mode::Frozen;
            engine.discardPendingStructure();
        }

        engine.softClearDelay();
        push (ppq, Command::Reseed, "RESEED");
    }

    void enterSilence (double ppq, pfl::dsp::RuinEngine& engine) noexcept
    {
        state_.frozenBeforeSilence = (state_.mode == Mode::Frozen);
        state_.collapsingBeforeSilence = (state_.mode == Mode::Collapsing || state_.mode == Mode::Collapsed);
        state_.silenceActive = true;
        state_.mode = Mode::Silenced;
        pendingMutate_ = false;
        engine.softClearDelay();
        engine.discardPendingStructure();
        push (ppq, Command::SilenceOn, "SILENCE");
    }

    void exitSilence (double ppq, pfl::dsp::RuinEngine& engine) noexcept
    {
        state_.silenceActive = false;
        if (state_.collapsingBeforeSilence)
        {
            // Prefer pause policy: restore Collapsed residue (safe) rather than mid-arc
            state_.mode = Mode::Collapsed;
            state_.collapsePhase = CollapsePhase::Residue;
            state_.collapseElapsedBeats = kCollapseBeats;
            engine.setCollapsePhase (CollapsePhase::Residue, 1.0f);
        }
        else if (state_.frozenBeforeSilence || state_.freezeLatched)
        {
            state_.mode = Mode::Frozen;
            state_.freezeLatched = true;
        }
        else
        {
            state_.mode = Mode::Normal;
        }
        state_.frozenBeforeSilence = false;
        state_.collapsingBeforeSilence = false;
        engine.discardPendingStructure();
        engine.beginUnsilenceFade();
        push (ppq, Command::SilenceOff, "UNSILENCE");
    }

    RuinPerfState state_;
    bool pendingMutate_ = false;
    bool seedDirty_ = false;
    bool traceEnabled_ = false;
    std::vector<PerfTraceEvent> events_;
    pfl::generative::DeterministicRNG manualRng_, reseedRng_;
};

} // namespace pfl::ruin_perf

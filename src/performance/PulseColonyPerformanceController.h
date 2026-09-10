#pragma once

#include "PulseColonyPerformanceTypes.h"
#include "dsp/PulseColonyEngine.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace pfl::pulse_perf
{

/**
 * Pulse Colony Stage 3 performance state machine.
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 *
 * FREEZE: pause DNA evolution + hunger; arbiter/gate continue.
 * SILENCE: mute output (processor); pause evolution + collapse clock.
 * MUTATE: one cell, one bounded MutOp (works while frozen / residue).
 * COLLAPSE: 24-beat SWARM→STARVE→FRACTURE→RESIDUE (≠ DENSITY ramp).
 * RESEED: new three-cell universe; preserve freeze latch; OK under silence.
 */
class PulseColonyPerformanceController
{
public:
    static constexpr double kCollapseBeats = 24.0;
    static constexpr double kCollapsePhaseBeats = 6.0;
    static constexpr int kPerformanceEngineVersion = pulse_perf::kPerformanceEngineVersion;

    void reset (uint64_t masterSeed) noexcept
    {
        state_ = {};
        state_.currentSeed = masterSeed == 0 ? 1ull : masterSeed;
        pendingMutate_ = false;
        pendingMutateRole_ = -1;
        fractureApplied_ = false;
        starveSkitterSuppressed_ = false;
        collapsedBeforeSilence_ = false;
        dnaEdited_ = false;
        events_.clear();
        seedDirty_ = false;
        reseedRng_ = pfl::generative::DeterministicRNG::derived (state_.currentSeed, 0x52534544ull); // RSED
    }

    void setTraceEnabled (bool e) noexcept { traceEnabled_ = e; }
    const std::vector<PerfTraceEvent>& events() const noexcept { return events_; }
    void clearEvents() noexcept { events_.clear(); }

    const PulsePerfState& state() const noexcept { return state_; }
    Mode mode() const noexcept { return state_.mode; }
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

    void trigger (Command cmd, double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
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
    void tick (double ppq, bool playing, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        // Seek / large discontinuity: cancel mid-collapse; keep DNA; clear pending mutate.
        if (state_.lastTickPpq >= 0.0)
        {
            const double jump = ppq - state_.lastTickPpq;
            if (jump < -0.01 || jump > 8.0)
            {
                if (state_.mode == Mode::Collapsing)
                {
                    push (ppq, Command::Collapse, "COLLAPSE_CANCEL_ON_SEEK");
                    cancelCollapseToStable (engine);
                }
                pendingMutate_ = false;
                pendingMutateRole_ = -1;
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
                if (state_.mode != Mode::Normal
                    || std::floor (ppq / 4.0) != std::floor (state_.lastTickPpq / 4.0))
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

    /** Restore stable collapsed/frozen residue after plugin state load (no mid-collapse). */
    void restoreStableMode (int residueRole, bool freezeLatched,
                            pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        state_.freezeLatched = freezeLatched;
        state_.residueRole = residueRole;
        if (residueRole >= 0)
        {
            state_.mode = freezeLatched ? Mode::Frozen : Mode::Collapsed;
            state_.collapsePhase = CollapsePhase::Residue;
            state_.collapseElapsedBeats = kCollapseBeats;
            engine.setSoloRole (residueRole);
            engine.setEvolutionPaused (true);
            engine.clearCollapseParticipation();
            engine.setSwarmPressure (false);
            dnaEdited_ = true;
        }
        else if (freezeLatched)
        {
            state_.mode = Mode::Frozen;
            engine.setEvolutionPaused (true);
        }
        engine.snapEvolutionBaselines();
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

    void syncEngine (pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        const bool silenced = state_.mode == Mode::Silenced;
        const bool frozen = state_.mode == Mode::Frozen;
        const bool collapsing = state_.mode == Mode::Collapsing;
        const bool collapsed = state_.mode == Mode::Collapsed;

        const bool pauseEvo = frozen || silenced || collapsing || collapsed;
        engine.setEvolutionPaused (pauseEvo);
        state_.silenceActive = silenced;

        // Residue / post-collapse freeze: keep solo survivor locked.
        if ((collapsed || frozen) && state_.residueRole >= 0)
            engine.setSoloRole (state_.residueRole);
    }

    void enterFreeze (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        state_.mode = Mode::Frozen;
        state_.freezeLatched = true;
        engine.setEvolutionPaused (true);
        char buf[96];
        std::snprintf (buf, sizeof (buf), "FREEZE gen=%d", engine.generation());
        push (ppq, Command::FreezeOn, buf);
    }

    void exitFreeze (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        state_.freezeLatched = false;
        if (state_.residueRole >= 0)
        {
            // Unfreeze returns to stable residue — do not repopulate without RESEED.
            state_.mode = Mode::Collapsed;
            engine.setEvolutionPaused (true);
            engine.setSoloRole (state_.residueRole);
            engine.snapEvolutionBaselines();
            push (ppq, Command::FreezeOff, "UNFREEZE_TO_RESIDUE");
        }
        else
        {
            state_.mode = Mode::Normal;
            engine.setEvolutionPaused (false);
            engine.setSoloRole (-1);
            engine.snapEvolutionBaselines();
            push (ppq, Command::FreezeOff, "UNFREEZE");
        }
    }

    void beginCollapse (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Collapsed)
            return;

        if (state_.mode == Mode::Frozen)
            push (ppq, Command::Collapse, "COLLAPSE_OVERRIDES_FREEZE");

        state_.mode = Mode::Collapsing;
        state_.collapsePhase = CollapsePhase::Swarm;
        state_.collapseElapsedBeats = 0.0;
        state_.lastTickPpq = ppq;
        state_.residueRole = -1;
        pendingMutate_ = false;
        pendingMutateRole_ = -1;
        fractureApplied_ = false;
        starveSkitterSuppressed_ = false;

        engine.setEvolutionPaused (true);
        engine.setSwarmPressure (true);
        engine.setCollapseParticipation (1.45f, 1.35f, 1.0f);
        engine.setSoloRole (-1);

        push (ppq, Command::Collapse, "COLLAPSE_START SWARM");
    }

    void updateCollapsePhase (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        const int phaseIdx = std::clamp (
            static_cast<int> (state_.collapseElapsedBeats / kCollapsePhaseBeats), 0, 3);
        const auto phase = static_cast<CollapsePhase> (phaseIdx + 1);
        if (phase != state_.collapsePhase)
        {
            state_.collapsePhase = phase;
            applyCollapsePhase (ppq, engine);
            char buf[96];
            std::snprintf (buf, sizeof (buf), "COLLAPSE_%s", collapsePhaseName (phase));
            push (ppq, Command::Collapse, buf);
        }
        else if (phase == CollapsePhase::Starve)
        {
            // Progressive: Ghost first, then Skitter mid-phase
            const double into = state_.collapseElapsedBeats - kCollapsePhaseBeats;
            if (! starveSkitterSuppressed_ && into >= 3.0)
            {
                starveSkitterSuppressed_ = true;
                engine.setCollapseParticipation (0.0f, 0.0f, 1.0f);
                push (ppq, Command::Collapse, "STARVE_SKITTER");
            }
        }
        else if (phase == CollapsePhase::Fracture && ! fractureApplied_)
        {
            applyFracture (ppq, engine);
        }
    }

    void applyCollapsePhase (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        switch (state_.collapsePhase)
        {
            case CollapsePhase::None:
                break;
            case CollapsePhase::Swarm:
                engine.setSwarmPressure (true);
                engine.setCollapseParticipation (1.45f, 1.35f, 1.0f);
                engine.setSoloRole (-1);
                break;
            case CollapsePhase::Starve:
                engine.setSwarmPressure (false);
                // Suppress Ghost first
                engine.setCollapseParticipation (0.0f, 1.0f, 1.0f);
                engine.setSoloRole (-1);
                starveSkitterSuppressed_ = false;
                break;
            case CollapsePhase::Fracture:
                engine.setSwarmPressure (false);
                applyFracture (ppq, engine);
                break;
            case CollapsePhase::Residue:
            {
                engine.setSwarmPressure (false);
                if (! fractureApplied_)
                    applyFracture (ppq, engine);
                int residue = 0;
                if (engine.cellDna (0).windowCount > 0) residue = 0;
                else if (engine.cellDna (1).windowCount > 0) residue = 1;
                else if (engine.cellDna (2).windowCount > 0) residue = 2;
                state_.residueRole = residue;
                for (int r = 0; r < pfl::dsp::PulseColonyEngine::kNumRoles; ++r)
                    if (r != residue)
                        engine.stripRoleWindows (r);
                engine.setSoloRole (residue);
                engine.clearCollapseParticipation();
                break;
            }
        }
    }

    void applyFracture (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        if (fractureApplied_)
            return;
        fractureApplied_ = true;
        dnaEdited_ = true;
        engine.setSwarmPressure (false);
        engine.stripRoleWindows (2); // Ghost
        engine.stripRoleWindows (1); // Skitter
        // Bounded destructive ops on Anchor DNA
        if (engine.cellDna (0).windowCount > 0)
        {
            engine.manualMutate (0);
            if (engine.cellDna (0).windowCount > 2)
                engine.manualMutate (0);
        }
        engine.setCollapseParticipation (0.0f, 0.0f, 1.0f);
        engine.setSoloRole (0);
        push (ppq, Command::Collapse, "FRACTURE_ANCHOR");
    }

    void finishCollapse (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        // Prefer Anchor residue; else Skitter; else Ghost
        int residue = -1;
        if (engine.cellDna (0).windowCount > 0)
            residue = 0;
        else if (engine.cellDna (1).windowCount > 0)
            residue = 1;
        else if (engine.cellDna (2).windowCount > 0)
            residue = 2;

        if (residue < 0)
        {
            // Ensure at least Anchor empty spine stays selectable for silence
            residue = 0;
        }

        // Lock non-survivors
        for (int r = 0; r < pfl::dsp::PulseColonyEngine::kNumRoles; ++r)
        {
            if (r != residue)
                engine.stripRoleWindows (r);
        }

        state_.residueRole = residue;
        state_.collapsePhase = CollapsePhase::Residue;
        state_.collapseElapsedBeats = kCollapseBeats;
        engine.setSwarmPressure (false);
        engine.clearCollapseParticipation();
        engine.setSoloRole (residue);
        engine.setEvolutionPaused (true);
        dnaEdited_ = true;

        if (state_.freezeLatched)
            state_.mode = Mode::Frozen;
        else
            state_.mode = Mode::Collapsed;

        char buf[96];
        std::snprintf (buf, sizeof (buf), "RESIDUE role=%d", residue);
        push (ppq, Command::Collapse, buf);
    }

    void cancelCollapseToStable (pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        // Mid-collapse seek → stable snapshot, collapse inactive; keep DNA as-is.
        if (fractureApplied_ || state_.collapseElapsedBeats >= 2.0 * kCollapsePhaseBeats)
        {
            // Already fractured: settle into residue-like solo
            int residue = state_.residueRole;
            if (residue < 0)
            {
                if (engine.cellDna (0).windowCount > 0) residue = 0;
                else if (engine.cellDna (1).windowCount > 0) residue = 1;
                else residue = 0;
            }
            state_.residueRole = residue;
            engine.setSoloRole (residue);
            engine.clearCollapseParticipation();
            engine.setSwarmPressure (false);
            state_.mode = state_.freezeLatched ? Mode::Frozen : Mode::Collapsed;
            state_.collapsePhase = CollapsePhase::Residue;
            state_.collapseElapsedBeats = kCollapseBeats;
        }
        else
        {
            engine.clearCollapseParticipation();
            engine.setSwarmPressure (false);
            engine.setSoloRole (-1);
            state_.mode = state_.freezeLatched ? Mode::Frozen : Mode::Normal;
            state_.collapsePhase = CollapsePhase::None;
            state_.collapseElapsedBeats = 0.0;
            state_.residueRole = -1;
        }
        fractureApplied_ = false;
        starveSkitterSuppressed_ = false;
    }

    void requestMutate (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Silenced)
            return;
        if (state_.mode == Mode::Frozen || state_.mode == Mode::Collapsed)
            flushMutate (ppq, engine);
        else
        {
            pendingMutate_ = true;
            pendingMutateRole_ = -1;
        }
    }

    void flushMutate (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        pendingMutate_ = false;
        const int forced = (state_.mode == Mode::Collapsed && state_.residueRole >= 0)
                               ? state_.residueRole
                               : pendingMutateRole_;
        pendingMutateRole_ = -1;

        const int role = engine.manualMutate (forced);
        state_.lastMutateRole = role;
        state_.lastMutateOp = engine.lastManualMutateOp();
        if (role < 0)
        {
            push (ppq, Command::Mutate, "MUTATE NO_ELIGIBLE_CELL");
            return;
        }
        dnaEdited_ = true;
        char buf[128];
        std::snprintf (buf, sizeof (buf), "MUTATE role=%d op=%s", role, engine.lastManualMutateOpName());
        push (ppq, Command::Mutate, buf);
        // Does not unfreeze
    }

    void beginReseed (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        ++state_.reseedCount;
        const uint64_t oldSeed = state_.currentSeed;
        const uint64_t next = 1ull + static_cast<uint64_t> (reseedRng_.nextFloat() * 999998.0f);
        state_.currentSeed = next;
        seedDirty_ = true;

        engine.setSeed (next);
        engine.forceRebuild (static_cast<int> (std::floor (ppq / 4.0)));
        engine.clearCollapseParticipation();
        engine.setSwarmPressure (false);
        engine.setSoloRole (-1);
        reseedRng_ = pfl::generative::DeterministicRNG::derived (next, 0x52534544ull);

        pendingMutate_ = false;
        pendingMutateRole_ = -1;
        fractureApplied_ = false;
        starveSkitterSuppressed_ = false;
        dnaEdited_ = false;
        state_.residueRole = -1;
        state_.lastMutateRole = -1;
        state_.lastMutateOp = -1;

        if (state_.mode == Mode::Collapsing || state_.mode == Mode::Collapsed)
        {
            state_.mode = Mode::Normal;
            state_.collapsePhase = CollapsePhase::None;
            state_.collapseElapsedBeats = 0.0;
        }

        if (state_.mode == Mode::Silenced)
        {
            // stay silenced under new seed
        }
        else if (state_.freezeLatched)
        {
            state_.mode = Mode::Frozen;
            engine.setEvolutionPaused (true);
        }
        else if (state_.mode != Mode::Silenced)
        {
            state_.mode = Mode::Normal;
            engine.setEvolutionPaused (false);
        }

        char buf[128];
        std::snprintf (buf, sizeof (buf), "RESEED old=%llu new=%llu",
                       static_cast<unsigned long long> (oldSeed),
                       static_cast<unsigned long long> (next));
        push (ppq, Command::Reseed, buf);
    }

    void enterSilence (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        state_.frozenBeforeSilence = (state_.mode == Mode::Frozen) || state_.freezeLatched;
        state_.collapsingBeforeSilence = (state_.mode == Mode::Collapsing);
        collapsedBeforeSilence_ = (state_.mode == Mode::Collapsed);
        state_.silenceActive = true;
        state_.mode = Mode::Silenced;
        pendingMutate_ = false;
        pendingMutateRole_ = -1;
        engine.setEvolutionPaused (true);
        push (ppq, Command::SilenceOn, "SILENCE");
        (void) engine;
    }

    void exitSilence (double ppq, pfl::dsp::PulseColonyEngine& engine) noexcept
    {
        state_.silenceActive = false;

        if (state_.collapsingBeforeSilence)
        {
            state_.mode = Mode::Collapsing;
            engine.setEvolutionPaused (true);
            push (ppq, Command::SilenceOff, "UNSILENCE_COLLAPSE_RESUME");
        }
        else if (collapsedBeforeSilence_ || state_.residueRole >= 0)
        {
            state_.mode = (state_.freezeLatched ? Mode::Frozen : Mode::Collapsed);
            if (state_.residueRole >= 0)
                engine.setSoloRole (state_.residueRole);
            engine.setEvolutionPaused (true);
            engine.snapEvolutionBaselines();
            push (ppq, Command::SilenceOff, "UNSILENCE_RESIDUE");
        }
        else if (state_.freezeLatched)
        {
            state_.mode = Mode::Frozen;
            engine.setEvolutionPaused (true);
            engine.snapEvolutionBaselines();
            push (ppq, Command::SilenceOff, "UNSILENCE");
        }
        else
        {
            state_.mode = Mode::Normal;
            engine.setEvolutionPaused (false);
            engine.snapEvolutionBaselines();
            push (ppq, Command::SilenceOff, "UNSILENCE");
        }

        state_.frozenBeforeSilence = false;
        state_.collapsingBeforeSilence = false;
        collapsedBeforeSilence_ = false;
        syncEngine (engine);
    }

    PulsePerfState state_;
    bool pendingMutate_ = false;
    int pendingMutateRole_ = -1;
    bool seedDirty_ = false;
    bool traceEnabled_ = false;
    bool collapsedBeforeSilence_ = false;
    bool fractureApplied_ = false;
    bool starveSkitterSuppressed_ = false;
    bool dnaEdited_ = false;
    std::vector<PerfTraceEvent> events_;
    pfl::generative::DeterministicRNG reseedRng_;
};

} // namespace pfl::pulse_perf

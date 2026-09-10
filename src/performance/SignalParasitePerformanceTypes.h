#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pfl::parasite_perf
{

inline constexpr int kPerformanceEngineVersion = 1;

enum class Command : uint8_t
{
    FreezeOn = 0,
    FreezeOff,
    Mutate,
    Collapse,
    Reseed,
    SilenceOn,
    SilenceOff
};

enum class Mode : uint8_t
{
    Normal = 0,
    Frozen,
    Collapsing,
    Collapsed,
    Silenced
};

/** COLLAPSE arc: CLING → FEVER → WITHDRAW → DORMANT (6 beats each = 24). */
enum class CollapsePhase : uint8_t
{
    None = 0,
    Cling,
    Fever,
    Withdraw,
    Dormant
};

struct PerfTraceEvent
{
    double ppq = 0.0;
    Command command = Command::FreezeOn;
    std::string detail;
};

inline const char* commandName (Command c) noexcept
{
    switch (c)
    {
        case Command::FreezeOn: return "FREEZE_ON";
        case Command::FreezeOff: return "FREEZE_OFF";
        case Command::Mutate: return "MUTATE";
        case Command::Collapse: return "COLLAPSE";
        case Command::Reseed: return "RESEED";
        case Command::SilenceOn: return "SILENCE_ON";
        case Command::SilenceOff: return "SILENCE_OFF";
    }
    return "UNKNOWN";
}

inline const char* modeName (Mode m) noexcept
{
    switch (m)
    {
        case Mode::Normal: return "NORMAL";
        case Mode::Frozen: return "FROZEN";
        case Mode::Collapsing: return "COLLAPSING";
        case Mode::Collapsed: return "COLLAPSED";
        case Mode::Silenced: return "SILENCED";
    }
    return "UNKNOWN";
}

inline const char* collapsePhaseName (CollapsePhase p) noexcept
{
    switch (p)
    {
        case CollapsePhase::None: return "NONE";
        case CollapsePhase::Cling: return "CLING";
        case CollapsePhase::Fever: return "FEVER";
        case CollapsePhase::Withdraw: return "WITHDRAW";
        case CollapsePhase::Dormant: return "DORMANT";
    }
    return "?";
}

/**
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 *
 * FREEZE   — hold ParasiteDNA *and* the relationship state; the parasite keeps
 *            listening and keeps answering, in the manner it was holding.
 * SILENCE  — mute the plugin output; no answers launch, no backlog builds.
 * COLLAPSE — 24 beats of CLING → FEVER → WITHDRAW → DORMANT, ending dormant.
 * MUTATE   — one bounded DNA touch; never a density control.
 * RESEED   — a new parasite on the same source; relationship back to LURKING.
 */
struct ParasitePerfState
{
    Mode mode = Mode::Normal;
    CollapsePhase collapsePhase = CollapsePhase::None;
    double collapseElapsedBeats = 0.0;
    double lastTickPpq = -1.0;
    bool silenceActive = false;
    bool freezeLatched = false;
    bool frozenBeforeSilence = false;
    bool collapsingBeforeSilence = false;
    bool dormantBeforeSilence = false;
    bool dormant = false;
    uint64_t reseedCount = 0;
    uint64_t currentSeed = 2002;
    int lastMutateOp = -1;
};

} // namespace pfl::parasite_perf

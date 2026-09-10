#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pfl::memory_perf
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

/** COLLAPSE arc: REMEMBER → ERODE → DEVOUR → RESIDUE (6 beats each = 24). */
enum class CollapsePhase : uint8_t
{
    None = 0,
    Remember,
    Erode,
    Devour,
    Residue
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
        default: return "UNKNOWN";
    }
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
        default: return "UNKNOWN";
    }
}

inline const char* collapsePhaseName (CollapsePhase p) noexcept
{
    switch (p)
    {
        case CollapsePhase::None: return "NONE";
        case CollapsePhase::Remember: return "REMEMBER";
        case CollapsePhase::Erode: return "ERODE";
        case CollapsePhase::Devour: return "DEVOUR";
        case CollapsePhase::Residue: return "RESIDUE";
        default: return "?";
    }
}

/**
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 * FREEZE = lock ecology library, keep recalling.
 * SILENCE = mute output; ring keeps listening; ecology paused.
 * RESEED preserves ecology/genealogy.
 */
struct MemoryPerfState
{
    Mode mode = Mode::Normal;
    CollapsePhase collapsePhase = CollapsePhase::None;
    double collapseElapsedBeats = 0.0;
    double lastTickPpq = -1.0;
    bool silenceActive = false;
    bool freezeLatched = false;
    bool frozenBeforeSilence = false;
    bool collapsingBeforeSilence = false;
    uint64_t reseedCount = 0;
    uint64_t currentSeed = 3003;
    int lastMutateParentId = -1;
    int lastMutateChildId = -1;
    int residueMemoryId = -1;
};

} // namespace pfl::memory_perf

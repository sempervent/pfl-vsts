#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pfl::pulse_perf
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

/** COLLAPSE arc: SWARM → STARVE → FRACTURE → RESIDUE (6 beats each = 24). */
enum class CollapsePhase : uint8_t
{
    None = 0,
    Swarm,
    Starve,
    Fracture,
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
        case CollapsePhase::Swarm: return "SWARM";
        case CollapsePhase::Starve: return "STARVE";
        case CollapsePhase::Fracture: return "FRACTURE";
        case CollapsePhase::Residue: return "RESIDUE";
    }
    return "?";
}

/**
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 * FREEZE = pause DNA evolution + hunger; arbiter/gate continue.
 * SILENCE = mute output; evolution + collapse clock paused.
 * COLLAPSE = SWARM→STARVE→FRACTURE→RESIDUE (≠ DENSITY ramp).
 */
struct PulsePerfState
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
    uint64_t currentSeed = 2002;
    int lastMutateRole = -1;
    int residueRole = -1;
    int lastMutateOp = -1;
};

} // namespace pfl::pulse_perf

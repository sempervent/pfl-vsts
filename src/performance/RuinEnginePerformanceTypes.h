#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pfl::ruin_perf
{

/** Ruin Engine performance-engine version (DSP algorithm version is separate). */
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

enum class CollapsePhase : uint8_t
{
    None = 0,
    Destabilize,
    Fracture, // aka Thin/Fracture chapter
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
        case CollapsePhase::Destabilize: return "DESTABILIZE";
        case CollapsePhase::Fracture: return "FRACTURE";
        case CollapsePhase::Devour: return "DEVOUR";
        case CollapsePhase::Residue: return "RESIDUE";
        default: return "?";
    }
}

/**
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 * COLLAPSE overrides FREEZE. MUTATE ignored while Collapsing/Silenced.
 * RESEED preserves WearState. SILENCE != MIX=0.
 */
struct RuinPerfState
{
    Mode mode = Mode::Normal;
    CollapsePhase collapsePhase = CollapsePhase::None;
    double collapseElapsedBeats = 0.0;
    double lastTickPpq = -1.0;
    bool evolutionLocked = false;
    bool silenceActive = false;
    bool freezeLatched = false;      // performer freeze under silence/collapse
    bool frozenBeforeSilence = false;
    bool collapsingBeforeSilence = false;
    uint64_t reseedCount = 0;
    uint64_t currentSeed = 2002;
    int lastMutateAxis = -1;
};

} // namespace pfl::ruin_perf

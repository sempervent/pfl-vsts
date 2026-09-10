#pragma once

#include "generative/DeterministicRNG.h"
#include "generative/EnsembleTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pfl::conductor_perf
{

/** Broken Conductor performance-engine version (generative algo stays at ConductorEngine::kAlgorithmVersion). */
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
    Thin,
    Fragment,
    Residue
};

struct PerfTraceEvent
{
    double ppq = 0.0;
    Command command = Command::FreezeOn;
    uint64_t seedAfter = 0;
    int mutateRole = -1; // VoiceRole or -1
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

/**
 * Priority: SILENCE > COLLAPSE > FREEZE > MUTATE
 * COLLAPSE overrides FREEZE. MUTATE ignored while Collapsing.
 * FREEZE pauses evolution + hunger; frozen RhythmDNA may still express onsets.
 * COLLAPSE lasts kCollapseBeats (24) across four phases of 6 beats.
 */
struct ConductorPerfState
{
    Mode mode = Mode::Normal;
    CollapsePhase collapsePhase = CollapsePhase::None;
    double collapseStartPpq = -1.0;
    double collapseEndPpq = -1.0;
    bool evolutionLocked = false;
    bool silenceActive = false;
    bool hungerPaused = false;
    bool frozenBeforeSilence = false;
    uint64_t reseedCount = 0;
    uint64_t currentSeed = 1001;
    int lastMutateRole = -1;
    int mutateCount = 0;
};

} // namespace pfl::conductor_perf

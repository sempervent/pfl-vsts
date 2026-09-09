#pragma once

#include <cstdint>

namespace pfl::performance
{

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
    Silenced,
    Reseeding
};

struct PerformanceEvent
{
    double ppq = 0.0;
    Command command = Command::FreezeOn;
    uint64_t seedAfter = 0; // for RESEED
};

/** Version of the performance-engine layer (Composer algorithm stays at 3). */
inline constexpr int kPerformanceEngineVersion = 1;

} // namespace pfl::performance

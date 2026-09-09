#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pfl::generative
{

/** Broken Conductor Stage 3 ensemble roles (one MIDI channel). */
enum class VoiceRole : uint8_t
{
    Foundation = 0,
    Pulse = 1,
    Wanderer = 2,
    Accent = 3,
    Count = 4
};

enum class InteractionReason : uint8_t
{
    Normal = 0,
    GapFill = 1,
    CallResponse = 2,
    CongestionSuppress = 3,
    CollisionShift = 4,
    CollisionSuppress = 5,
    BudgetSuppress = 6,
    RoleRest = 7
};

inline const char* voiceRoleName (VoiceRole r) noexcept
{
    switch (r)
    {
        case VoiceRole::Foundation: return "FOUNDATION";
        case VoiceRole::Pulse: return "PULSE";
        case VoiceRole::Wanderer: return "WANDERER";
        case VoiceRole::Accent: return "ACCENT";
        default: return "UNKNOWN";
    }
}

inline const char* interactionReasonName (InteractionReason r) noexcept
{
    switch (r)
    {
        case InteractionReason::Normal: return "NORMAL";
        case InteractionReason::GapFill: return "GAP_FILL";
        case InteractionReason::CallResponse: return "CALL_RESPONSE";
        case InteractionReason::CongestionSuppress: return "CONGESTION_SUPPRESS";
        case InteractionReason::CollisionShift: return "COLLISION_SHIFT";
        case InteractionReason::CollisionSuppress: return "COLLISION_SUPPRESS";
        case InteractionReason::BudgetSuppress: return "BUDGET_SUPPRESS";
        case InteractionReason::RoleRest: return "ROLE_REST";
        default: return "UNKNOWN";
    }
}

inline int voicePriority (VoiceRole r) noexcept
{
    // Higher = more important for sustain / conflict (Foundation wins).
    switch (r)
    {
        case VoiceRole::Foundation: return 4;
        case VoiceRole::Pulse: return 3;
        case VoiceRole::Wanderer: return 2;
        case VoiceRole::Accent: return 1;
        default: return 0;
    }
}

struct VoiceRegister
{
    int minMidi = 26;
    int maxMidi = 50;
    int startMidi = 38;
    int minOctave = 1;
    int maxOctave = 3;
};

inline VoiceRegister registerFor (VoiceRole r) noexcept
{
    switch (r)
    {
        case VoiceRole::Foundation: return { 26, 50, 38, 1, 3 };
        case VoiceRole::Pulse: return { 38, 62, 50, 2, 4 };
        case VoiceRole::Wanderer: return { 50, 74, 62, 3, 5 };
        case VoiceRole::Accent: return { 62, 86, 74, 4, 6 };
        default: return { 26, 50, 38, 1, 3 };
    }
}

/** Role presence in [0,1] given density — eligibility, not fire rate. */
inline float rolePresence (VoiceRole r, float density01) noexcept
{
    const float d = density01 < 0.0f ? 0.0f : (density01 > 1.0f ? 1.0f : density01);
    switch (r)
    {
        case VoiceRole::Foundation:
            return 1.0f;
        case VoiceRole::Pulse:
            return 0.15f + 0.85f * d;
        case VoiceRole::Wanderer:
            return d < 0.08f ? 0.0f : (0.0f + 0.90f * ((d - 0.08f) / 0.92f));
        case VoiceRole::Accent:
            // Presence opens windows; fire rate stays tiny.
            return d < 0.12f ? 0.0f : (0.05f + 0.35f * ((d - 0.12f) / 0.88f));
        default:
            return 0.0f;
    }
}

/** Multiplier on expression gate (before congestion/gap). Accent kept very low. */
inline float roleExpressionMult (VoiceRole r) noexcept
{
    switch (r)
    {
        case VoiceRole::Foundation: return 1.00f;
        case VoiceRole::Pulse: return 0.85f;
        case VoiceRole::Wanderer: return 0.50f;
        case VoiceRole::Accent: return 0.08f;
        default: return 0.0f;
    }
}

inline float roleMutationGain (VoiceRole r) noexcept
{
    switch (r)
    {
        case VoiceRole::Foundation: return 0.25f;
        case VoiceRole::Pulse: return 0.70f;
        case VoiceRole::Wanderer: return 1.35f;
        case VoiceRole::Accent: return 0.55f; // pitch; timing handled separately
        default: return 1.0f;
    }
}

/** Effective mutation for a role's pitch/DNA (clamped). */
inline float roleEffectiveMutation (VoiceRole r, float mutation01) noexcept
{
    const float m = mutation01 < 0.0f ? 0.0f : (mutation01 > 1.0f ? 1.0f : mutation01);
    return std::min (1.0f, m * roleMutationGain (r));
}

/**
 * Stage 4 routing/configuration — which role(s) leave this instance as MIDI.
 * Does not affect composition. Default Ensemble preserves Stage 3 behavior.
 */
enum class OutputRole : uint8_t
{
    Ensemble = 0,
    Foundation = 1,
    Pulse = 2,
    Wanderer = 3,
    Accent = 4
};

inline const char* outputRoleName (OutputRole r) noexcept
{
    switch (r)
    {
        case OutputRole::Ensemble: return "ENSEMBLE";
        case OutputRole::Foundation: return "FOUNDATION";
        case OutputRole::Pulse: return "PULSE";
        case OutputRole::Wanderer: return "WANDERER";
        case OutputRole::Accent: return "ACCENT";
        default: return "UNKNOWN";
    }
}

inline bool outputRolePasses (OutputRole filter, VoiceRole role) noexcept
{
    if (filter == OutputRole::Ensemble)
        return true;
    return static_cast<uint8_t> (filter) == static_cast<uint8_t> (role) + 1;
}

/**
 * Global onset budget per bar (approximate). dens=1 still leaves space.
 * Not exposed as a parameter.
 */
inline int onsetBudgetPerBar (float density01) noexcept
{
    const float d = density01 < 0.0f ? 0.0f : (density01 > 1.0f ? 1.0f : density01);
    // ~2 at dens0 … ~11 at dens1
    return 2 + static_cast<int> (std::lround (9.0 * static_cast<double> (d)));
}

/** Proposed note-on for arbitration (Phase 1). */
struct EventIntent
{
    VoiceRole role = VoiceRole::Foundation;
    bool active = false; // false = role chose rest / no onset this slot
    int pitch = 0;
    int velocity = 80;
    double durationBeats = 1.0;
    InteractionReason reason = InteractionReason::Normal;
};

} // namespace pfl::generative

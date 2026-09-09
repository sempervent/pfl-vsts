#pragma once

#include <cstdint>

namespace pfl::generative
{

enum class EventType : uint8_t
{
    NoteChange = 0,
    VoiceEnter,
    VoiceExit
};

struct MusicalEvent
{
    double beat = 0.0;          // absolute PPQ beat when decision applies
    double durationBeats = 0.0; // informational; drones hold until next change
    int midiNote = 38;
    float velocity = 0.8f;
    int voice = 0;
    EventType type = EventType::NoteChange;
};

} // namespace pfl::generative

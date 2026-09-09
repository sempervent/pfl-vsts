#pragma once

#include <cstdint>
#include <vector>

namespace pfl::generative
{

/** Non-real-time / test MIDI trace (never write to disk from audio callback). */
enum class MidiMsgKind : uint8_t
{
    NoteOn = 0,
    NoteOff,
    AllNotesOff
};

struct MidiTraceEvent
{
    double ppq = 0.0;
    int channel = 1; // MIDI channel 1..16
    int note = 0;
    int velocity = 0;
    int voice = 0; // Stage 3: VoiceRole index
    int reason = 0; // InteractionReason (diagnostic; 0 = NORMAL)
    MidiMsgKind kind = MidiMsgKind::NoteOn;
};

} // namespace pfl::generative

#pragma once

#include "MidiTrace.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace pfl::generative
{

/**
 * Bounded MIDI note ownership — guarantees stop/seek/reset can panic cleanly.
 * Channels 1..16, notes 0..127.
 */
class MidiNoteTracker
{
public:
    void clear() noexcept
    {
        active_.fill (false);
        count_ = 0;
    }

    bool noteOn (int channel, int note) noexcept
    {
        if (! valid (channel, note))
            return false;
        const size_t i = index (channel, note);
        if (! active_[i])
        {
            active_[i] = true;
            ++count_;
        }
        return true;
    }

    bool noteOff (int channel, int note) noexcept
    {
        if (! valid (channel, note))
            return false;
        const size_t i = index (channel, note);
        if (active_[i])
        {
            active_[i] = false;
            --count_;
            return true;
        }
        return false;
    }

    bool isActive (int channel, int note) const noexcept
    {
        if (! valid (channel, note))
            return false;
        return active_[index (channel, note)];
    }

    int activeCount() const noexcept { return count_; }

    /** Append NoteOff for every active note, then clear. */
    void panicTo (std::vector<MidiTraceEvent>& out, double ppq, int voice = 0) noexcept
    {
        for (int ch = 1; ch <= 16; ++ch)
        {
            for (int n = 0; n < 128; ++n)
            {
                if (! isActive (ch, n))
                    continue;
                MidiTraceEvent e;
                e.ppq = ppq;
                e.channel = ch;
                e.note = n;
                e.velocity = 0;
                e.voice = voice;
                e.kind = MidiMsgKind::NoteOff;
                out.push_back (e);
            }
        }
        clear();
    }

private:
    static bool valid (int channel, int note) noexcept
    {
        return channel >= 1 && channel <= 16 && note >= 0 && note < 128;
    }

    static size_t index (int channel, int note) noexcept
    {
        return static_cast<size_t> ((channel - 1) * 128 + note);
    }

    std::array<bool, 16 * 128> active_ {};
    int count_ = 0;
};

} // namespace pfl::generative

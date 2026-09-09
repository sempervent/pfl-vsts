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
 * Stage 3: each active (channel, note) tracks owning role + scheduled end.
 * Channels 1..16, notes 0..127.
 */
class MidiNoteTracker
{
public:
    struct Ownership
    {
        bool active = false;
        int role = -1; // VoiceRole as int, or -1
        double startPpq = 0.0;
        double endPpq = 0.0;
    };

    void clear() noexcept
    {
        for (auto& o : owners_)
            o = Ownership{};
        count_ = 0;
    }

    bool noteOn (int channel, int note) noexcept
    {
        return noteOn (channel, note, -1, 0.0, 0.0);
    }

    bool noteOn (int channel, int note, int role, double startPpq, double endPpq) noexcept
    {
        if (! valid (channel, note))
            return false;
        const size_t i = index (channel, note);
        if (! owners_[i].active)
        {
            owners_[i].active = true;
            ++count_;
        }
        owners_[i].role = role;
        owners_[i].startPpq = startPpq;
        owners_[i].endPpq = endPpq;
        return true;
    }

    bool noteOff (int channel, int note) noexcept
    {
        if (! valid (channel, note))
            return false;
        const size_t i = index (channel, note);
        if (owners_[i].active)
        {
            owners_[i] = Ownership{};
            --count_;
            return true;
        }
        return false;
    }

    /** NoteOff only if the named role currently owns the pitch. */
    bool noteOffIfOwner (int channel, int note, int role) noexcept
    {
        if (! valid (channel, note))
            return false;
        const size_t i = index (channel, note);
        if (! owners_[i].active || owners_[i].role != role)
            return false;
        owners_[i] = Ownership{};
        --count_;
        return true;
    }

    bool isActive (int channel, int note) const noexcept
    {
        if (! valid (channel, note))
            return false;
        return owners_[index (channel, note)].active;
    }

    int ownerRole (int channel, int note) const noexcept
    {
        if (! isActive (channel, note))
            return -1;
        return owners_[index (channel, note)].role;
    }

    Ownership ownership (int channel, int note) const noexcept
    {
        if (! valid (channel, note))
            return {};
        return owners_[index (channel, note)];
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
                e.voice = owners_[index (ch, n)].role >= 0 ? owners_[index (ch, n)].role : voice;
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

    std::array<Ownership, 16 * 128> owners_ {};
    int count_ = 0;
};

} // namespace pfl::generative

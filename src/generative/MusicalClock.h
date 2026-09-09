#pragma once

#include <algorithm>
#include <cmath>

namespace pfl::generative
{

struct ClockSnapshot
{
    bool playing = false;
    double ppq = 0.0;
    double tempoBpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
};

/**
 * Host-synced musical clock.
 * Detects bar / 4-bar phrase boundaries and timeline seeks.
 */
class MusicalClock
{
public:
    void reset (double ppq = 0.0) noexcept
    {
        lastPpq_ = ppq;
        initialized_ = false;
        seekDetected_ = false;
    }

    /** Update from host; returns true if a discontinuous seek was detected. */
    bool advance (const ClockSnapshot& snap) noexcept
    {
        seekDetected_ = false;
        tempoBpm_ = snap.tempoBpm > 1.0 ? snap.tempoBpm : 120.0;
        beatsPerBar_ = snap.timeSigNumerator > 0 ? static_cast<double> (snap.timeSigNumerator) : 4.0;

        if (! snap.playing)
        {
            playing_ = false;
            return false;
        }

        if (! initialized_)
        {
            lastPpq_ = snap.ppq;
            initialized_ = true;
            playing_ = true;
            return false;
        }

        const double delta = snap.ppq - lastPpq_;
        // Contiguous playback: small forward progress. Seek if backward or large jump.
        if (delta < -0.001 || delta > 2.0)
        {
            seekDetected_ = true;
            lastPpq_ = snap.ppq;
            playing_ = true;
            return true;
        }

        lastPpq_ = snap.ppq;
        playing_ = true;
        return false;
    }

    double ppq() const noexcept { return lastPpq_; }
    double tempoBpm() const noexcept { return tempoBpm_; }
    double beatsPerBar() const noexcept { return beatsPerBar_; }
    bool playing() const noexcept { return playing_; }
    bool seekDetected() const noexcept { return seekDetected_; }

    int barIndex() const noexcept
    {
        return static_cast<int> (std::floor (lastPpq_ / beatsPerBar_));
    }

    int phraseIndex4() const noexcept
    {
        return barIndex() / 4;
    }

    double barStartPpq (int bar) const noexcept
    {
        return static_cast<double> (bar) * beatsPerBar_;
    }

    /** Inclusive bar crossings in (fromPpq, toPpq]. */
    template <typename Fn>
    void forEachBarCrossing (double fromPpq, double toPpq, Fn&& fn) const
    {
        if (toPpq <= fromPpq)
            return;
        const double first = std::floor (fromPpq / beatsPerBar_) + 1.0;
        for (double b = first; ; ++b)
        {
            const double at = b * beatsPerBar_;
            if (at > toPpq + 1.0e-9)
                break;
            if (at > fromPpq + 1.0e-12)
                fn (static_cast<int> (b), at);
        }
    }

private:
    double lastPpq_ = 0.0;
    double tempoBpm_ = 120.0;
    double beatsPerBar_ = 4.0;
    bool initialized_ = false;
    bool playing_ = false;
    bool seekDetected_ = false;
};

} // namespace pfl::generative

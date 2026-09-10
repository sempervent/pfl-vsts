#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pfl::dsp
{

/**
 * Fixed-capacity stereo audio history ring (Memory Eater).
 * Allocate only in prepare. No processBlock allocations.
 * Dual-mono L/R with shared write index (coherent stereo time).
 */
class AudioHistoryRing
{
public:
    void prepare (double sampleRate, int maxBlockSize, float maxHistoryBeats, float minDesignBpm) noexcept
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        const double seconds = static_cast<double> (maxHistoryBeats) * 60.0
                               / static_cast<double> (std::max (1.0f, minDesignBpm));
        const int framesRaw = std::max (1, static_cast<int> (std::ceil (sampleRate_ * seconds)));
        const int margin = std::max (64, maxBlockSize) + 8;
        capacity_ = framesRaw + margin;
        bufferL_.assign (static_cast<size_t> (capacity_), 0.0f);
        bufferR_.assign (static_cast<size_t> (capacity_), 0.0f);
        clear();
    }

    void clear() noexcept
    {
        if (! bufferL_.empty())
        {
            std::fill (bufferL_.begin(), bufferL_.end(), 0.0f);
            std::fill (bufferR_.begin(), bufferR_.end(), 0.0f);
        }
        write_ = 0;
        filled_ = 0;
    }

    int capacity() const noexcept { return capacity_; }
    int filled() const noexcept { return filled_; }
    double sampleRate() const noexcept { return sampleRate_; }

    void write (float L, float R) noexcept
    {
        if (capacity_ <= 0)
            return;
        bufferL_[static_cast<size_t> (write_)] = L;
        bufferR_[static_cast<size_t> (write_)] = R;
        write_ = (write_ + 1) % capacity_;
        if (filled_ < capacity_)
            ++filled_;
    }

    /** Absolute lookback in samples behind the write head (1 = last written). */
    void readAtLookback (float lookbackSamples, float& outL, float& outR) const noexcept
    {
        if (filled_ < 2 || capacity_ <= 0)
        {
            outL = outR = 0.0f;
            return;
        }
        const float maxLb = static_cast<float> (std::max (1, filled_ - 1));
        const float lb = std::clamp (lookbackSamples, 1.0f, maxLb);
        // Position relative to write_: write_-lb is the sample lb behind
        const float pos = static_cast<float> (write_) - lb;
        readFractional (pos, outL, outR);
    }

    /** Max safe lookback in samples (leaves write margin). */
    int maxSafeLookback() const noexcept
    {
        const int margin = 8;
        return std::max (0, filled_ - margin);
    }

private:
    void readFractional (float absolutePos, float& outL, float& outR) const noexcept
    {
        // Wrap into [0, capacity)
        float p = absolutePos;
        while (p < 0.0f)
            p += static_cast<float> (capacity_);
        while (p >= static_cast<float> (capacity_))
            p -= static_cast<float> (capacity_);

        const int i0 = static_cast<int> (p) % capacity_;
        const int i1 = (i0 + 1) % capacity_;
        const float frac = p - static_cast<float> (static_cast<int> (p));
        const float a0 = bufferL_[static_cast<size_t> (i0)];
        const float a1 = bufferL_[static_cast<size_t> (i1)];
        const float b0 = bufferR_[static_cast<size_t> (i0)];
        const float b1 = bufferR_[static_cast<size_t> (i1)];
        outL = a0 + (a1 - a0) * frac;
        outR = b0 + (b1 - b0) * frac;
    }

    double sampleRate_ = 44100.0;
    int capacity_ = 0;
    int write_ = 0;
    int filled_ = 0;
    std::vector<float> bufferL_, bufferR_;
};

} // namespace pfl::dsp

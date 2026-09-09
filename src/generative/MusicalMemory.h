#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace pfl::generative
{

/** Recent pitch history; penalizes immediate repetition without forbidding motifs. */
class MusicalMemory
{
public:
    static constexpr int kCapacity = 12;

    void clear() noexcept
    {
        size_ = 0;
        write_ = 0;
        for (auto& n : notes_)
            n = -1;
    }

    void push (int midiNote) noexcept
    {
        notes_[static_cast<size_t> (write_)] = midiNote;
        write_ = (write_ + 1) % kCapacity;
        if (size_ < kCapacity)
            ++size_;
    }

    /** Multiplier in (0,1] — lower = more penalized. */
    float penaltyMultiplier (int candidateMidi) const noexcept
    {
        if (size_ == 0)
            return 1.0f;

        float mul = 1.0f;
        for (int age = 0; age < size_; ++age)
        {
            const int idx = (write_ - 1 - age + kCapacity * 4) % kCapacity;
            if (notes_[static_cast<size_t> (idx)] != candidateMidi)
                continue;

            if (age == 0)
                mul *= 0.22f; // strongly discourage immediate repeat
            else if (age == 1)
                mul *= 0.45f;
            else if (age < 4)
                mul *= 0.70f;
            else
                mul *= 0.88f;
        }
        return std::max (0.08f, mul);
    }

    int size() const noexcept { return size_; }

private:
    std::array<int, kCapacity> notes_ {};
    int write_ = 0;
    int size_ = 0;
};

} // namespace pfl::generative

#pragma once

#include <cstdint>

namespace pfl::generative
{

/** Minimal deterministic xorshift64* RNG for audio-rate / control-rate use. */
class DeterministicRNG
{
public:
    DeterministicRNG() noexcept = default;

    explicit DeterministicRNG (uint64_t seed) noexcept { reseed (seed); }

    void reseed (uint64_t seed) noexcept
    {
        state_ = seed == 0 ? 0x9E3779B97F4A7C15ull : seed;
    }

    /** Derive an independent stream from a master seed + label hash. */
    static DeterministicRNG derived (uint64_t masterSeed, uint64_t streamTag) noexcept
    {
        // SplitMix64-style mix
        uint64_t z = masterSeed + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z = z ^ (z >> 31);
        z ^= streamTag * 0xD6E8FEB86659FD93ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        return DeterministicRNG (z ^ (z >> 27));
    }

    uint64_t nextU64() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return state_ * 0x2545F4914F6CDD1Dull;
    }

    /** Uniform in [0, 1). */
    float nextFloat() noexcept
    {
        return static_cast<float> ((nextU64() >> 40) * (1.0 / (1ull << 24)));
    }

    /** Uniform in [lo, hi). */
    float nextFloat (float lo, float hi) noexcept
    {
        return lo + (hi - lo) * nextFloat();
    }

private:
    uint64_t state_ = 0x9E3779B97F4A7C15ull;
};

} // namespace pfl::generative

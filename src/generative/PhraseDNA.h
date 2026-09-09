#pragma once

#include "DeterministicRNG.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pfl::generative
{

/** Compact evolving degree-step phrase. Values are relative degree deltas. */
struct PhraseDNA
{
    static constexpr int kMaxLen = 8;
    std::array<int, kMaxLen> steps {};
    int length = 4;
    int generation = 0;
    int cursor = 0;
    int birthBar = 0;
    int lifespanBars = 16;
    int lastMutationIndex = -1;
    int lastMutationFrom = 0;
    int lastMutationTo = 0;

    int nextStep() const noexcept
    {
        if (length <= 0)
            return 0;
        return steps[static_cast<size_t> (cursor % length)];
    }

    void advanceCursor() noexcept
    {
        if (length > 0)
            cursor = (cursor + 1) % length;
    }

    std::string describe() const
    {
        std::string s = "gen=" + std::to_string (generation) + " DNA:";
        for (int i = 0; i < length; ++i)
        {
            char buf[16];
            std::snprintf (buf, sizeof buf, "%s%d", (i == 0 ? "" : ","), steps[static_cast<size_t> (i)]);
            s += buf;
        }
        return s;
    }
};

struct PhraseTrace
{
    int bar = 0;
    int generation = 0;
    std::string kind; // "born" | "mutate"
    std::string detail;
};

/**
 * Phrase DNA manager — uses only the phrase RNG stream.
 * Influences Composer pitch motion without replacing voice identity / memory.
 */
class PhraseEngine
{
public:
    void reset (DeterministicRNG phraseRng, float mutation01, int currentBar = 0) noexcept
    {
        rng_ = phraseRng;
        mutation_ = std::clamp (mutation01, 0.0f, 1.0f);
        traces_.clear();
        dna_ = generateNew (currentBar);
        record (currentBar, "born", dna_.describe());
    }

    void setTraceEnabled (bool enabled) noexcept { traceEnabled_ = enabled; }

    void setMutation (float mutation01) noexcept
    {
        mutation_ = std::clamp (mutation01, 0.0f, 1.0f);
    }

    const PhraseDNA& dna() const noexcept { return dna_; }

    const std::vector<PhraseTrace>& traces() const noexcept { return traces_; }

    void clearTraces() noexcept { traces_.clear(); }

    /** Probability of following phrase vs free walk when a pitch change occurs. */
    float followBias() const noexcept
    {
        // High mutation → slightly more free walk; low mutation → cling to DNA
        return std::clamp (0.82f - 0.35f * mutation_, 0.45f, 0.88f);
    }

    int consumePhraseStep() noexcept
    {
        const int step = dna_.nextStep();
        dna_.advanceCursor();
        return step;
    }

    /** Call once per bar; may mutate one DNA element when lifespan expires. */
    void onBar (int barIndex) noexcept
    {
        if (barIndex < dna_.birthBar + dna_.lifespanBars)
            return;
        mutateOne (barIndex);
    }

    /**
     * One bounded DNA mutation using an external RNG (manualMutation stream).
     * Does not consume the autonomous phrase RNG.
     */
    void manualMutateOne (DeterministicRNG& rng, int barIndex) noexcept
    {
        if (dna_.length <= 0)
            return;

        const int idx = static_cast<int> (rng.nextFloat() * static_cast<float> (dna_.length)) % dna_.length;
        const int old = dna_.steps[static_cast<size_t> (idx)];
        int neu = old;
        const float r = rng.nextFloat();
        if (r < 0.5f)
            neu = old + (rng.nextFloat() < 0.5f ? -1 : 1);
        else if (r < 0.75f)
            neu = -old;
        else
            neu = 0;
        neu = std::clamp (neu, -2, 2);
        dna_.steps[static_cast<size_t> (idx)] = neu;
        dna_.lastMutationIndex = idx;
        dna_.lastMutationFrom = old;
        dna_.lastMutationTo = neu;
        ++dna_.generation;
        dna_.birthBar = barIndex;
        // Keep lifespan; do not re-roll with phrase RNG

        char detail[128];
        std::snprintf (detail, sizeof detail, "%s | manual mut idx %d: %d -> %d",
                       dna_.describe().c_str(), idx, old, neu);
        record (barIndex, "mutate", detail);
    }

private:
    PhraseDNA generateNew (int bar) noexcept
    {
        PhraseDNA p;
        p.generation = 0;
        p.cursor = 0;
        p.birthBar = bar;
        p.length = 3 + static_cast<int> (rng_.nextFloat() * 5.0f); // 3..7
        p.length = std::clamp (p.length, 3, PhraseDNA::kMaxLen);

        // Prefer small local motions; occasional stay (0)
        static constexpr int choices[] = { 0, 0, 1, -1, 1, -1, 2, -2 };
        for (int i = 0; i < p.length; ++i)
        {
            const int idx = static_cast<int> (rng_.nextFloat() * 8.0f) % 8;
            p.steps[static_cast<size_t> (i)] = choices[idx];
        }
        p.lifespanBars = lifespanForMutation();
        return p;
    }

    int lifespanForMutation() noexcept
    {
        // 8–32 bars; higher mutation → shorter ancestry
        const float t = 1.0f - mutation_;
        const int lo = 8 + static_cast<int> (8.0f * t);
        const int hi = 16 + static_cast<int> (16.0f * t);
        const int span = std::max (1, hi - lo + 1);
        return lo + static_cast<int> (rng_.nextFloat() * static_cast<float> (span));
    }

    void mutateOne (int bar) noexcept
    {
        if (dna_.length <= 0)
        {
            dna_ = generateNew (bar);
            record (bar, "born", dna_.describe());
            return;
        }

        const int idx = static_cast<int> (rng_.nextFloat() * static_cast<float> (dna_.length)) % dna_.length;
        const int old = dna_.steps[static_cast<size_t> (idx)];
        int neu = old;

        // One conceptual mutation type
        const float r = rng_.nextFloat();
        if (r < 0.35f)
        {
            // nudge degree by ±1
            neu = old + (rng_.nextFloat() < 0.5f ? -1 : 1);
        }
        else if (r < 0.55f)
        {
            // invert one motion
            neu = -old;
        }
        else if (r < 0.70f)
        {
            // set to stay
            neu = 0;
        }
        else if (r < 0.85f)
        {
            // set to ±2
            neu = (rng_.nextFloat() < 0.5f ? -2 : 2);
        }
        else
        {
            // duplicate neighbor into this slot
            const int nbr = (idx + 1) % dna_.length;
            neu = dna_.steps[static_cast<size_t> (nbr)];
        }

        neu = std::clamp (neu, -2, 2);
        dna_.steps[static_cast<size_t> (idx)] = neu;
        dna_.lastMutationIndex = idx;
        dna_.lastMutationFrom = old;
        dna_.lastMutationTo = neu;
        ++dna_.generation;
        dna_.birthBar = bar;
        dna_.lifespanBars = lifespanForMutation();
        // Keep cursor continuity — ancestry remains audible

        char detail[128];
        std::snprintf (detail, sizeof detail, "%s | mut idx %d: %d -> %d",
                       dna_.describe().c_str(), idx, old, neu);
        record (bar, "mutate", detail);
    }

    void record (int bar, const char* kind, const std::string& detail) noexcept
    {
        if (! traceEnabled_)
            return;
        PhraseTrace t;
        t.bar = bar;
        t.generation = dna_.generation;
        t.kind = kind;
        t.detail = detail;
        traces_.push_back (std::move (t));
    }

    DeterministicRNG rng_;
    float mutation_ = 0.35f;
    PhraseDNA dna_{};
    std::vector<PhraseTrace> traces_;
    bool traceEnabled_ = false;
};

} // namespace pfl::generative

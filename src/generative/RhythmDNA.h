#pragma once

#include "DeterministicRNG.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pfl::generative
{

/** Sixteenth-grid cell: onset, sustain continuation, or rest. */
enum class RhythmCell : uint8_t
{
    Rest = 0,  // .
    Onset = 1, // X
    Hold = 2   // _
};

/**
 * Broken Conductor Stage 2 — rhythmic Phrase DNA.
 * Fixed 16th-note cells spanning 1–4 bars (default 2). Playback walks cells;
 * generation/mutation use the dedicated rhythm RNG only.
 */
struct RhythmDNA
{
    static constexpr int kMaxBars = 4;
    static constexpr int kStepsPerBar = 16; // sixteenth notes in 4/4
    static constexpr int kMaxCells = kMaxBars * kStepsPerBar;

    std::array<RhythmCell, kMaxCells> cells {};
    int lengthBars = 2;
    int generation = 0;
    int birthBar = 0;
    int lifespanBars = 16;
    int lastMutationIndex = -1;
    RhythmCell lastMutationFrom = RhythmCell::Rest;
    RhythmCell lastMutationTo = RhythmCell::Rest;

    int lengthCells() const noexcept
    {
        return std::clamp (lengthBars, 1, kMaxBars) * kStepsPerBar;
    }

    RhythmCell cellAt (int index) const noexcept
    {
        const int n = lengthCells();
        if (n <= 0)
            return RhythmCell::Rest;
        int i = index % n;
        if (i < 0)
            i += n;
        return cells[static_cast<size_t> (i)];
    }

    /** Absolute musical slot index → cell in cycling phrase. */
    RhythmCell cellForAbsoluteSlot (std::int64_t absoluteSlot) const noexcept
    {
        const int n = lengthCells();
        if (n <= 0)
            return RhythmCell::Rest;
        auto i = absoluteSlot % n;
        if (i < 0)
            i += n;
        return cells[static_cast<size_t> (i)];
    }

    std::string describe() const
    {
        std::string s = "gen=" + std::to_string (generation) + " bars=" + std::to_string (lengthBars) + " DNA:";
        const int n = lengthCells();
        for (int i = 0; i < n; ++i)
        {
            const auto c = cells[static_cast<size_t> (i)];
            s += (c == RhythmCell::Onset ? 'X' : (c == RhythmCell::Hold ? '_' : '.'));
        }
        return s;
    }

    float occupancy() const noexcept
    {
        const int n = lengthCells();
        if (n <= 0)
            return 0.0f;
        int filled = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto c = cells[static_cast<size_t> (i)];
            if (c == RhythmCell::Onset || c == RhythmCell::Hold)
                ++filled;
        }
        return static_cast<float> (filled) / static_cast<float> (n);
    }
};

struct RhythmTrace
{
    int bar = 0;
    int generation = 0;
    std::string kind; // "born" | "mutate"
    std::string detail;
};

/** Stage 3 role bias for RhythmDNA birth (Foundation keeps Stage 2 defaults). */
enum class RhythmVoiceKind : uint8_t
{
    Foundation = 0,
    Pulse = 1,
    Wanderer = 2,
    Accent = 3
};

/**
 * Rhythm DNA manager — uses only the rhythm RNG stream.
 * Does not touch pitch Phrase DNA.
 */
class RhythmEngine
{
public:
    static constexpr double kSlotBeats = 0.25; // sixteenth
    static constexpr float kMaxOccupancy = 0.58f;

    void setVoiceKind (RhythmVoiceKind kind) noexcept { voiceKind_ = kind; }
    RhythmVoiceKind voiceKind() const noexcept { return voiceKind_; }

    void reset (DeterministicRNG rhythmRng, float mutation01, float density01, int currentBar = 0) noexcept
    {
        rng_ = rhythmRng;
        mutation_ = std::clamp (mutation01, 0.0f, 1.0f);
        density_ = std::clamp (density01, 0.0f, 1.0f);
        traces_.clear();
        dna_ = generateNew (currentBar);
        record (currentBar, "born", dna_.describe());
    }

    void setTraceEnabled (bool enabled) noexcept { traceEnabled_ = enabled; }

    void setMutation (float mutation01) noexcept
    {
        mutation_ = std::clamp (mutation01, 0.0f, 1.0f);
    }

    void setDensity (float density01) noexcept
    {
        density_ = std::clamp (density01, 0.0f, 1.0f);
    }

    /**
     * Live control response: shorten remaining DNA lifespan so a meaningful
     * DENSITY/MUTATION increase can adapt structure by the next bar (density)
     * or within ~1–4 bars (mutation), without full state wipe.
     * Large density jumps (≥0.25) rebuild DNA immediately — occupancy is
     * birth-baked, so a single-cell mutate cannot open the control range.
     */
    void respondToLiveParams (int currentBar, float previousDensity, float previousMutation) noexcept
    {
        const float d = density_;
        const float m = mutation_;

        if (std::abs (d - previousDensity) >= 0.25f)
        {
            dna_ = generateNew (currentBar);
            record (currentBar, "reborn-density", dna_.describe());
            return;
        }

        int maxRemain = 8;
        if (d > previousDensity + 0.08f)
            maxRemain = std::min (maxRemain, 1);
        if (m > previousMutation + 0.08f)
        {
            const int mutCap = std::max (1, static_cast<int> (std::lround (1.0 + 3.0 * (1.0 - static_cast<double> (m)))));
            maxRemain = std::min (maxRemain, mutCap);
        }
        if (d < previousDensity - 0.08f)
            maxRemain = std::min (maxRemain, 2);

        const int expireAt = dna_.birthBar + dna_.lifespanBars;
        const int cappedExpire = currentBar + std::max (1, maxRemain);
        if (cappedExpire < expireAt)
            dna_.lifespanBars = std::max (1, cappedExpire - dna_.birthBar);
    }

    /**
     * Fraction of DNA onsets to express during playback (Stage 2B).
     * dens=0 → very sparse; dens=1 → express all DNA onsets.
     * Pure function of density — no RNG stream advance.
     */
    static float expressionRate (float density01) noexcept
    {
        const float d = std::clamp (density01, 0.0f, 1.0f);
        // 0.0→0.12, 0.25→0.32, 0.5→0.58, 0.75→0.82, 1.0→1.0
        return std::clamp (0.12f + 0.88f * d, 0.12f, 1.0f);
    }

    /** Deterministic onset gate for live density without consuming rhythm RNG. */
    static bool shouldExpressOnset (uint64_t masterSeed, std::int64_t absoluteSlot, float density01) noexcept
    {
        return shouldExpressOnset (masterSeed, absoluteSlot, density01, 0, 1.0f);
    }

    /**
     * Role-aware expression gate. `roleTag` isolates streams; `roleRate` scales
     * the density expression curve (Accent ≪ Foundation). Does not advance RNG.
     */
    static bool shouldExpressOnset (uint64_t masterSeed,
                                    std::int64_t absoluteSlot,
                                    float density01,
                                    uint64_t roleTag,
                                    float roleRate) noexcept
    {
        const float rate = std::clamp (expressionRate (density01) * std::clamp (roleRate, 0.0f, 1.5f),
                                       0.0f, 1.0f);
        if (rate <= 1.0e-6f)
            return false;
        if (rate >= 0.999f)
            return true;
        uint64_t z = masterSeed ^ (static_cast<uint64_t> (absoluteSlot) * 0x9E3779B97F4A7C15ull);
        z ^= 0xD6E8FEB86659FD93ull; // "express" mix
        z ^= roleTag * 0xC2B2AE3D27D4EB4Full;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        const float u = static_cast<float> ((z >> 40) * (1.0 / (1ull << 24)));
        return u < rate;
    }

    const RhythmDNA& dna() const noexcept { return dna_; }
    const std::vector<RhythmTrace>& traces() const noexcept { return traces_; }
    void clearTraces() noexcept { traces_.clear(); }

    /** Call once per bar; may apply one bounded mutation when lifespan expires. */
    void onBar (int barIndex) noexcept
    {
        if (barIndex < dna_.birthBar + dna_.lifespanBars)
            return;
        mutateOne (barIndex);
    }

    /**
     * One bounded RhythmDNA mutation using an external RNG (manual MUTATE).
     * Does not advance the autonomous rhythm stream.
     */
    void manualMutateOne (DeterministicRNG& rng, int barIndex) noexcept
    {
        const int n = dna_.lengthCells();
        if (n <= 0)
            return;
        const int idx = static_cast<int> (rng.nextFloat() * static_cast<float> (n)) % n;
        const RhythmCell old = dna_.cells[static_cast<size_t> (idx)];
        RhythmCell neu = old;
        if (old == RhythmCell::Rest)
            neu = RhythmCell::Onset;
        else if (old == RhythmCell::Onset)
            neu = (rng.nextFloat() < 0.5f) ? RhythmCell::Rest : RhythmCell::Hold;
        else
            neu = RhythmCell::Rest;

        const float maxOcc = maxOccupancyForDensity();
        applyCellMutation (idx, neu, maxOcc);
        dna_.lastMutationIndex = idx;
        dna_.lastMutationFrom = old;
        dna_.lastMutationTo = dna_.cells[static_cast<size_t> (idx)];
        ++dna_.generation;
        dna_.birthBar = barIndex;
        record (barIndex, "mutate", "manual " + dna_.describe());
    }

    /** Structural velocity bias for an absolute slot (−12..+12 before jitter). */
    static int accentBiasForSlot (std::int64_t absoluteSlot, bool afterRestEntrance) noexcept
    {
        const int slotInBar = static_cast<int> (absoluteSlot % RhythmDNA::kStepsPerBar);
        int bias = 0;
        if (slotInBar == 0)
            bias = 12; // bar downbeat
        else if (slotInBar % 4 == 0)
            bias = 6; // other quarters
        else if (slotInBar % 2 == 0)
            bias = 0; // eighth offbeat
        else
            bias = -8; // odd sixteenth

        if (afterRestEntrance)
            bias += 8;
        return bias;
    }

    /** Duration in beats for an onset starting at local cell index (within dna). */
    double durationBeatsAt (int localIndex) const noexcept
    {
        const int n = dna_.lengthCells();
        if (n <= 0)
            return 1.0;
        int i = ((localIndex % n) + n) % n;
        if (dna_.cells[static_cast<size_t> (i)] != RhythmCell::Onset)
            return 0.0;
        int slots = 1;
        for (int k = 1; k < n; ++k)
        {
            const int j = (i + k) % n;
            if (dna_.cells[static_cast<size_t> (j)] == RhythmCell::Hold)
                ++slots;
            else
                break;
        }
        return static_cast<double> (slots) * kSlotBeats;
    }

private:
    float maxOccupancyForDensity() const noexcept
    {
        // Stage 2B: wider audible span (was 0.20…0.58 with compressed top end)
        const float d = density_;
        float base = kMaxOccupancy;
        if (d < 0.20f)
            base = 0.10f;
        else if (d < 0.40f)
            base = 0.22f;
        else if (d < 0.60f)
            base = 0.36f;
        else if (d < 0.80f)
            base = 0.48f;
        else
            base = kMaxOccupancy; // 0.58

        // Stage 3: role DNA occupancy share of global wallet (sum ≪ Stage 2 solo at dens=1)
        float share = 1.0f;
        switch (voiceKind_)
        {
            case RhythmVoiceKind::Foundation: share = 0.72f; break;
            case RhythmVoiceKind::Pulse: share = 0.48f; break;
            case RhythmVoiceKind::Wanderer: share = 0.40f; break;
            case RhythmVoiceKind::Accent: share = 0.14f; break;
        }
        return std::max (0.04f, base * share);
    }

    int chooseLengthBars() noexcept
    {
        const float r = rng_.nextFloat();
        if (r < 0.15f)
            return 1;
        if (r < 0.65f)
            return 2;
        if (r < 0.90f)
            return 3;
        return 4;
    }

    double chooseDurationBeats() noexcept
    {
        // Vocab: 4, 2, 1, 0.5, 0.25 — role-biased (Foundation long; Accent short)
        const float d = density_;
        const float m = mutation_;
        float w4 = 0.18f - 0.10f * (d - 0.45f) - 0.04f * (m - 0.35f);
        float w2 = 0.28f - 0.06f * (d - 0.45f) - 0.03f * (m - 0.35f);
        float w1 = 0.32f + 0.02f * (d - 0.45f);
        float w05 = 0.15f + 0.08f * (d - 0.45f) + 0.04f * (m - 0.35f);
        float w025 = 0.07f + 0.06f * (d - 0.45f) + 0.03f * (m - 0.35f);

        switch (voiceKind_)
        {
            case RhythmVoiceKind::Foundation:
                break;
            case RhythmVoiceKind::Pulse:
                w4 = 0.02f;
                w2 = 0.12f;
                w1 = 0.38f;
                w05 = 0.35f;
                w025 = 0.13f;
                break;
            case RhythmVoiceKind::Wanderer:
                w4 = 0.04f;
                w2 = 0.22f;
                w1 = 0.36f;
                w05 = 0.28f;
                w025 = 0.10f;
                break;
            case RhythmVoiceKind::Accent:
                w4 = 0.0f;
                w2 = 0.02f;
                w1 = 0.08f;
                w05 = 0.40f;
                w025 = 0.50f;
                break;
        }

        w4 = std::clamp (w4, 0.0f, 0.35f);
        w2 = std::clamp (w2, 0.0f, 0.40f);
        w1 = std::clamp (w1, 0.05f, 0.50f);
        w05 = std::clamp (w05, 0.05f, 0.45f);
        w025 = std::clamp (w025, 0.02f, 0.55f);
        if (voiceKind_ == RhythmVoiceKind::Foundation && d < 0.50f)
            w025 = std::min (w025, 0.06f);
        const float sum = w4 + w2 + w1 + w05 + w025;
        float r = rng_.nextFloat() * sum;
        if (w4 > 0.0f && (r -= w4) < 0.0f)
            return 4.0;
        if (w2 > 0.0f && (r -= w2) < 0.0f)
            return 2.0;
        if ((r -= w1) < 0.0f)
            return 1.0;
        if ((r -= w05) < 0.0f)
            return 0.5;
        return 0.25;
    }

    bool allowOddSixteenth() const noexcept
    {
        return density_ >= 0.40f || mutation_ >= 0.45f;
    }

    int chooseOnsetSlot (int phraseSlots, int preferStart) noexcept
    {
        // Prefer quarter and eighth phases; odd 16ths rare (Pulse favors offbeats)
        const float r = rng_.nextFloat();
        int phase = 0;
        if (voiceKind_ == RhythmVoiceKind::Pulse)
        {
            if (r < 0.18f)
                phase = 0;
            else if (r < 0.78f)
                phase = 2; // eighth offbeat
            else if (r < 0.92f || ! allowOddSixteenth())
                phase = 2;
            else
                phase = (rng_.nextFloat() < 0.5f ? 1 : 3);
        }
        else if (voiceKind_ == RhythmVoiceKind::Accent)
        {
            // Prefer late in bar / after rests
            if (r < 0.25f)
                phase = 0;
            else if (r < 0.70f)
                phase = 2;
            else
                phase = (allowOddSixteenth() ? (rng_.nextFloat() < 0.5f ? 1 : 3) : 2);
        }
        else if (r < 0.45f)
            phase = 0; // downbeat of beat
        else if (r < 0.80f)
            phase = 2; // eighth offbeat
        else if (r < 0.92f || ! allowOddSixteenth())
            phase = (rng_.nextFloat() < 0.5f ? 0 : 2);
        else
            phase = (rng_.nextFloat() < 0.5f ? 1 : 3); // odd 16th within beat

        const int beat = preferStart / 4;
        int slot = beat * 4 + phase;
        if (slot < 0)
            slot = 0;
        if (slot >= phraseSlots)
            slot = phraseSlots - 1;
        // Jitter within phrase by whole beats sometimes
        if (rng_.nextFloat() < 0.35f)
        {
            const int beatShift = static_cast<int> (rng_.nextFloat() * static_cast<float> (std::max (1, phraseSlots / 4)));
            slot = (slot + beatShift * 4) % phraseSlots;
            slot = (slot / 4) * 4 + phase;
            if (slot >= phraseSlots)
                slot = phraseSlots - 1 - ((phraseSlots - 1 - phase) % 4);
        }
        return std::clamp (slot, 0, phraseSlots - 1);
    }

    void clearCells (RhythmDNA& p) noexcept
    {
        for (auto& c : p.cells)
            c = RhythmCell::Rest;
    }

    int countFilled (const RhythmDNA& p) const noexcept
    {
        const int n = p.lengthCells();
        int filled = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto c = p.cells[static_cast<size_t> (i)];
            if (c == RhythmCell::Onset || c == RhythmCell::Hold)
                ++filled;
        }
        return filled;
    }

    bool placeNote (RhythmDNA& p, int start, int durSlots, float maxOcc) noexcept
    {
        const int n = p.lengthCells();
        if (start < 0 || start >= n || durSlots <= 0)
            return false;
        if (p.cells[static_cast<size_t> (start)] != RhythmCell::Rest)
            return false;
        for (int k = 1; k < durSlots; ++k)
        {
            const int j = start + k;
            if (j >= n)
                return false;
            if (p.cells[static_cast<size_t> (j)] != RhythmCell::Rest)
                return false;
        }
        const int would = countFilled (p) + durSlots;
        if (static_cast<float> (would) / static_cast<float> (n) > maxOcc + 1.0e-6f)
            return false;

        p.cells[static_cast<size_t> (start)] = RhythmCell::Onset;
        for (int k = 1; k < durSlots; ++k)
            p.cells[static_cast<size_t> (start + k)] = RhythmCell::Hold;
        return true;
    }

    RhythmDNA generateNew (int bar) noexcept
    {
        RhythmDNA p;
        p.generation = 0;
        p.birthBar = bar;
        p.lengthBars = chooseLengthBars();
        p.lifespanBars = lifespanForMutation();
        clearCells (p);

        const int n = p.lengthCells();
        const float maxOcc = maxOccupancyForDensity();
        const int targetFilled = std::max (1, static_cast<int> (std::floor (maxOcc * static_cast<float> (n) * 0.90f)));

        int attempts = 0;
        int cursorHint = 0;
        while (countFilled (p) < targetFilled && attempts < 64)
        {
            ++attempts;
            const double durBeats = chooseDurationBeats();
            int durSlots = std::max (1, static_cast<int> (std::lround (durBeats / kSlotBeats)));
            durSlots = std::min (durSlots, n);
            const int start = chooseOnsetSlot (n, cursorHint);
            if (placeNote (p, start, durSlots, maxOcc))
                cursorHint = (start + durSlots) % n;
            else
            {
                for (int d = durSlots - 1; d >= 1; --d)
                {
                    if (placeNote (p, start, d, maxOcc))
                    {
                        cursorHint = (start + d) % n;
                        break;
                    }
                }
            }
        }

        // Stillness: carve a contiguous rest island at low density
        if (density_ < 0.40f && n >= 8)
        {
            const int restRun = std::clamp (4 + static_cast<int> ((0.40f - density_) * 20.0f), 4, n / 2);
            const int start = static_cast<int> (rng_.nextFloat() * static_cast<float> (std::max (1, n - restRun)));
            for (int i = 0; i < restRun && start + i < n; ++i)
                p.cells[static_cast<size_t> (start + i)] = RhythmCell::Rest;
        }

        // Guarantee at least one onset and one rest (stillness)
        if (countFilled (p) == 0)
            placeNote (p, 0, std::min (4, n), maxOcc);
        if (countFilled (p) >= n)
        {
            // Occupancy hard cap — force a rest island
            const int cut = std::max (1, n / 4);
            for (int i = n - cut; i < n; ++i)
                p.cells[static_cast<size_t> (i)] = RhythmCell::Rest;
            // Fix illegal holds after cut
            sanitize (p);
        }

        sanitize (p);
        return p;
    }

    void sanitize (RhythmDNA& p) noexcept
    {
        const int n = p.lengthCells();
        bool inNote = false;
        for (int i = 0; i < n; ++i)
        {
            auto& c = p.cells[static_cast<size_t> (i)];
            if (c == RhythmCell::Onset)
                inNote = true;
            else if (c == RhythmCell::Hold)
            {
                if (! inNote)
                    c = RhythmCell::Rest;
            }
            else
                inNote = false;
        }
        // Cap occupancy
        while (p.occupancy() > kMaxOccupancy + 1.0e-6f)
        {
            // Remove last onset run
            for (int i = n - 1; i >= 0; --i)
            {
                if (p.cells[static_cast<size_t> (i)] == RhythmCell::Onset)
                {
                    p.cells[static_cast<size_t> (i)] = RhythmCell::Rest;
                    for (int j = i + 1; j < n && p.cells[static_cast<size_t> (j)] == RhythmCell::Hold; ++j)
                        p.cells[static_cast<size_t> (j)] = RhythmCell::Rest;
                    break;
                }
            }
            sanitize (p);
            break;
        }
    }

    int lifespanForMutation() noexcept
    {
        // Stage 2B: mut=0 → 12–24; mut=1 → 2–6 (was 8–16 at mut=1)
        const float t = 1.0f - mutation_;
        const int lo = 2 + static_cast<int> (10.0f * t);
        const int hi = 6 + static_cast<int> (18.0f * t);
        const int span = std::max (1, hi - lo + 1);
        return lo + static_cast<int> (rng_.nextFloat() * static_cast<float> (span));
    }

    void mutateOne (int bar) noexcept
    {
        const int n = dna_.lengthCells();
        if (n <= 0)
        {
            dna_ = generateNew (bar);
            record (bar, "born", dna_.describe());
            return;
        }

        const float r = rng_.nextFloat();
        int idx = static_cast<int> (rng_.nextFloat() * static_cast<float> (n)) % n;
        const RhythmCell old = dna_.cells[static_cast<size_t> (idx)];
        RhythmCell neu = old;
        const float maxOcc = maxOccupancyForDensity();

        if (r < 0.30f)
        {
            // Duration nudge: find onset containing/near idx
            int onset = idx;
            while (onset > 0 && dna_.cells[static_cast<size_t> (onset)] == RhythmCell::Hold)
                --onset;
            if (dna_.cells[static_cast<size_t> (onset)] != RhythmCell::Onset)
            {
                neu = (old == RhythmCell::Rest) ? RhythmCell::Onset : RhythmCell::Rest;
                applyCellMutation (idx, neu, maxOcc);
            }
            else
            {
                int end = onset + 1;
                while (end < n && dna_.cells[static_cast<size_t> (end)] == RhythmCell::Hold)
                    ++end;
                const int oldDur = end - onset;
                int dur = oldDur;
                if (rng_.nextFloat() < 0.5f && dur > 1)
                    --dur;
                else if (end < n && dna_.cells[static_cast<size_t> (end)] == RhythmCell::Rest)
                    ++dur;

                std::array<RhythmCell, RhythmDNA::kMaxCells> backup = dna_.cells;
                for (int i = onset; i < end; ++i)
                    dna_.cells[static_cast<size_t> (i)] = RhythmCell::Rest;
                if (! placeNote (dna_, onset, dur, maxOcc))
                {
                    dna_.cells = backup; // rollback — mutation must not erase ancestry
                    placeNote (dna_, onset, oldDur, maxOcc);
                }
                idx = onset;
                neu = dna_.cells[static_cast<size_t> (idx)];
            }
        }
        else if (r < 0.55f)
        {
            // Rest ↔ onset flip (single cell; holds cleaned by sanitize)
            if (old == RhythmCell::Rest)
                neu = RhythmCell::Onset;
            else if (old == RhythmCell::Onset)
                neu = RhythmCell::Rest;
            else
                neu = RhythmCell::Rest;
            applyCellMutation (idx, neu, maxOcc);
        }
        else if (r < 0.80f)
        {
            // Phase shift: move one onset ±1 sixteenth
            int onset = -1;
            for (int i = 0; i < n; ++i)
            {
                if (dna_.cells[static_cast<size_t> (i)] == RhythmCell::Onset)
                {
                    onset = i;
                    if (rng_.nextFloat() < 0.35f)
                        break;
                }
            }
            if (onset >= 0)
            {
                int end = onset + 1;
                while (end < n && dna_.cells[static_cast<size_t> (end)] == RhythmCell::Hold)
                    ++end;
                const int dur = end - onset;
                const int delta = (rng_.nextFloat() < 0.5f ? -1 : 1);
                const int neuStart = onset + delta;
                std::array<RhythmCell, RhythmDNA::kMaxCells> backup = dna_.cells;
                for (int i = onset; i < end; ++i)
                    dna_.cells[static_cast<size_t> (i)] = RhythmCell::Rest;
                bool ok = false;
                if (neuStart >= 0 && neuStart + dur <= n)
                    ok = placeNote (dna_, neuStart, dur, maxOcc);
                if (! ok)
                {
                    dna_.cells = backup;
                    placeNote (dna_, onset, dur, maxOcc);
                    idx = onset;
                }
                else
                    idx = neuStart;
                neu = dna_.cells[static_cast<size_t> (std::clamp (idx, 0, n - 1))];
            }
        }
        else
        {
            // Copy neighbor cell into this index
            const int nbr = (idx + 1) % n;
            neu = dna_.cells[static_cast<size_t> (nbr)];
            applyCellMutation (idx, neu, maxOcc);
        }

        sanitize (dna_);
        dna_.lastMutationIndex = idx;
        dna_.lastMutationFrom = old;
        dna_.lastMutationTo = dna_.cells[static_cast<size_t> (std::clamp (idx, 0, n - 1))];
        ++dna_.generation;
        dna_.birthBar = bar;
        dna_.lifespanBars = lifespanForMutation();

        char detail[192];
        std::snprintf (detail, sizeof detail, "%s | mut idx %d", dna_.describe().c_str(), idx);
        record (bar, "mutate", detail);
        (void) neu;
    }

    void applyCellMutation (int idx, RhythmCell neu, float maxOcc) noexcept
    {
        const int n = dna_.lengthCells();
        if (idx < 0 || idx >= n)
            return;
        if (neu == RhythmCell::Onset)
        {
            dna_.cells[static_cast<size_t> (idx)] = RhythmCell::Rest;
            placeNote (dna_, idx, 1, maxOcc);
        }
        else if (neu == RhythmCell::Rest)
        {
            if (dna_.cells[static_cast<size_t> (idx)] == RhythmCell::Onset)
            {
                dna_.cells[static_cast<size_t> (idx)] = RhythmCell::Rest;
                for (int j = idx + 1; j < n && dna_.cells[static_cast<size_t> (j)] == RhythmCell::Hold; ++j)
                    dna_.cells[static_cast<size_t> (j)] = RhythmCell::Rest;
            }
            else
                dna_.cells[static_cast<size_t> (idx)] = RhythmCell::Rest;
        }
        else
        {
            // Hold only legal after onset/hold
            if (idx > 0)
            {
                const auto prev = dna_.cells[static_cast<size_t> (idx - 1)];
                if (prev == RhythmCell::Onset || prev == RhythmCell::Hold)
                    dna_.cells[static_cast<size_t> (idx)] = RhythmCell::Hold;
            }
        }
    }

    void record (int bar, const char* kind, const std::string& detail) noexcept
    {
        if (! traceEnabled_)
            return;
        RhythmTrace t;
        t.bar = bar;
        t.generation = dna_.generation;
        t.kind = kind;
        t.detail = detail;
        traces_.push_back (std::move (t));
    }

    DeterministicRNG rng_;
    float mutation_ = 0.35f;
    float density_ = 0.45f;
    RhythmVoiceKind voiceKind_ = RhythmVoiceKind::Foundation;
    RhythmDNA dna_{};
    std::vector<RhythmTrace> traces_;
    bool traceEnabled_ = false;
};

} // namespace pfl::generative

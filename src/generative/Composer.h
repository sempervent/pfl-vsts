#pragma once

#include "DeterministicRNG.h"
#include "MusicalClock.h"
#include "MusicalEvent.h"
#include "MusicalMemory.h"
#include "PhraseDNA.h"
#include "RandomWalk.h"
#include "Scale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pfl::generative
{

struct ComposerParams
{
    float density = 0.45f;
    float mutation = 0.35f;
};

struct VoiceIdentity
{
    int homeOctave = 2;
    int minOctave = 1;
    int maxOctave = 3;
    int evalPeriodBarsMin = 4;
    int evalPeriodBarsMax = 8;
    float changeBias = 0.35f;   // probability to attempt walk when evaluating
    float chromaticBoost = 0.6f;
    int preferredDegree = 0;    // starting degree
};

struct VoiceRuntime
{
    bool active = false;
    PitchState pitch {};
    int nextEvalBar = 0;
};

/**
 * Host-time Composer — no DSP knowledge.
 * Advance by PPQ range; decisions only on bar crossings.
 */
class Composer
{
public:
    static constexpr int kMaxVoices = 3;
    /** Phase 2 = 2; Phrase DNA = 3. Seed output is versioned by this. */
    static constexpr int kAlgorithmVersion = 3;

    Composer() { setupIdentities(); }

    void setEventCapture (bool enabled) noexcept
    {
        captureEvents_ = enabled;
        phrases_.setTraceEnabled (enabled);
    }

    const std::vector<MusicalEvent>& capturedEvents() const noexcept { return captured_; }

    void clearCaptured() noexcept { captured_.clear(); }

    const PhraseEngine& phrases() const noexcept { return phrases_; }
    PhraseEngine& phrases() noexcept { return phrases_; }

    void reseed (uint64_t masterSeed) noexcept
    {
        masterSeed_ = masterSeed;
        armedSeed_ = masterSeed;
        pendingReseed_ = false;
        pitchRng_ = DeterministicRNG::derived (masterSeed, hashTag ("pitch"));
        voiceRng_ = DeterministicRNG::derived (masterSeed, hashTag ("voice"));
        structureRng_ = DeterministicRNG::derived (masterSeed, hashTag ("structure"));
        rhythmRng_ = DeterministicRNG::derived (masterSeed, hashTag ("rhythm"));
        timbreRng_ = DeterministicRNG::derived (masterSeed, hashTag ("timbre"));
        auto phraseRng = DeterministicRNG::derived (masterSeed, hashTag ("phrase"));
        memory_.clear();
        captured_.clear();
        lastProcessedPpq_ = 0.0;
        clock_.reset (0.0);
        phrases_.reset (phraseRng, params_.mutation, 0);
        initVoicesAtOrigin (0.0, 0);
    }

    /** Apply pending/new seed at a bar without rewinding the timeline. */
    void applySeedAtBar (uint64_t newSeed, int barIndex, double barPpq) noexcept
    {
        masterSeed_ = newSeed;
        armedSeed_ = newSeed;
        pendingReseed_ = false;
        pitchRng_ = DeterministicRNG::derived (newSeed, hashTag ("pitch"));
        voiceRng_ = DeterministicRNG::derived (newSeed, hashTag ("voice"));
        structureRng_ = DeterministicRNG::derived (newSeed, hashTag ("structure"));
        rhythmRng_ = DeterministicRNG::derived (newSeed, hashTag ("rhythm"));
        timbreRng_ = DeterministicRNG::derived (newSeed, hashTag ("timbre"));
        auto phraseRng = DeterministicRNG::derived (newSeed, hashTag ("phrase"));
        memory_.clear();
        phrases_.reset (phraseRng, params_.mutation, barIndex);
        initVoicesAtOrigin (barPpq, barIndex);
    }

    /** Queue seed change; applied at next bar boundary. */
    void requestReseed (uint64_t newSeed) noexcept
    {
        if (newSeed == masterSeed_ && ! pendingReseed_)
            return;
        armedSeed_ = newSeed;
        pendingReseed_ = true;
    }

    uint64_t masterSeed() const noexcept { return masterSeed_; }

    void setParams (const ComposerParams& p) noexcept
    {
        params_ = p;
        phrases_.setMutation (p.mutation);
    }

    ComposerParams params() const noexcept { return params_; }

    /** Consume one timbre draw (for isolation tests). */
    float consumeTimbreRandom() noexcept { return timbreRng_.nextFloat(); }

    const VoiceRuntime& voice (int i) const noexcept { return voices_[static_cast<size_t> (i)]; }

    int targetActiveCount() const noexcept
    {
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        // 0% → 1, 50% → 2, 100% → 3
        return 1 + static_cast<int> (std::round (d * static_cast<float> (kMaxVoices - 1)));
    }

    /**
     * Process contiguous PPQ interval (fromPpq, toPpq].
     * Call once per audio buffer using host PPQ at buffer start/end.
     */
    void processTimeRange (double fromPpq, double toPpq, bool playing) noexcept
    {
        if (! playing)
        {
            lastProcessedPpq_ = toPpq;
            return;
        }

        if (toPpq <= fromPpq)
        {
            lastProcessedPpq_ = toPpq;
            return;
        }

        const double bpb = std::max (1.0e-9, clock_.beatsPerBar());
        // Integer bar indices whose downbeat lies in (fromPpq, toPpq]
        const int first = static_cast<int> (std::floor (fromPpq / bpb + 1.0e-9)) + 1;
        const int last = static_cast<int> (std::floor (toPpq / bpb + 1.0e-9));
        for (int b = first; b <= last; ++b)
            onBar (b, static_cast<double> (b) * bpb);

        lastProcessedPpq_ = toPpq;
    }

    /** After seek: reset and re-seed, then fast-forward deterministically to target PPQ. */
    void handleSeek (double targetPpq) noexcept
    {
        const uint64_t seed = pendingReseed_ ? armedSeed_ : masterSeed_;
        reseed (seed);
        // Fast-forward bar-by-bar without audio — same decisions as real-time from 0
        double ppq = 0.0;
        const double end = std::max (0.0, targetPpq);
        while (ppq < end)
        {
            const double next = std::min (end, ppq + clock_.beatsPerBar());
            // Process exclusive of ending mid-bar: only full bars via processTimeRange
            processTimeRange (ppq, next, true);
            ppq = next;
            if (next >= end)
                break;
        }
        lastProcessedPpq_ = targetPpq;
    }

    void notifySeekReset (double ppq) noexcept
    {
        handleSeek (ppq);
    }

    double lastProcessedPpq() const noexcept { return lastProcessedPpq_; }

    MusicalClock& clock() noexcept { return clock_; }
    const MusicalClock& clock() const noexcept { return clock_; }

private:
    static uint64_t hashTag (const char* s) noexcept
    {
        uint64_t h = 0xcbf29ce484222325ull;
        while (*s)
        {
            h ^= static_cast<uint64_t> (*s++);
            h *= 0x100000001b3ull;
        }
        return h;
    }

    void setupIdentities() noexcept
    {
        // Foundation
        identities_[0] = { 2, 1, 3, 4, 8, 0.28f, 0.35f, 0 };
        // Middle
        identities_[1] = { 2, 2, 4, 2, 4, 0.42f, 0.8f, 3 };
        // Texture
        identities_[2] = { 3, 2, 4, 1, 4, 0.55f, 1.4f, 1 };
    }

    void initVoicesAtOrigin (double beat, int currentBar) noexcept
    {
        const int active = targetActiveCount();
        const int starts[3][2] = { { 0, 2 }, { 3, 2 }, { 0, 3 } }; // deg, oct → D2, A2, D3

        for (int i = 0; i < kMaxVoices; ++i)
        {
            auto& v = voices_[static_cast<size_t> (i)];
            v.pitch.degree = starts[i][0];
            v.pitch.octave = starts[i][1];
            v.pitch.midiNote = Scale::toMidi (v.pitch.degree, v.pitch.octave);
            v.pitch.chromatic = false;
            v.active = i < active;
            scheduleNextEval (i, currentBar);

            if (v.active)
            {
                emit ({ beat, 0.0, v.pitch.midiNote, 0.85f, i, EventType::VoiceEnter });
                memory_.push (v.pitch.midiNote);
            }
        }
    }

    void scheduleNextEval (int voice, int currentBar) noexcept
    {
        const auto& id = identities_[static_cast<size_t> (voice)];
        const int span = std::max (1, id.evalPeriodBarsMax - id.evalPeriodBarsMin + 1);
        // Density shortens periods slightly
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        const float m = std::clamp (params_.mutation, 0.0f, 1.0f);
        int period = id.evalPeriodBarsMin
                     + static_cast<int> (rhythmRng_.nextFloat() * static_cast<float> (span));
        period = std::max (1, static_cast<int> (std::round (static_cast<float> (period) * (1.0f - 0.35f * d) * (1.0f - 0.25f * m))));
        voices_[static_cast<size_t> (voice)].nextEvalBar = currentBar + period;
    }

    void onBar (int barIndex, double barPpq) noexcept
    {
        if (pendingReseed_)
        {
            applySeedAtBar (armedSeed_, barIndex, barPpq);
            return;
        }

        // Population adjust at bar
        adjustPopulation (barIndex, barPpq);

        phrases_.onBar (barIndex);

        for (int i = 0; i < kMaxVoices; ++i)
        {
            auto& v = voices_[static_cast<size_t> (i)];
            if (! v.active)
                continue;
            if (barIndex < v.nextEvalBar)
                continue;

            maybeChangePitch (i, barPpq);
            scheduleNextEval (i, barIndex);
        }
    }

    void adjustPopulation (int /*barIndex*/, double barPpq) noexcept
    {
        const int target = targetActiveCount();
        int active = 0;
        for (auto& v : voices_)
            if (v.active)
                ++active;

        while (active < target)
        {
            // Enter lowest inactive voice index (stable) with voice RNG spice for pitch
            int chosen = -1;
            for (int i = 0; i < kMaxVoices; ++i)
            {
                if (! voices_[static_cast<size_t> (i)].active)
                {
                    chosen = i;
                    break;
                }
            }
            if (chosen < 0)
                break;

            auto& v = voices_[static_cast<size_t> (chosen)];
            const auto& id = identities_[static_cast<size_t> (chosen)];
            v.pitch.degree = id.preferredDegree;
            v.pitch.octave = id.homeOctave;
            v.pitch.midiNote = Scale::toMidi (v.pitch.degree, v.pitch.octave);
            v.pitch.chromatic = false;
            v.active = true;
            scheduleNextEval (chosen, static_cast<int> (std::floor (barPpq / std::max (1.0e-9, clock_.beatsPerBar()))));
            emit ({ barPpq, 0.0, v.pitch.midiNote, 0.8f, chosen, EventType::VoiceEnter });
            memory_.push (v.pitch.midiNote);
            ++active;
            (void) voiceRng_.nextFloat(); // consume so voice stream advances on entries
        }

        while (active > target)
        {
            // Exit highest active index
            int chosen = -1;
            for (int i = kMaxVoices - 1; i >= 0; --i)
            {
                if (voices_[static_cast<size_t> (i)].active)
                {
                    chosen = i;
                    break;
                }
            }
            if (chosen < 0)
                break;

            auto& v = voices_[static_cast<size_t> (chosen)];
            v.active = false;
            emit ({ barPpq, 0.0, v.pitch.midiNote, 0.0f, chosen, EventType::VoiceExit });
            --active;
            (void) voiceRng_.nextFloat();
        }
    }

    void maybeChangePitch (int voiceIndex, double barPpq) noexcept
    {
        auto& v = voices_[static_cast<size_t> (voiceIndex)];
        const auto& id = identities_[static_cast<size_t> (voiceIndex)];
        const float m = std::clamp (params_.mutation, 0.0f, 1.0f);
        const float d = std::clamp (params_.density, 0.0f, 1.0f);

        float chance = id.changeBias * (0.45f + 0.9f * m) * (0.75f + 0.4f * d);
        chance = std::clamp (chance, 0.05f, 0.92f);

        if (pitchRng_.nextFloat() > chance)
            return; // stillness

        PitchState next = v.pitch;
        const bool followPhrase = (pitchRng_.nextFloat() < phrases_.followBias());

        if (followPhrase)
        {
            const int delta = phrases_.consumePhraseStep();
            next.degree += delta;
            next.chromatic = false;
            while (next.degree < 0)
            {
                next.degree += Scale::kNumDegrees;
                --next.octave;
            }
            while (next.degree >= Scale::kNumDegrees)
            {
                next.degree -= Scale::kNumDegrees;
                ++next.octave;
            }
            next.octave = std::clamp (next.octave, id.minOctave, id.maxOctave);
            next.midiNote = Scale::toMidi (next.degree, next.octave);

            // Mild memory soft-reject: if heavily penalized, fall back toward phrase root motion (0)
            const float pen = memory_.penaltyMultiplier (next.midiNote);
            if (pen < 0.25f && pitchRng_.nextFloat() > pen)
            {
                next.degree = id.preferredDegree;
                next.octave = std::clamp (v.pitch.octave, id.minOctave, id.maxOctave);
                next.midiNote = Scale::toMidi (next.degree, next.octave);
            }
        }
        else
        {
            next = walk_.step (v.pitch, pitchRng_, memory_, m, id.chromaticBoost);
            next.octave = std::clamp (next.octave, id.minOctave, id.maxOctave);
            if (! next.chromatic)
                next.midiNote = Scale::toMidi (next.degree, next.octave);
            else
                next.midiNote = std::clamp (next.midiNote,
                                           Scale::toMidi (0, id.minOctave),
                                           Scale::toMidi (0, id.maxOctave) + 10);
        }

        if (next.midiNote == v.pitch.midiNote)
            return;

        v.pitch = next;
        memory_.push (v.pitch.midiNote);
        emit ({ barPpq, 0.0, v.pitch.midiNote, 0.75f, voiceIndex, EventType::NoteChange });
    }

    void emit (const MusicalEvent& e) noexcept
    {
        if (captureEvents_)
            captured_.push_back (e);
    }

    uint64_t masterSeed_ = 1001;
    uint64_t armedSeed_ = 1001;
    bool pendingReseed_ = false;
    ComposerParams params_{};
    MusicalClock clock_{};
    MusicalMemory memory_{};
    RandomWalk walk_{};
    DeterministicRNG pitchRng_{};
    DeterministicRNG voiceRng_{};
    DeterministicRNG structureRng_{};
    DeterministicRNG rhythmRng_{};
    DeterministicRNG timbreRng_{};
    PhraseEngine phrases_{};
    std::array<VoiceIdentity, kMaxVoices> identities_{};
    std::array<VoiceRuntime, kMaxVoices> voices_{};
    double lastProcessedPpq_ = 0.0;
    bool captureEvents_ = false;
    std::vector<MusicalEvent> captured_;
};

} // namespace pfl::generative

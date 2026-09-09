#pragma once

#include "DeterministicRNG.h"
#include "MidiNoteTracker.h"
#include "MidiTrace.h"
#include "MusicalClock.h"
#include "MusicalMemory.h"
#include "PhraseDNA.h"
#include "RandomWalk.h"
#include "RhythmDNA.h"
#include "Scale.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pfl::generative
{

struct ConductorParams
{
    float density = 0.45f;  // Stage 2: occupancy / rest / duration / syncopation allowance
    float mutation = 0.35f; // Stage 2: pitch adventurousness + Rhythm/Phrase DNA lifespan
};

/**
 * Broken Conductor Stage 2B — Foundation + RhythmDNA with live DENSITY/MUTATION response.
 * Does not alter Drone Organism Composer behavior.
 */
class ConductorEngine
{
public:
    static constexpr int kAlgorithmVersion = 3; // Stage 2B control response
    static constexpr int kMidiChannel = 1;
    static constexpr int kVoice = 0;
    static constexpr int kMinMidi = 26; // D1
    static constexpr int kMaxMidi = 50; // D3
    static constexpr int kStartMidi = 38; // D2
    static constexpr double kSlotBeats = RhythmEngine::kSlotBeats;

    void setCapture (bool enabled) noexcept { capture_ = enabled; }
    const std::vector<MidiTraceEvent>& captured() const noexcept { return captured_; }
    void clearCaptured() noexcept { captured_.clear(); }

    void reseed (uint64_t masterSeed) noexcept
    {
        masterSeed_ = masterSeed;
        pitchRng_ = DeterministicRNG::derived (masterSeed, hashTag ("pitch"));
        rhythmRng_ = DeterministicRNG::derived (masterSeed, hashTag ("rhythm"));
        velocityRng_ = DeterministicRNG::derived (masterSeed, hashTag ("velocity"));
        auto phraseRng = DeterministicRNG::derived (masterSeed, hashTag ("phrase"));
        memory_.clear();
        captured_.clear();
        tracker_.clear();
        lastProcessedPpq_ = 0.0;
        clock_.reset (0.0);
        phrases_.reset (phraseRng, params_.mutation, 0);
        rhythm_.reset (rhythmRng_, params_.mutation, params_.density, 0);
        pitch_.degree = 0;
        pitch_.octave = 2;
        pitch_.midiNote = kStartMidi;
        pitch_.chromatic = false;
        sounding_ = false;
        noteOffPpq_ = 0.0;
        soundingNote_ = kStartMidi;
        nextPitchEvalBar_ = 4;
        lastSlotWasRest_ = true;
        lastRhythmBar_ = -1;
        paramsDirty_ = false;
        pendingPrevDensity_ = params_.density;
        pendingPrevMutation_ = params_.mutation;
        pending_.clear();
        schedulePitchEval (0);
    }

    uint64_t masterSeed() const noexcept { return masterSeed_; }

    void setParams (const ConductorParams& p) noexcept
    {
        if (std::abs (p.density - params_.density) > 1.0e-6f
            || std::abs (p.mutation - params_.mutation) > 1.0e-6f)
        {
            if (! paramsDirty_)
            {
                pendingPrevDensity_ = params_.density;
                pendingPrevMutation_ = params_.mutation;
            }
            paramsDirty_ = true;
        }
        params_ = p;
        phrases_.setMutation (p.mutation);
        rhythm_.setMutation (p.mutation);
        rhythm_.setDensity (p.density);
    }

    /** Last density/mutation observed by the engine (for diagnostics / tests). */
    float diagnosticDensity() const noexcept { return params_.density; }
    float diagnosticMutation() const noexcept { return params_.mutation; }

    ConductorParams params() const noexcept { return params_; }

    MusicalClock& clock() noexcept { return clock_; }
    const MusicalClock& clock() const noexcept { return clock_; }
    const MidiNoteTracker& tracker() const noexcept { return tracker_; }
    const PhraseEngine& phrases() const noexcept { return phrases_; }
    const RhythmEngine& rhythm() const noexcept { return rhythm_; }
    RhythmEngine& rhythm() noexcept { return rhythm_; }

    int currentMidiNote() const noexcept { return pitch_.midiNote; }
    bool sounding() const noexcept { return sounding_; }

    /**
     * Advance (fromPpq, toPpq]. Decisions only at absolute sixteenth slots —
     * never per processBlock. When playing=false, no new note-ons / no rhythm RNG.
     * When emitOutput=false, advance state without queueing MIDI (seek reconstruct).
     */
    void processTimeRange (double fromPpq, double toPpq, bool playing, bool emitOutput = true) noexcept
    {
        emitOutput_ = emitOutput;
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

        const double slot = kSlotBeats;
        const std::int64_t first = static_cast<std::int64_t> (std::floor (fromPpq / slot + 1.0e-9)) + 1;
        const std::int64_t last = static_cast<std::int64_t> (std::floor (toPpq / slot + 1.0e-9));
        for (std::int64_t i = first; i <= last; ++i)
            onSlot (i, static_cast<double> (i) * slot);

        lastProcessedPpq_ = toPpq;
        emitOutput_ = true;
    }

    void handleSeek (double targetPpq) noexcept
    {
        const uint64_t seed = masterSeed_;
        const ConductorParams p = params_;
        const bool wasCapture = capture_;
        capture_ = false;
        reseed (seed);
        setParams (p);
        const double end = std::max (0.0, targetPpq);
        double ppq = 0.0;
        while (ppq < end)
        {
            const double next = std::min (end, ppq + 1.0);
            processTimeRange (ppq, next, true, false);
            ppq = next;
        }
        capture_ = wasCapture;
        pending_.clear();
        lastProcessedPpq_ = targetPpq;
    }

    bool needsHostRetrigger() const noexcept { return sounding_; }
    int soundingMidiNote() const noexcept { return soundingNote_; }
    int lastVelocity() const noexcept { return lastVelocity_; }

    void panic (double ppq) noexcept
    {
        if (sounding_)
        {
            emitOff (ppq, soundingNote_);
            sounding_ = false;
        }
        std::vector<MidiTraceEvent> panicEv;
        tracker_.panicTo (panicEv, ppq, kVoice);
        if (capture_)
            captured_.insert (captured_.end(), panicEv.begin(), panicEv.end());
        tracker_.clear();
    }

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

    void schedulePitchEval (int currentBar) noexcept
    {
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        const float m = std::clamp (params_.mutation, 0.0f, 1.0f);
        // Stage 2B: mut=1 → periods ~1–3 bars (was ~3–5)
        int period = 1 + static_cast<int> (pitchRng_.nextFloat() * (4.0f - 2.5f * m)); // 1..4 → 1..2 at high m
        period = std::max (1, static_cast<int> (std::round (static_cast<float> (period)
                                                            * (1.0f - 0.25f * d) * (1.0f - 0.35f * m))));
        nextPitchEvalBar_ = currentBar + period;
    }

    void maybeChangePitch (double /*beatPpq*/) noexcept
    {
        const float m = std::clamp (params_.mutation, 0.0f, 1.0f);
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        // Stage 2B: mut=1 → chance ~0.55–0.75 (was ~0.35)
        float chance = 0.22f * (0.55f + 1.35f * m) * (0.80f + 0.35f * d);
        chance = std::clamp (chance, 0.08f, 0.92f);
        if (pitchRng_.nextFloat() > chance)
            return;

        PitchState next = pitch_;
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
            next.octave = std::clamp (next.octave, 1, 3);
            next.midiNote = Scale::toMidi (next.degree, next.octave);
            // Soft memory reject weaker at high mutation
            const float pen = memory_.penaltyMultiplier (next.midiNote);
            const float rejectGate = 0.25f * (1.0f - 0.7f * m);
            if (pen < rejectGate && pitchRng_.nextFloat() > pen)
            {
                next.degree = 0;
                next.octave = std::clamp (pitch_.octave, 1, 3);
                next.midiNote = Scale::toMidi (next.degree, next.octave);
            }
        }
        else
        {
            // Higher chromaticBoost at high mutation (was fixed 0.35)
            const float chromaBoost = 0.35f + 1.1f * m;
            next = walk_.step (pitch_, pitchRng_, memory_, m, chromaBoost);
            next.octave = std::clamp (next.octave, 1, 3);
            if (! next.chromatic)
                next.midiNote = Scale::toMidi (next.degree, next.octave);
        }

        next.midiNote = std::clamp (next.midiNote, kMinMidi, kMaxMidi);
        if (next.midiNote == pitch_.midiNote)
            return;
        pitch_ = next;
        memory_.push (pitch_.midiNote);
    }

    void applyLiveParamResponse (int bar) noexcept
    {
        if (! paramsDirty_)
            return;
        const float prevD = pendingPrevDensity_;
        const float prevM = pendingPrevMutation_;
        rhythm_.respondToLiveParams (bar, prevD, prevM);
        phrases_.respondToLiveParams (bar, prevM);
        const float m = params_.mutation;
        const int maxDelay = std::max (1, static_cast<int> (std::lround (1.0 + 3.0 * (1.0 - static_cast<double> (m)))));
        nextPitchEvalBar_ = std::min (nextPitchEvalBar_, bar + maxDelay);
        paramsDirty_ = false;
        pendingPrevDensity_ = params_.density;
        pendingPrevMutation_ = params_.mutation;
    }

    int chooseVelocity (std::int64_t absoluteSlot, bool afterRest) noexcept
    {
        const int bias = RhythmEngine::accentBiasForSlot (absoluteSlot, afterRest);
        const float u = velocityRng_.nextFloat();
        int v = 80 + bias + static_cast<int> (std::round (4.0f * (u - 0.5f)));
        return std::clamp (v, 64, 96);
    }

    void onSlot (std::int64_t absoluteSlot, double slotPpq) noexcept
    {
        const double beatsPerBar = std::max (1.0e-9, clock_.beatsPerBar());
        const int bar = static_cast<int> (std::floor (slotPpq / beatsPerBar));

        if (sounding_ && slotPpq + 1.0e-9 >= noteOffPpq_)
        {
            emitOff (slotPpq, soundingNote_);
            sounding_ = false;
        }

        if (bar != lastRhythmBar_)
        {
            applyLiveParamResponse (bar);
            phrases_.onBar (bar);
            rhythm_.onBar (bar);
            lastRhythmBar_ = bar;
        }
        else if (paramsDirty_)
        {
            // Apply as soon as we know the bar (same bar mid-slot) — still ≤ next bar
            applyLiveParamResponse (bar);
        }

        const double barStart = static_cast<double> (bar) * beatsPerBar;
        const bool atBarStart = std::abs (slotPpq - barStart) < 1.0e-9;
        if (atBarStart && bar >= nextPitchEvalBar_)
        {
            maybeChangePitch (slotPpq);
            schedulePitchEval (bar);
        }

        const RhythmCell cell = rhythm_.dna().cellForAbsoluteSlot (absoluteSlot);
        const bool afterRest = lastSlotWasRest_;

        if (cell == RhythmCell::Onset)
        {
            // Stage 2B: density gates DNA onset expression immediately
            if (! RhythmEngine::shouldExpressOnset (masterSeed_, absoluteSlot, params_.density))
            {
                if (sounding_)
                {
                    emitOff (slotPpq, soundingNote_);
                    sounding_ = false;
                }
                lastSlotWasRest_ = true;
                return;
            }

            const int n = rhythm_.dna().lengthCells();
            const int local = n > 0 ? static_cast<int> (((absoluteSlot % n) + n) % n) : 0;
            double dur = rhythm_.durationBeatsAt (local);
            dur = std::max (kSlotBeats, dur);

            if (sounding_)
                emitOff (slotPpq, soundingNote_);

            const int vel = chooseVelocity (absoluteSlot, afterRest);
            lastVelocity_ = vel;
            emitOn (slotPpq, pitch_.midiNote, vel);
            sounding_ = true;
            soundingNote_ = pitch_.midiNote;
            noteOffPpq_ = slotPpq + dur;
            lastSlotWasRest_ = false;
            return;
        }

        if (cell == RhythmCell::Rest)
        {
            if (sounding_)
            {
                emitOff (slotPpq, soundingNote_);
                sounding_ = false;
            }
            lastSlotWasRest_ = true;
            return;
        }

        lastSlotWasRest_ = false;
    }

    void emitOn (double ppq, int note, int velocity) noexcept
    {
        if (! emitOutput_)
        {
            if (tracker_.isActive (kMidiChannel, note))
                tracker_.noteOff (kMidiChannel, note);
            tracker_.noteOn (kMidiChannel, note);
            return;
        }
        if (tracker_.isActive (kMidiChannel, note))
            emitOff (ppq, note);
        tracker_.noteOn (kMidiChannel, note);
        MidiTraceEvent e;
        e.ppq = ppq;
        e.channel = kMidiChannel;
        e.note = note;
        e.velocity = velocity;
        e.voice = kVoice;
        e.kind = MidiMsgKind::NoteOn;
        if (capture_)
            captured_.push_back (e);
        pending_.push_back (e);
    }

    void emitOff (double ppq, int note) noexcept
    {
        tracker_.noteOff (kMidiChannel, note);
        if (! emitOutput_)
            return;
        MidiTraceEvent e;
        e.ppq = ppq;
        e.channel = kMidiChannel;
        e.note = note;
        e.velocity = 0;
        e.voice = kVoice;
        e.kind = MidiMsgKind::NoteOff;
        if (capture_)
            captured_.push_back (e);
        pending_.push_back (e);
    }

public:
    std::vector<MidiTraceEvent> drainPending() noexcept
    {
        std::vector<MidiTraceEvent> out;
        out.swap (pending_);
        return out;
    }

    double lastProcessedPpq() const noexcept { return lastProcessedPpq_; }

private:
    uint64_t masterSeed_ = 1001;
    ConductorParams params_{};
    MusicalClock clock_{};
    MusicalMemory memory_{};
    RandomWalk walk_{};
    PhraseEngine phrases_{};
    RhythmEngine rhythm_{};
    DeterministicRNG pitchRng_{};
    DeterministicRNG rhythmRng_{};
    DeterministicRNG velocityRng_{};
    MidiNoteTracker tracker_{};
    PitchState pitch_{};
    bool sounding_ = false;
    int soundingNote_ = kStartMidi;
    int lastVelocity_ = 80;
    double noteOffPpq_ = 0.0;
    int nextPitchEvalBar_ = 4;
    int lastRhythmBar_ = -1;
    bool lastSlotWasRest_ = true;
    bool paramsDirty_ = false;
    float pendingPrevDensity_ = 0.45f;
    float pendingPrevMutation_ = 0.35f;
    double lastProcessedPpq_ = 0.0;
    bool capture_ = false;
    bool emitOutput_ = true;
    std::vector<MidiTraceEvent> captured_;
    std::vector<MidiTraceEvent> pending_;
};

} // namespace pfl::generative

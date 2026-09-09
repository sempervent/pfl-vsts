#pragma once

#include "DeterministicRNG.h"
#include "MidiNoteTracker.h"
#include "MidiTrace.h"
#include "MusicalClock.h"
#include "MusicalMemory.h"
#include "PhraseDNA.h"
#include "RandomWalk.h"
#include "Scale.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pfl::generative
{

struct ConductorParams
{
    float density = 0.45f;  // rest probability + duration bias (NOT voice count)
    float mutation = 0.35f; // pitch adventurousness + duration bias + Phrase DNA
};

/**
 * Broken Conductor Stage 1 — one Foundation MIDI voice.
 * Musical-time beat grid; explicit durations and rests.
 * Does not alter Drone Organism Composer behavior.
 */
class ConductorEngine
{
public:
    static constexpr int kAlgorithmVersion = 1; // Broken Conductor engine version
    static constexpr int kMidiChannel = 1;
    static constexpr int kVoice = 0;
    static constexpr int kMinMidi = 26; // D1
    static constexpr int kMaxMidi = 50; // D3
    static constexpr int kStartMidi = 38; // D2

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
        pitch_.degree = 0;
        pitch_.octave = 2;
        pitch_.midiNote = kStartMidi;
        pitch_.chromatic = false;
        sounding_ = false;
        noteOffPpq_ = 0.0;
        soundingNote_ = kStartMidi;
        nextPitchEvalBar_ = 4;
        pending_.clear();
        schedulePitchEval (0);
    }

    uint64_t masterSeed() const noexcept { return masterSeed_; }

    void setParams (const ConductorParams& p) noexcept
    {
        params_ = p;
        phrases_.setMutation (p.mutation);
    }

    ConductorParams params() const noexcept { return params_; }

    MusicalClock& clock() noexcept { return clock_; }
    const MusicalClock& clock() const noexcept { return clock_; }
    const MidiNoteTracker& tracker() const noexcept { return tracker_; }
    const PhraseEngine& phrases() const noexcept { return phrases_; }

    int currentMidiNote() const noexcept { return pitch_.midiNote; }
    bool sounding() const noexcept { return sounding_; }

    /**
     * Advance (fromPpq, toPpq]. When playing=false, only panic is expected from caller;
     * no new note-ons. When emitOutput=false, advance state/RNGs without queueing MIDI
     * (used for seek fast-forward on a non-emitting reconstruct).
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

        const int first = static_cast<int> (std::floor (fromPpq + 1.0e-9)) + 1;
        const int last = static_cast<int> (std::floor (toPpq + 1.0e-9));
        for (int b = first; b <= last; ++b)
            onBeat (static_cast<double> (b));

        lastProcessedPpq_ = toPpq;
        emitOutput_ = true;
    }

    /**
     * Seek reconstruct: reseed + silent fast-forward to target.
     * Preserves mid-sustain occupancy so post-seek rhythm RNG matches uninterrupted run.
     * Caller must panic host MIDI before calling; then re-articulate sounding note if needed.
     */
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
        // Keep sounding_ / noteOffPpq_ / tracker as reconstructed at target.
    }

    /** True if a note is mid-sustain after seek reconstruct (host should re-articulate). */
    bool needsHostRetrigger() const noexcept { return sounding_; }
    int soundingMidiNote() const noexcept { return soundingNote_; }
    int lastVelocity() const noexcept { return lastVelocity_; }

    /** Emit NoteOffs for all owned notes into capture (and clear sounding). */
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
        // Foundation: 4–8 bars, density/mutation shorten slightly (mirror Composer)
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        const float m = std::clamp (params_.mutation, 0.0f, 1.0f);
        int period = 4 + static_cast<int> (rhythmRng_.nextFloat() * 5.0f); // 4..8
        period = std::max (1, static_cast<int> (std::round (static_cast<float> (period)
                                                            * (1.0f - 0.35f * d) * (1.0f - 0.25f * m))));
        nextPitchEvalBar_ = currentBar + period;
    }

    void maybeChangePitch (double beatPpq) noexcept
    {
        const float m = std::clamp (params_.mutation, 0.0f, 1.0f);
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        float chance = 0.28f * (0.45f + 0.9f * m) * (0.75f + 0.4f * d);
        chance = std::clamp (chance, 0.05f, 0.92f);
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
            const float pen = memory_.penaltyMultiplier (next.midiNote);
            if (pen < 0.25f && pitchRng_.nextFloat() > pen)
            {
                next.degree = 0;
                next.octave = std::clamp (pitch_.octave, 1, 3);
                next.midiNote = Scale::toMidi (next.degree, next.octave);
            }
        }
        else
        {
            next = walk_.step (pitch_, pitchRng_, memory_, m, 0.35f);
            next.octave = std::clamp (next.octave, 1, 3);
            if (! next.chromatic)
                next.midiNote = Scale::toMidi (next.degree, next.octave);
        }

        next.midiNote = std::clamp (next.midiNote, kMinMidi, kMaxMidi);
        if (next.midiNote == pitch_.midiNote)
            return;
        pitch_ = next;
        memory_.push (pitch_.midiNote);
        (void) beatPpq;
    }

    float restProbability() const noexcept
    {
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        return std::clamp (0.55f - 0.45f * d, 0.10f, 0.55f);
    }

    double chooseDurationBeats() noexcept
    {
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        const float m = std::clamp (params_.mutation, 0.0f, 1.0f);
        // Blend duration weights: density → shorter; mutation → shorter
        // Base at dens=0.45 mut=0.35 ≈ 50/35/15
        float w1 = 0.50f + 0.20f * (d - 0.45f) + 0.15f * (m - 0.35f);
        float w2 = 0.35f - 0.10f * (d - 0.45f) - 0.05f * (m - 0.35f);
        float w4 = 1.0f - w1 - w2;
        w1 = std::clamp (w1, 0.20f, 0.75f);
        w2 = std::clamp (w2, 0.10f, 0.50f);
        w4 = std::clamp (1.0f - w1 - w2, 0.05f, 0.40f);
        const float sum = w1 + w2 + w4;
        w1 /= sum;
        w2 /= sum;
        const float r = rhythmRng_.nextFloat();
        if (r < w1)
            return 1.0;
        if (r < w1 + w2)
            return 2.0;
        return 4.0;
    }

    int chooseVelocity() noexcept
    {
        // Center 80, ± ±16 → [64, 96]
        const float u = velocityRng_.nextFloat();
        int v = 80 + static_cast<int> (std::round (16.0f * (u - 0.5f)));
        return std::clamp (v, 64, 96);
    }

    void onBeat (double beatPpq) noexcept
    {
        const int bar = static_cast<int> (std::floor (beatPpq / std::max (1.0e-9, clock_.beatsPerBar())));

        // Release due notes at this beat
        if (sounding_ && beatPpq + 1.0e-9 >= noteOffPpq_)
        {
            emitOff (beatPpq, soundingNote_);
            sounding_ = false;
        }

        if (sounding_)
            return; // still sustaining

        phrases_.onBar (bar); // may mutate DNA at bar boundaries (idempotent if early)

        if (bar >= nextPitchEvalBar_ && std::abs (beatPpq - bar * clock_.beatsPerBar()) < 1.0e-6)
        {
            maybeChangePitch (beatPpq);
            schedulePitchEval (bar);
        }
        else if (bar >= nextPitchEvalBar_)
        {
            // Pitch eval mid-bar: apply on this beat then reschedule
            maybeChangePitch (beatPpq);
            schedulePitchEval (bar);
        }

        if (rhythmRng_.nextFloat() < restProbability())
            return; // rest this beat

        double dur = chooseDurationBeats();
        dur = std::max (1.0, dur);

        const int vel = chooseVelocity();
        lastVelocity_ = vel;
        emitOn (beatPpq, pitch_.midiNote, vel);
        sounding_ = true;
        soundingNote_ = pitch_.midiNote;
        noteOffPpq_ = beatPpq + dur;
    }

    void emitOn (double ppq, int note, int velocity) noexcept
    {
        if (! emitOutput_)
        {
            // Silent reconstruct: update ownership only
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
    /** Drain newly generated MIDI since last drain (for plugin/renderer). */
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
    double lastProcessedPpq_ = 0.0;
    bool capture_ = false;
    bool emitOutput_ = true;
    std::vector<MidiTraceEvent> captured_;
    std::vector<MidiTraceEvent> pending_;
};

} // namespace pfl::generative

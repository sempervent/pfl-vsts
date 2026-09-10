#pragma once

#include "DeterministicRNG.h"
#include "EnsembleTypes.h"
#include "MidiNoteTracker.h"
#include "MidiTrace.h"
#include "MusicalClock.h"
#include "MusicalMemory.h"
#include "PhraseDNA.h"
#include "RandomWalk.h"
#include "RhythmDNA.h"
#include "Scale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace pfl::generative
{

struct ConductorParams
{
    float density = 0.45f;
    float mutation = 0.35f;
};

/**
 * Broken Conductor Stage 3 — four-role generative ensemble on one MIDI channel.
 * Two-phase: propose EventIntents from a shared EnsembleState snapshot, then arbitrate.
 */
class ConductorEngine
{
public:
    static constexpr int kAlgorithmVersion = 5;
    static constexpr int kMidiChannel = 1;
    static constexpr int kNumVoices = static_cast<int> (VoiceRole::Count);
    static constexpr int kVoice = 0; // Foundation (compat)
    static constexpr int kMinMidi = 26; // Foundation low
    static constexpr int kMaxMidi = 86; // Accent high (ensemble span)
    static constexpr int kStartMidi = 38;
    static constexpr double kSlotBeats = RhythmEngine::kSlotBeats;
    static constexpr int kCongestionWindowSlots = 8;
    static constexpr int kGapWindowSlots = 12;

    struct CollisionStats
    {
        int attemptedSamePitch = 0;
        int shifted = 0;
        int suppressed = 0;
    };

    void setCapture (bool enabled) noexcept { capture_ = enabled; }
    const std::vector<MidiTraceEvent>& captured() const noexcept { return captured_; }
    /** Full pre-filter ensemble stream (for Stage 4 identity proofs). */
    const std::vector<MidiTraceEvent>& capturedEnsemble() const noexcept { return ensembleCaptured_; }
    void clearCaptured() noexcept
    {
        captured_.clear();
        ensembleCaptured_.clear();
    }

    OutputRole outputRole() const noexcept { return outputRole_; }

    /**
     * Configuration only — does not reseed or alter generative params.
     * Flushes currently emitted (projected) notes so the host never hangs.
     */
    void setOutputRole (OutputRole role) noexcept
    {
        if (role == outputRole_)
            return;
        flushEmittedNotes (lastProcessedPpq_);
        outputRole_ = role;
    }

    const CollisionStats& collisionStats() const noexcept { return collisionStats_; }
    void clearCollisionStats() noexcept { collisionStats_ = {}; }

    /** Diagnostic: raw Foundation pitch RNG draws since reseed (isolation tests). */
    uint64_t foundationPitchDraws() const noexcept { return voices_[0].pitchDraws; }

    /**
     * Performance intervention flags (Stage 5). Applied before composition decisions.
     * collapsePhase: 0=none, 1=Destabilize … 4=Residue.
     */
    void setPerformanceFlags (bool evolutionLocked, bool silenceActive, bool hungerPaused,
                              int collapsePhase) noexcept
    {
        evolutionLocked_ = evolutionLocked;
        silenceActive_ = silenceActive;
        hungerPaused_ = hungerPaused;
        collapsePhase_ = std::clamp (collapsePhase, 0, 4);
    }

    bool evolutionLocked() const noexcept { return evolutionLocked_; }
    bool silenceActive() const noexcept { return silenceActive_; }
    int collapsePhase() const noexcept { return collapsePhase_; }

    /**
     * One bounded manual MUTATE using an external RNG (does not touch autonomous streams).
     * Returns mutated VoiceRole index; writes short detail for traces.
     */
    int applyManualMutation (DeterministicRNG& manualRng, int barIndex, char* detailOut, size_t detailCap) noexcept
    {
        // Role weights: Foundation low, Pulse mid, Wanderer high, Accent low/mid
        const float r = manualRng.nextFloat();
        VoiceRole role = VoiceRole::Wanderer;
        if (r < 0.10f)
            role = VoiceRole::Foundation;
        else if (r < 0.35f)
            role = VoiceRole::Pulse;
        else if (r < 0.80f)
            role = VoiceRole::Wanderer;
        else
            role = VoiceRole::Accent;

        auto& v = voices_[static_cast<size_t> (role)];
        const bool doPhrase = (manualRng.nextFloat() < 0.55f) || role == VoiceRole::Wanderer;
        if (doPhrase)
        {
            v.phrases.manualMutateOne (manualRng, barIndex);
            if (detailOut != nullptr && detailCap > 0)
                std::snprintf (detailOut, detailCap, "%s PhraseDNA gen=%d",
                               voiceRoleName (role), v.phrases.dna().generation);
        }
        else
        {
            v.rhythm.manualMutateOne (manualRng, barIndex);
            if (detailOut != nullptr && detailCap > 0)
                std::snprintf (detailOut, detailCap, "%s RhythmDNA gen=%d",
                               voiceRoleName (role), v.rhythm.dna().generation);
        }
        return static_cast<int> (role);
    }

    void reseed (uint64_t masterSeed) noexcept
    {
        masterSeed_ = masterSeed;
        arbiterRng_ = DeterministicRNG::derived (masterSeed, hashTag ("ensemble/arbitrate"));
        memoryEnsemble_.clear();
        captured_.clear();
        tracker_.clear();
        collisionStats_ = {};
        lastProcessedPpq_ = 0.0;
        clock_.reset (0.0);
        // Do not clear pending_ here — panic NoteOffs from the same block must reach the host.
        ensembleCaptured_.clear();
        emittedTracker_.clear();
        // outputRole_ preserved across reseed (configuration)
        // performance flags: preserved — controller calls setPerformanceFlags after
        recentOnsets_.fill (0);
        recentOnsetCursor_ = 0;
        onsetsThisBar_ = 0;
        lastBudgetBar_ = -1;
        foundationPitchChangedSlot_ = -100000;
        pulseGestureEndSlot_ = -100000;
        paramsDirty_ = false;
        pendingPrevDensity_ = params_.density;
        pendingPrevMutation_ = params_.mutation;

        for (int i = 0; i < kNumVoices; ++i)
            initVoice (static_cast<VoiceRole> (i), masterSeed);
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
        for (auto& v : voices_)
        {
            const float em = roleEffectiveMutation (v.role, p.mutation);
            v.phrases.setMutation (em);
            v.rhythm.setMutation (em);
            v.rhythm.setDensity (p.density);
        }
    }

    float diagnosticDensity() const noexcept { return params_.density; }
    float diagnosticMutation() const noexcept { return params_.mutation; }
    ConductorParams params() const noexcept { return params_; }

    MusicalClock& clock() noexcept { return clock_; }
    const MusicalClock& clock() const noexcept { return clock_; }
    const MidiNoteTracker& tracker() const noexcept { return tracker_; }

    /** Stage 2 compatibility: Foundation phrase/rhythm accessors. */
    const PhraseEngine& phrases() const noexcept { return voices_[0].phrases; }
    const RhythmEngine& rhythm() const noexcept { return voices_[0].rhythm; }
    RhythmEngine& rhythm() noexcept { return voices_[0].rhythm; }

    const PhraseEngine& phrasesFor (VoiceRole r) const noexcept
    {
        return voices_[static_cast<int> (r)].phrases;
    }
    const RhythmEngine& rhythmFor (VoiceRole r) const noexcept
    {
        return voices_[static_cast<int> (r)].rhythm;
    }

    int currentMidiNote() const noexcept { return voices_[0].pitch.midiNote; }
    bool sounding() const noexcept
    {
        for (const auto& v : voices_)
            if (v.sounding)
                return true;
        return false;
    }

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
        const bool evo = evolutionLocked_;
        const bool sil = silenceActive_;
        const bool hung = hungerPaused_;
        const int col = collapsePhase_;
        capture_ = false;
        reseed (seed);
        setParams (p);
        setPerformanceFlags (evo, sil, hung, col);
        const double end = std::max (0.0, targetPpq);
        double ppq = 0.0;
        while (ppq < end)
        {
            const double next = std::min (end, ppq + 1.0);
            // Catch-up is silent to host; skip commits entirely when silenced
            if (! sil)
                processTimeRange (ppq, next, true, false);
            else
                lastProcessedPpq_ = next;
            ppq = next;
        }
        capture_ = wasCapture;
        pending_.clear();
        lastProcessedPpq_ = targetPpq;
        if (sil)
        {
            for (auto& v : voices_)
            {
                v.sounding = false;
                v.noteOffPpq = 0.0;
            }
            tracker_.clear();
        }
    }

    bool needsHostRetrigger() const noexcept
    {
        for (const auto& v : voices_)
            if (v.sounding)
                return true;
        return false;
    }

    int soundingMidiNote() const noexcept { return voices_[0].soundingNote; }
    int lastVelocity() const noexcept { return voices_[0].lastVelocity; }

    /** Fill up to kNumVoices active notes for host seek re-articulation (no heap). */
    struct HostSoundingNote
    {
        int note = 0;
        int velocity = 80;
        int voice = 0;
    };

    int copySoundingNotes (HostSoundingNote* out, int maxOut) const noexcept
    {
        if (out == nullptr || maxOut <= 0)
            return 0;
        int n = 0;
        for (const auto& v : voices_)
        {
            if (! v.sounding)
                continue;
            if (! outputRolePasses (outputRole_, v.role))
                continue;
            if (n >= maxOut)
                break;
            out[n].note = v.soundingNote;
            out[n].velocity = v.lastVelocity;
            out[n].voice = static_cast<int> (v.role);
            ++n;
        }
        return n;
    }

    /** After host seek re-articulation, register projected notes as emitted. */
    void markProjectedSoundingAsEmitted (double ppq) noexcept
    {
        for (const auto& v : voices_)
        {
            if (! v.sounding || ! outputRolePasses (outputRole_, v.role))
                continue;
            emittedTracker_.noteOn (kMidiChannel, v.soundingNote, static_cast<int> (v.role),
                                    ppq, v.noteOffPpq);
        }
    }

    void panic (double ppq) noexcept
    {
        // Host-facing offs only for notes this instance actually emitted
        flushEmittedNotes (ppq);
        tracker_.clear();
        for (auto& v : voices_)
        {
            v.sounding = false;
            v.noteOffPpq = 0.0;
        }
    }

    std::vector<MidiTraceEvent> drainPending() noexcept
    {
        std::vector<MidiTraceEvent> out;
        out.swap (pending_);
        return out;
    }

    double lastProcessedPpq() const noexcept { return lastProcessedPpq_; }

private:
    struct VoiceState
    {
        VoiceRole role = VoiceRole::Foundation;
        PhraseEngine phrases{};
        RhythmEngine rhythm{};
        MusicalMemory memory{};
        RandomWalk walk{};
        DeterministicRNG pitchRng{};
        DeterministicRNG rhythmRng{};
        DeterministicRNG velocityRng{};
        PitchState pitch{};
        bool sounding = false;
        int soundingNote = 38;
        int lastVelocity = 80;
        double noteOffPpq = 0.0;
        int nextPitchEvalBar = 4;
        bool lastSlotWasRest = true;
        int consecutiveOnsets = 0; // Wanderer run cap
        float beatsSinceContribution = 0.0f;
        int lastRhythmBar = -1;
        uint64_t pitchDraws = 0;
        bool pitchChangedThisBar = false;
    };

    struct EnsembleSnapshot
    {
        float density = 0.45f;
        float mutation = 0.35f;
        int activeCount = 0;
        int recentOnsetCount = 0;
        int slotsSinceLastOnset = 0;
        bool foundationSounding = false;
        bool pulseSounding = false;
        int foundationPitch = 38;
        bool foundationPitchChangedRecently = false;
        bool pulseGestureEndedRecently = false;
        int onsetsThisBar = 0;
        int onsetBudget = 8;
        std::array<bool, 128> pitchActive {};
    };

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

    static const char* streamTagFor (VoiceRole role, const char* purpose) noexcept
    {
        // Foundation keeps Stage 2 stream names for continuity of derivation style.
        if (role == VoiceRole::Foundation)
        {
            if (purpose[0] == 'p' && purpose[1] == 'i')
                return "pitch";
            if (purpose[0] == 'r')
                return "rhythm";
            if (purpose[0] == 'v')
                return "velocity";
            if (purpose[0] == 'p' && purpose[1] == 'h')
                return "phrase";
        }
        switch (role)
        {
            case VoiceRole::Pulse:
                if (purpose[0] == 'p' && purpose[1] == 'i') return "pulse/pitch";
                if (purpose[0] == 'r') return "pulse/rhythm";
                if (purpose[0] == 'v') return "pulse/velocity";
                return "pulse/phrase";
            case VoiceRole::Wanderer:
                if (purpose[0] == 'p' && purpose[1] == 'i') return "wanderer/pitch";
                if (purpose[0] == 'r') return "wanderer/rhythm";
                if (purpose[0] == 'v') return "wanderer/velocity";
                return "wanderer/phrase";
            case VoiceRole::Accent:
                if (purpose[0] == 'p' && purpose[1] == 'i') return "accent/pitch";
                if (purpose[0] == 'r') return "accent/rhythm";
                if (purpose[0] == 'v') return "accent/velocity";
                return "accent/phrase";
            default:
                return purpose;
        }
    }

    void initVoice (VoiceRole role, uint64_t masterSeed) noexcept
    {
        auto& v = voices_[static_cast<int> (role)];
        v = VoiceState{};
        v.role = role;
        const auto reg = registerFor (role);
        v.pitchRng = DeterministicRNG::derived (masterSeed, hashTag (streamTagFor (role, "pitch")));
        v.rhythmRng = DeterministicRNG::derived (masterSeed, hashTag (streamTagFor (role, "rhythm")));
        v.velocityRng = DeterministicRNG::derived (masterSeed, hashTag (streamTagFor (role, "velocity")));
        auto phraseRng = DeterministicRNG::derived (masterSeed, hashTag (streamTagFor (role, "phrase")));

        const float em = roleEffectiveMutation (role, params_.mutation);
        v.rhythm.setVoiceKind (static_cast<RhythmVoiceKind> (role));
        v.phrases.reset (phraseRng, em, 0);
        v.rhythm.reset (v.rhythmRng, em, params_.density, 0);

        Scale::fromMidi (reg.startMidi, v.pitch.degree, v.pitch.octave);
        v.pitch.midiNote = reg.startMidi;
        v.pitch.chromatic = false;
        v.soundingNote = reg.startMidi;
        v.nextPitchEvalBar = 2 + static_cast<int> (role); // stagger
        schedulePitchEval (v, 0);
    }

    void schedulePitchEval (VoiceState& v, int currentBar) noexcept
    {
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        const float m = roleEffectiveMutation (v.role, params_.mutation);
        v.pitchDraws += 1;
        int period = 1 + static_cast<int> (v.pitchRng.nextFloat() * (4.0f - 2.5f * m));
        period = std::max (1, static_cast<int> (std::round (static_cast<float> (period)
                                                            * (1.0f - 0.25f * d) * (1.0f - 0.35f * m))));
        if (v.role == VoiceRole::Foundation)
            period = std::max (period, 2);
        if (v.role == VoiceRole::Accent)
            period = std::max (period, 4);
        v.nextPitchEvalBar = currentBar + period;
    }

    void maybeChangePitch (VoiceState& v) noexcept
    {
        const float m = roleEffectiveMutation (v.role, params_.mutation);
        const float d = std::clamp (params_.density, 0.0f, 1.0f);
        const auto reg = registerFor (v.role);

        float chance = 0.22f * (0.55f + 1.35f * m) * (0.80f + 0.35f * d);
        if (v.role == VoiceRole::Foundation)
            chance *= 0.85f;
        if (v.role == VoiceRole::Pulse)
            chance *= 0.55f; // pitch nearly static
        if (v.role == VoiceRole::Accent)
            chance *= 0.40f;
        if (v.role == VoiceRole::Wanderer)
            chance *= 1.15f;
        chance = std::clamp (chance, 0.05f, 0.92f);

        v.pitchDraws += 1;
        if (v.pitchRng.nextFloat() > chance)
            return;

        PitchState next = v.pitch;
        v.pitchDraws += 1;
        const bool followPhrase = (v.pitchRng.nextFloat() < v.phrases.followBias());
        if (followPhrase)
        {
            const int delta = v.phrases.consumePhraseStep();
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
            next.octave = std::clamp (next.octave, reg.minOctave, reg.maxOctave);
            next.midiNote = Scale::toMidi (next.degree, next.octave);
            const float pen = v.memory.penaltyMultiplier (next.midiNote);
            const float rejectGate = 0.25f * (1.0f - 0.7f * m);
            v.pitchDraws += 1;
            if (pen < rejectGate && v.pitchRng.nextFloat() > pen)
            {
                next.degree = 0;
                next.octave = std::clamp (v.pitch.octave, reg.minOctave, reg.maxOctave);
                next.midiNote = Scale::toMidi (next.degree, next.octave);
            }
        }
        else
        {
            const float chromaBoost = 0.35f + 1.1f * m;
            next = v.walk.step (v.pitch, v.pitchRng, v.memory, m, chromaBoost);
            // walk.step consumes pitch RNG internally — count ~1 draw approx via nextFloat usage
            v.pitchDraws += 2;
            next.octave = std::clamp (next.octave, reg.minOctave, reg.maxOctave);
            if (! next.chromatic)
                next.midiNote = Scale::toMidi (next.degree, next.octave);
        }

        // Pulse soft lock toward Foundation pitch class
        if (v.role == VoiceRole::Pulse && voices_[0].pitch.midiNote > 0)
        {
            const int fPc = ((voices_[0].pitch.midiNote % 12) + 12) % 12;
            int deg = 0, oct = 0;
            Scale::fromMidi (next.midiNote, deg, oct);
            if (m < 0.55f)
            {
                const int prefer = Scale::toMidi (0, std::clamp (oct, reg.minOctave, reg.maxOctave));
                // bias toward root/fifth near foundation class
                if (std::abs (((prefer % 12) + 12) % 12 - fPc) > 1
                    && std::abs (((Scale::toMidi (3, oct) % 12) + 12) % 12 - fPc) <= 1)
                    next.midiNote = Scale::toMidi (3, std::clamp (oct, reg.minOctave, reg.maxOctave));
            }
        }

        next.midiNote = std::clamp (next.midiNote, reg.minMidi, reg.maxMidi);
        if (next.midiNote == v.pitch.midiNote)
            return;
        v.pitch = next;
        v.memory.push (v.pitch.midiNote);
        v.pitchChangedThisBar = true;
        if (v.role == VoiceRole::Foundation)
            foundationPitchChangedSlot_ = lastSlotIndex_;
    }

    void applyLiveParamResponse (VoiceState& v, int bar) noexcept
    {
        if (! paramsDirty_)
            return;
        const float prevD = pendingPrevDensity_;
        const float prevM = pendingPrevMutation_;
        const float em = roleEffectiveMutation (v.role, params_.mutation);
        const float prevEm = roleEffectiveMutation (v.role, prevM);
        v.rhythm.respondToLiveParams (bar, prevD, prevM);
        v.phrases.respondToLiveParams (bar, prevEm);
        const int maxDelay = std::max (1, static_cast<int> (std::lround (1.0 + 3.0 * (1.0 - static_cast<double> (em)))));
        v.nextPitchEvalBar = std::min (v.nextPitchEvalBar, bar + maxDelay);
    }

    int chooseVelocity (VoiceState& v, std::int64_t absoluteSlot, bool afterRest) noexcept
    {
        const int bias = RhythmEngine::accentBiasForSlot (absoluteSlot, afterRest);
        const float u = v.velocityRng.nextFloat();
        int base = 80;
        if (v.role == VoiceRole::Accent)
            base = 88;
        if (v.role == VoiceRole::Foundation)
            base = 78;
        int vel = base + bias + static_cast<int> (std::round (4.0f * (u - 0.5f)));
        return std::clamp (vel, 64, 96);
    }

    EnsembleSnapshot makeSnapshot (std::int64_t /*absoluteSlot*/) const noexcept
    {
        EnsembleSnapshot s;
        s.density = params_.density;
        s.mutation = params_.mutation;
        s.activeCount = tracker_.activeCount();
        s.foundationSounding = voices_[0].sounding;
        s.pulseSounding = voices_[1].sounding;
        s.foundationPitch = voices_[0].pitch.midiNote;
        s.onsetsThisBar = onsetsThisBar_;
        s.onsetBudget = onsetBudgetPerBar (params_.density);
        s.foundationPitchChangedRecently =
            (lastSlotIndex_ - foundationPitchChangedSlot_) >= 0
            && (lastSlotIndex_ - foundationPitchChangedSlot_) <= 16;
        s.pulseGestureEndedRecently =
            (lastSlotIndex_ - pulseGestureEndSlot_) >= 0
            && (lastSlotIndex_ - pulseGestureEndSlot_) <= 8;

        int recent = 0;
        for (int i = 0; i < kCongestionWindowSlots; ++i)
        {
            const int idx = (recentOnsetCursor_ - 1 - i + static_cast<int> (recentOnsets_.size()))
                            % static_cast<int> (recentOnsets_.size());
            recent += recentOnsets_[static_cast<size_t> (idx)];
        }
        s.recentOnsetCount = recent;

        int since = kGapWindowSlots;
        for (int i = 0; i < kGapWindowSlots; ++i)
        {
            const int idx = (recentOnsetCursor_ - 1 - i + static_cast<int> (recentOnsets_.size()))
                            % static_cast<int> (recentOnsets_.size());
            if (recentOnsets_[static_cast<size_t> (idx)] > 0)
            {
                since = i;
                break;
            }
        }
        s.slotsSinceLastOnset = since;

        for (int n = 0; n < 128; ++n)
            s.pitchActive[static_cast<size_t> (n)] = tracker_.isActive (kMidiChannel, n);
        return s;
    }

    float interactionGate (VoiceRole role, const EnsembleSnapshot& snap, float beatsSilent,
                           InteractionReason& reasonOut) noexcept
    {
        reasonOut = InteractionReason::Normal;
        float g = rolePresence (role, snap.density) * roleExpressionMult (role);
        if (! hungerPaused_)
            g *= roleHungerMult (role, beatsSilent, snap.density);
        g *= collapseRoleMult (role);

        // Congestion: suppress decorative voices (hunger can still push through moderately)
        if (role == VoiceRole::Wanderer || role == VoiceRole::Accent)
        {
            if (snap.recentOnsetCount >= 3)
            {
                g *= (role == VoiceRole::Accent ? 0.22f : 0.40f);
                reasonOut = InteractionReason::CongestionSuppress;
            }
            else if (snap.slotsSinceLastOnset >= 6)
            {
                g *= (role == VoiceRole::Accent ? 1.6f : 1.35f);
                reasonOut = InteractionReason::GapFill;
            }
        }

        // Call / response windows
        if (role == VoiceRole::Wanderer && snap.foundationPitchChangedRecently)
        {
            g *= 1.45f;
            if (reasonOut == InteractionReason::Normal)
                reasonOut = InteractionReason::CallResponse;
        }
        if (role == VoiceRole::Accent && snap.pulseGestureEndedRecently)
        {
            g *= 1.9f;
            if (reasonOut == InteractionReason::Normal || reasonOut == InteractionReason::GapFill)
                reasonOut = InteractionReason::CallResponse;
        }

        // Accent remains punctuation — soft cap (was 0.12; that erased hunger)
        if (role == VoiceRole::Accent)
            g = std::min (g, 0.55f);

        return std::clamp (g, 0.0f, 1.35f);
    }

    /** Collapse phase multipliers (Accent dies first; Foundation lasts into residue). */
    float collapseRoleMult (VoiceRole role) const noexcept
    {
        if (collapsePhase_ <= 0)
            return 1.0f;
        // 1 Destabilize, 2 Thin, 3 Fragment, 4 Residue
        static constexpr float kTable[4][4] = {
            // Accent, Wanderer, Pulse, Foundation  (indexed by role enum order F,P,W,A)
            // Destabilize
            { 1.00f, 0.95f, 0.85f, 0.35f },
            // Thin
            { 0.90f, 0.65f, 0.40f, 0.05f },
            // Fragment
            { 0.55f, 0.30f, 0.12f, 0.00f },
            // Residue
            { 0.22f, 0.04f, 0.00f, 0.00f },
        };
        const int phase = std::clamp (collapsePhase_ - 1, 0, 3);
        const int ri = static_cast<int> (role);
        return kTable[phase][ri];
    }

    /** Non-consuming hunger probe when DNA is silent but pressure is high. */
    bool hungerAllowsProbe (VoiceRole role, std::int64_t absoluteSlot, float beatsSilent,
                            float density) const noexcept
    {
        if (hungerPaused_)
            return false;
        if (role != VoiceRole::Wanderer && role != VoiceRole::Accent)
            return false;

        const float need = (role == VoiceRole::Accent)
                               ? (28.0f + 36.0f * (1.0f - density))  // ~28 at dens1 … ~64 at dens0
                               : 10.0f;
        if (beatsSilent < need)
            return false;

        // Probabilistic — not a forced schedule. Rate rises with excess silence.
        const float excess = beatsSilent - need;
        float rate = (role == VoiceRole::Accent)
                         ? std::clamp (0.08f + 0.012f * excess, 0.08f, 0.35f)
                         : std::clamp (0.12f + 0.03f * excess, 0.12f, 0.45f);
        rate *= rolePresence (role, density);
        if (rate <= 1.0e-6f)
            return false;

        uint64_t z = masterSeed_ ^ (static_cast<uint64_t> (absoluteSlot) * 0xC2B2AE3D27D4EB4Full);
        z ^= static_cast<uint64_t> (role) * 0x9E3779B97F4A7C15ull;
        z ^= 0xA5A5A5A5DEADBEEFull; // hunger probe mix
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        const float u = static_cast<float> ((z >> 40) * (1.0 / (1ull << 24)));
        return u < rate;
    }

    EventIntent propose (VoiceState& v, std::int64_t absoluteSlot, double slotPpq,
                         const EnsembleSnapshot& snap) noexcept
    {
        EventIntent intent;
        intent.role = v.role;
        intent.active = false;
        intent.pitch = v.pitch.midiNote;
        intent.reason = InteractionReason::RoleRest;

        InteractionReason ix = InteractionReason::Normal;
        const float gate = interactionGate (v.role, snap, v.beatsSinceContribution, ix);

        RhythmCell cell = v.rhythm.dna().cellForAbsoluteSlot (absoluteSlot);
        bool hungerProbe = false;
        if (cell != RhythmCell::Onset)
        {
            if (hungerAllowsProbe (v.role, absoluteSlot, v.beatsSinceContribution, snap.density))
            {
                hungerProbe = true;
                cell = RhythmCell::Onset;
            }
            else
            {
                if (cell == RhythmCell::Rest)
                    v.lastSlotWasRest = true;
                else
                    v.lastSlotWasRest = false;
                return intent;
            }
        }

        // Wanderer consecutive onset cap
        if (v.role == VoiceRole::Wanderer && v.consecutiveOnsets >= 4)
        {
            v.consecutiveOnsets = 0;
            v.lastSlotWasRest = true;
            return intent;
        }

        const uint64_t roleTag = hashTag (voiceRoleName (v.role));
        if (! RhythmEngine::shouldExpressOnset (masterSeed_, absoluteSlot, snap.density, roleTag, gate))
        {
            if (ix == InteractionReason::CongestionSuppress)
                intent.reason = InteractionReason::CongestionSuppress;
            v.lastSlotWasRest = true;
            return intent;
        }

        // Pulse: avoid Foundation downbeat pile-ups (non-consuming hash — not velocity RNG)
        if (v.role == VoiceRole::Pulse)
        {
            const int slotInBar = static_cast<int> (absoluteSlot % RhythmDNA::kStepsPerBar);
            if (slotInBar % 4 == 0 && snap.foundationSounding)
            {
                uint64_t z = masterSeed_ ^ (static_cast<uint64_t> (absoluteSlot) * 0x85EBCA77C2B2AE63ull);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                const float u = static_cast<float> ((z >> 40) * (1.0 / (1ull << 24)));
                if (u < 0.55f)
                {
                    v.lastSlotWasRest = true;
                    return intent;
                }
            }
        }

        const int n = v.rhythm.dna().lengthCells();
        const int local = n > 0 ? static_cast<int> (((absoluteSlot % n) + n) % n) : 0;
        double dur = hungerProbe ? ((v.role == VoiceRole::Accent) ? 0.25 : 0.5)
                                 : v.rhythm.durationBeatsAt (local);
        if (! hungerProbe)
            dur = std::max (kSlotBeats, dur);
        if (v.role == VoiceRole::Accent)
            dur = std::min (dur, 0.5);
        if (collapsePhase_ >= 3) // Fragment / Residue — shorter gestures
            dur = std::min (dur, collapsePhase_ >= 4 ? 0.5 : 1.0);
        else if (collapsePhase_ == 2)
            dur = std::min (dur, 2.0);

        intent.active = true;
        intent.pitch = v.pitch.midiNote;
        intent.velocity = chooseVelocity (v, absoluteSlot, v.lastSlotWasRest);
        intent.durationBeats = dur;
        intent.reason = (ix == InteractionReason::CongestionSuppress) ? InteractionReason::Normal : ix;
        if (hungerProbe && intent.reason == InteractionReason::Normal)
            intent.reason = InteractionReason::GapFill;
        return intent;
    }

    int resolvePitchCollision (VoiceRole role, int desired, const EnsembleSnapshot& snap,
                               InteractionReason& reasonInOut) noexcept
    {
        const auto reg = registerFor (role);
        if (desired < 0 || desired > 127)
            desired = reg.startMidi;

        auto free = [&] (int n) {
            return n >= reg.minMidi && n <= reg.maxMidi && ! snap.pitchActive[static_cast<size_t> (n)];
        };

        if (free (desired))
            return desired;

        ++collisionStats_.attemptedSamePitch;

        // Nearest scale tones
        int deg = 0, oct = 0;
        Scale::fromMidi (desired, deg, oct);
        for (int delta = 1; delta <= 4; ++delta)
        {
            for (int sign : { +1, -1 })
            {
                int d2 = deg + sign * delta;
                int o2 = oct;
                while (d2 < 0)
                {
                    d2 += Scale::kNumDegrees;
                    --o2;
                }
                while (d2 >= Scale::kNumDegrees)
                {
                    d2 -= Scale::kNumDegrees;
                    ++o2;
                }
                const int cand = Scale::toMidi (d2, o2);
                if (free (cand))
                {
                    ++collisionStats_.shifted;
                    reasonInOut = InteractionReason::CollisionShift;
                    return cand;
                }
            }
        }

        // Octave displacement
        for (int octShift : { -1, +1, -2, +2 })
        {
            const int cand = desired + 12 * octShift;
            if (free (cand))
            {
                ++collisionStats_.shifted;
                reasonInOut = InteractionReason::CollisionShift;
                return cand;
            }
        }

        ++collisionStats_.suppressed;
        reasonInOut = InteractionReason::CollisionSuppress;
        return -1;
    }

    void commitIntent (VoiceState& v, EventIntent intent, std::int64_t absoluteSlot, double slotPpq,
                       EnsembleSnapshot& snap) noexcept
    {
        if (silenceActive_)
            return;

        if (! intent.active)
        {
            if (v.role == VoiceRole::Wanderer)
                v.consecutiveOnsets = 0;
            return;
        }

        // Budget: Foundation rarely suppressed; Accent/Wanderer first
        if (snap.onsetsThisBar >= snap.onsetBudget && v.role != VoiceRole::Foundation)
        {
            intent.reason = InteractionReason::BudgetSuppress;
            return;
        }

        InteractionReason reason = intent.reason;
        int pitch = resolvePitchCollision (v.role, intent.pitch, snap, reason);
        if (pitch < 0)
            return;

        // Preempt if higher priority needs a pitch owned by lower (should be rare after free check)
        if (tracker_.isActive (kMidiChannel, pitch))
        {
            const int owner = tracker_.ownerRole (kMidiChannel, pitch);
            if (owner >= 0 && voicePriority (v.role) > voicePriority (static_cast<VoiceRole> (owner)))
            {
                emitOff (slotPpq, pitch, static_cast<VoiceRole> (owner), InteractionReason::CollisionShift);
                voices_[owner].sounding = false;
            }
            else if (owner >= 0 && owner != static_cast<int> (v.role))
            {
                ++collisionStats_.suppressed;
                return;
            }
        }

        if (v.sounding)
            emitOff (slotPpq, v.soundingNote, v.role, InteractionReason::Normal);

        v.lastVelocity = intent.velocity;
        emitOn (slotPpq, pitch, intent.velocity, v.role, reason, slotPpq + intent.durationBeats);
        v.sounding = true;
        v.soundingNote = pitch;
        v.noteOffPpq = slotPpq + intent.durationBeats;
        v.lastSlotWasRest = false;
        v.beatsSinceContribution = 0.0f;
        if (v.role == VoiceRole::Wanderer)
            ++v.consecutiveOnsets;
        else
            v.consecutiveOnsets = 0;

        snap.pitchActive[static_cast<size_t> (pitch)] = true;
        ++snap.onsetsThisBar;
        ++onsetsThisBar_;

        if (v.role == VoiceRole::Pulse)
            pulseGestureEndSlot_ = absoluteSlot + static_cast<std::int64_t> (intent.durationBeats / kSlotBeats);
    }

    void recordRecentOnset (int count) noexcept
    {
        recentOnsets_[static_cast<size_t> (recentOnsetCursor_)] = static_cast<uint8_t> (count);
        recentOnsetCursor_ = (recentOnsetCursor_ + 1) % static_cast<int> (recentOnsets_.size());
    }

    void onSlot (std::int64_t absoluteSlot, double slotPpq) noexcept
    {
        lastSlotIndex_ = absoluteSlot;
        const double beatsPerBar = std::max (1.0e-9, clock_.beatsPerBar());
        const int bar = static_cast<int> (std::floor (slotPpq / beatsPerBar));

        if (bar != lastBudgetBar_)
        {
            onsetsThisBar_ = 0;
            lastBudgetBar_ = bar;
        }

        // Phase 0: expire notes + accumulate role silence (hunger)
        for (auto& v : voices_)
        {
            if (! hungerPaused_)
                v.beatsSinceContribution += static_cast<float> (kSlotBeats);
            if (v.sounding && slotPpq + 1.0e-9 >= v.noteOffPpq)
            {
                emitOff (slotPpq, v.soundingNote, v.role, InteractionReason::Normal);
                if (v.role == VoiceRole::Pulse)
                    pulseGestureEndSlot_ = absoluteSlot;
                v.sounding = false;
            }
        }

        // Bar / param updates per voice (independent streams)
        for (auto& v : voices_)
        {
            if (bar != v.lastRhythmBar)
            {
                if (! evolutionLocked_)
                {
                    applyLiveParamResponse (v, bar);
                    v.phrases.onBar (bar);
                    v.rhythm.onBar (bar);
                }
                v.lastRhythmBar = bar;
                v.pitchChangedThisBar = false;
            }
            else if (paramsDirty_ && ! evolutionLocked_)
            {
                applyLiveParamResponse (v, bar);
            }

            const double barStart = static_cast<double> (bar) * beatsPerBar;
            const bool atBarStart = std::abs (slotPpq - barStart) < 1.0e-9;
            if (! evolutionLocked_ && atBarStart && bar >= v.nextPitchEvalBar)
            {
                maybeChangePitch (v);
                schedulePitchEval (v, bar);
            }
        }
        if (paramsDirty_)
        {
            paramsDirty_ = false;
            pendingPrevDensity_ = params_.density;
            pendingPrevMutation_ = params_.mutation;
        }

        // Phase 1: snapshot then propose (order of propose must not affect other voices' RNGs)
        const EnsembleSnapshot snap0 = makeSnapshot (absoluteSlot);
        std::array<EventIntent, kNumVoices> intents {};
        for (int i = 0; i < kNumVoices; ++i)
            intents[static_cast<size_t> (i)] = propose (voices_[static_cast<size_t> (i)], absoluteSlot, slotPpq, snap0);

        // Phase 2: arbitrate in priority order (Foundation first)
        EnsembleSnapshot snap = snap0;
        const int onsetsBefore = onsetsThisBar_;
        static constexpr VoiceRole kCommitOrder[4] = {
            VoiceRole::Foundation, VoiceRole::Pulse, VoiceRole::Wanderer, VoiceRole::Accent
        };
        for (VoiceRole role : kCommitOrder)
        {
            auto& intent = intents[static_cast<size_t> (role)];
            commitIntent (voices_[static_cast<int> (role)], intent, absoluteSlot, slotPpq, snap);
        }

        recordRecentOnset (onsetsThisBar_ > onsetsBefore ? 1 : 0);
    }

    void flushEmittedNotes (double ppq) noexcept
    {
        std::vector<MidiTraceEvent> offs;
        emittedTracker_.panicTo (offs, ppq, 0);
        if (! emitOutput_)
            return;
        for (auto& e : offs)
        {
            if (capture_)
                captured_.push_back (e);
            pending_.push_back (e);
        }
    }

    void emitOn (double ppq, int note, int velocity, VoiceRole role, InteractionReason reason, double endPpq) noexcept
    {
        const int voice = static_cast<int> (role);
        // Composition ownership always advances (full ensemble)
        if (tracker_.isActive (kMidiChannel, note))
        {
            const int owner = tracker_.ownerRole (kMidiChannel, note);
            if (owner >= 0)
                emitOff (ppq, note, static_cast<VoiceRole> (owner), InteractionReason::CollisionShift);
            else
                tracker_.noteOff (kMidiChannel, note);
        }
        tracker_.noteOn (kMidiChannel, note, voice, ppq, endPpq);

        MidiTraceEvent e;
        e.ppq = ppq;
        e.channel = kMidiChannel;
        e.note = note;
        e.velocity = velocity;
        e.voice = voice;
        e.reason = static_cast<int> (reason);
        e.kind = MidiMsgKind::NoteOn;

        if (capture_)
            ensembleCaptured_.push_back (e);

        if (! emitOutput_)
            return;

        if (! outputRolePasses (outputRole_, role))
            return;

        emittedTracker_.noteOn (kMidiChannel, note, voice, ppq, endPpq);
        if (capture_)
            captured_.push_back (e);
        pending_.push_back (e);
    }

    void emitOff (double ppq, int note, VoiceRole role, InteractionReason reason) noexcept
    {
        const int voice = static_cast<int> (role);
        if (! tracker_.noteOffIfOwner (kMidiChannel, note, voice))
        {
            if (tracker_.isActive (kMidiChannel, note))
            {
                const int owner = tracker_.ownerRole (kMidiChannel, note);
                if (owner >= 0 && owner != voice)
                    return;
                tracker_.noteOff (kMidiChannel, note);
            }
            else
            {
                // Still may need to clear emitted if stale
            }
        }

        MidiTraceEvent e;
        e.ppq = ppq;
        e.channel = kMidiChannel;
        e.note = note;
        e.velocity = 0;
        e.voice = voice;
        e.reason = static_cast<int> (reason);
        e.kind = MidiMsgKind::NoteOff;

        if (capture_)
            ensembleCaptured_.push_back (e);

        const bool wasEmitted = [&]() noexcept {
            if (! emittedTracker_.isActive (kMidiChannel, note))
                return false;
            const int er = emittedTracker_.ownerRole (kMidiChannel, note);
            if (er >= 0 && er != voice)
                return false;
            emittedTracker_.noteOff (kMidiChannel, note);
            return true;
        }();

        if (! emitOutput_)
            return;
        if (! wasEmitted)
            return;

        if (capture_)
            captured_.push_back (e);
        pending_.push_back (e);
    }

    uint64_t masterSeed_ = 1001;
    ConductorParams params_{};
    MusicalClock clock_{};
    MusicalMemory memoryEnsemble_{};
    std::array<VoiceState, kNumVoices> voices_{};
    DeterministicRNG arbiterRng_{};
    MidiNoteTracker tracker_{};
    MidiNoteTracker emittedTracker_{};
    CollisionStats collisionStats_{};
    OutputRole outputRole_ = OutputRole::Ensemble;

    std::array<uint8_t, 16> recentOnsets_ {};
    int recentOnsetCursor_ = 0;
    int onsetsThisBar_ = 0;
    int lastBudgetBar_ = -1;
    std::int64_t lastSlotIndex_ = 0;
    std::int64_t foundationPitchChangedSlot_ = -100000;
    std::int64_t pulseGestureEndSlot_ = -100000;

    bool paramsDirty_ = false;
    float pendingPrevDensity_ = 0.45f;
    float pendingPrevMutation_ = 0.35f;
    double lastProcessedPpq_ = 0.0;
    bool capture_ = false;
    bool emitOutput_ = true;
    bool evolutionLocked_ = false;
    bool silenceActive_ = false;
    bool hungerPaused_ = false;
    int collapsePhase_ = 0;
    std::vector<MidiTraceEvent> captured_;
    std::vector<MidiTraceEvent> ensembleCaptured_;
    std::vector<MidiTraceEvent> pending_;
};

} // namespace pfl::generative

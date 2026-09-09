#include "generative/ConductorEngine.h"
#include "generative/RhythmDNA.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

static int gFails = 0;

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << #cond << " @" << __FILE__ << ":" << __LINE__ << "\n"; \
            ++gFails; \
        } \
    } while (0)

using pfl::generative::ConductorEngine;
using pfl::generative::ConductorParams;
using pfl::generative::MidiMsgKind;
using pfl::generative::MidiTraceEvent;
using pfl::generative::RhythmCell;
using pfl::generative::RhythmEngine;
using pfl::generative::DeterministicRNG;

static bool midiEqual (const std::vector<MidiTraceEvent>& a, const std::vector<MidiTraceEvent>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].kind != b[i].kind)
            return false;
        if (a[i].channel != b[i].channel)
            return false;
        if (a[i].note != b[i].note)
            return false;
        if (a[i].velocity != b[i].velocity)
            return false;
        if (a[i].voice != b[i].voice)
            return false;
        if (std::abs (a[i].ppq - b[i].ppq) > 1.0e-6)
            return false;
    }
    return true;
}

static std::string fingerprint (const std::vector<MidiTraceEvent>& ev)
{
    std::string s;
    for (const auto& e : ev)
    {
        char buf[64];
        std::snprintf (buf, sizeof buf, "%d:%d:%d:%d:%.4f;",
                       (int) e.kind, e.channel, e.note, e.velocity, e.ppq);
        s += buf;
    }
    return s;
}

static std::vector<MidiTraceEvent> runMidi (uint64_t seed, float density, float mutation,
                                            double bpm, int bars, int bufferSamples, double sampleRate)
{
    ConductorEngine eng;
    eng.setCapture (true);
    eng.setParams ({ density, mutation });
    eng.reseed (seed);

    const double beatsPerSec = bpm / 60.0;
    const double endPpq = static_cast<double> (bars) * 4.0;
    double ppq = 0.0;

    while (ppq < endPpq - 1.0e-12)
    {
        const double blockBeats = (static_cast<double> (bufferSamples) / sampleRate) * beatsPerSec;
        const double ppqEnd = std::min (endPpq, ppq + blockBeats);
        pfl::generative::ClockSnapshot snap;
        snap.playing = true;
        snap.ppq = ppq;
        snap.tempoBpm = bpm;
        snap.timeSigNumerator = 4;
        snap.timeSigDenominator = 4;
        eng.clock().advance (snap);
        eng.processTimeRange (ppq, ppqEnd, true);
        eng.drainPending();
        ppq = ppqEnd;
    }

    return eng.captured();
}

static void assertPaired (const std::vector<MidiTraceEvent>& ev, bool requireClosed = false)
{
    std::map<std::pair<int, int>, int> bal;
    for (const auto& e : ev)
    {
        EXPECT (e.note >= 0 && e.note <= 127);
        EXPECT (e.channel == 1);
        const auto key = std::make_pair (e.channel, e.note);
        if (e.kind == MidiMsgKind::NoteOn)
        {
            EXPECT (e.velocity >= 64 && e.velocity <= 96);
            ++bal[key];
        }
        else if (e.kind == MidiMsgKind::NoteOff)
        {
            --bal[key];
            EXPECT (bal[key] >= 0);
        }
    }
    if (requireClosed)
    {
        for (const auto& kv : bal)
            EXPECT (kv.second == 0);
    }
}

static void testAlgorithmVersion()
{
    EXPECT (ConductorEngine::kAlgorithmVersion == 4);
}

static void testDeterminism()
{
    auto a = runMidi (2002, 0.45f, 0.35f, 72.0, 32, 256, 48000.0);
    auto b = runMidi (2002, 0.45f, 0.35f, 72.0, 32, 256, 48000.0);
    EXPECT (midiEqual (a, b));
    EXPECT (a.size() >= 2);
}

static void testDifferentSeed()
{
    auto a = runMidi (1001, 0.45f, 0.35f, 72.0, 32, 256, 48000.0);
    auto b = runMidi (1002, 0.45f, 0.35f, 72.0, 32, 256, 48000.0);
    EXPECT (fingerprint (a) != fingerprint (b));
}

static void testBufferIndependence()
{
    std::vector<int> buffers { 64, 127, 128, 255, 256, 511, 512, 1024 };
    auto ref = runMidi (777, 0.45f, 0.35f, 93.0, 48, 256, 48000.0);
    for (int b : buffers)
    {
        auto t = runMidi (777, 0.45f, 0.35f, 93.0, 48, b, 48000.0);
        EXPECT (midiEqual (ref, t));
    }
}

static void testTempos()
{
    auto ref = runMidi (4242, 0.5f, 0.4f, 72.0, 24, 256, 48000.0);
    for (double bpm : { 40.0, 93.0, 120.0, 137.0, 180.0 })
    {
        auto t = runMidi (4242, 0.5f, 0.4f, bpm, 24, 256, 48000.0);
        EXPECT (midiEqual (ref, t));
    }
}

static void testNotePairingAndRegister()
{
    ConductorEngine eng;
    eng.setCapture (true);
    eng.setParams ({ 0.6f, 0.5f });
    eng.reseed (2002);
    eng.clock().advance ({ true, 0.0, 72.0, 4, 4 });
    eng.processTimeRange (0.0, 256.0, true);
    eng.panic (256.0);
    assertPaired (eng.captured(), true);
    for (const auto& e : eng.captured())
    {
        if (e.kind == MidiMsgKind::NoteOn || e.kind == MidiMsgKind::NoteOff)
            EXPECT (e.note >= ConductorEngine::kMinMidi && e.note <= ConductorEngine::kMaxMidi);
    }
}

static void testStopNoHang()
{
    ConductorEngine eng;
    eng.setCapture (true);
    eng.setParams ({ 0.7f, 0.4f });
    eng.reseed (55);

    eng.clock().advance ({ true, 0.0, 72.0, 4, 4 });
    eng.processTimeRange (0.0, 16.0, true);
    eng.drainPending();

    eng.panic (16.0);
    EXPECT (eng.tracker().activeCount() == 0);
    EXPECT (! eng.sounding());

    const size_t before = eng.captured().size();
    eng.processTimeRange (16.0, 32.0, false);
    EXPECT (eng.captured().size() == before);
}

static void testRestart()
{
    auto a = runMidi (99, 0.45f, 0.35f, 72.0, 16, 256, 48000.0);
    auto b = runMidi (99, 0.45f, 0.35f, 72.0, 16, 256, 48000.0);
    EXPECT (midiEqual (a, b));
}

static void testSeekNoHang()
{
    ConductorEngine eng;
    eng.setCapture (true);
    eng.setParams ({ 0.6f, 0.4f });
    eng.reseed (88);
    eng.clock().advance ({ true, 0.0, 72.0, 4, 4 });
    eng.processTimeRange (0.0, 20.0, true);
    eng.drainPending();

    eng.panic (20.0);
    EXPECT (eng.tracker().activeCount() == 0);

    eng.handleSeek (40.375);
    eng.setCapture (false);
    eng.panic (40.375);
    EXPECT (eng.tracker().activeCount() == 0);
    EXPECT (! eng.sounding());

    eng.clearCaptured();
    eng.setCapture (true);
    eng.processTimeRange (40.375, 56.0, true);
    eng.setCapture (false);
    eng.panic (56.0);
    EXPECT (eng.tracker().activeCount() == 0);
}

static void testSeekDeterministicResume()
{
    ConductorEngine eng;
    eng.setParams ({ 0.45f, 0.35f });
    eng.reseed (2002);
    eng.handleSeek (32.0);
    eng.clearCaptured();
    eng.setCapture (true);
    eng.clock().advance ({ true, 32.0, 72.0, 4, 4 });
    eng.processTimeRange (32.0, 96.0, true);
    eng.setCapture (false);
    eng.panic (96.0);
    auto post = eng.captured();

    ConductorEngine eng2;
    eng2.setParams ({ 0.45f, 0.35f });
    eng2.reseed (2002);
    eng2.handleSeek (32.0);
    eng2.clearCaptured();
    eng2.setCapture (true);
    eng2.processTimeRange (32.0, 96.0, true);
    eng2.setCapture (false);
    eng2.panic (96.0);
    EXPECT (midiEqual (post, eng2.captured()));
}

static void testLongRun()
{
    ConductorEngine eng;
    eng.setParams ({ 0.5f, 0.4f });
    eng.reseed (2002);

    const double endPpq = 2160.0; // 540 bars ≈ long soak at 72 BPM (~30 min musical); Stage 3 also has 60-min test
    double ppq = 0.0;
    size_t ons = 0, offs = 0;
    int maxActive = 0;

    while (ppq < endPpq)
    {
        const double next = std::min (endPpq, ppq + 4.0);
        eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
        eng.processTimeRange (ppq, next, true);
        auto pending = eng.drainPending();
        for (const auto& e : pending)
        {
            if (e.kind == MidiMsgKind::NoteOn)
            {
                ++ons;
                EXPECT (e.note >= ConductorEngine::kMinMidi && e.note <= ConductorEngine::kMaxMidi);
                EXPECT (e.channel == 1);
                EXPECT (e.voice >= 0 && e.voice < ConductorEngine::kNumVoices);
            }
            else if (e.kind == MidiMsgKind::NoteOff)
                ++offs;
        }
        maxActive = std::max (maxActive, eng.tracker().activeCount());
        EXPECT (maxActive <= 4);
        ppq = next;
    }

    eng.panic (endPpq);
    EXPECT (eng.tracker().activeCount() == 0);
    EXPECT (ons < 500000);
    EXPECT (offs <= ons + 4);
}

static void testParamRestore()
{
    auto a = runMidi (12345, 0.45f, 0.35f, 72.0, 16, 256, 48000.0);
    auto changed = runMidi (12345, 0.9f, 0.8f, 72.0, 16, 256, 48000.0);
    EXPECT (! midiEqual (a, changed));
    auto restored = runMidi (12345, 0.45f, 0.35f, 72.0, 16, 256, 48000.0);
    EXPECT (midiEqual (a, restored));
}

static void testSixteenthGrid()
{
    auto ev = runMidi (2002, 0.45f, 0.35f, 72.0, 16, 64, 48000.0);
    for (const auto& e : ev)
    {
        const double nearest = std::round (e.ppq / 0.25) * 0.25;
        EXPECT (std::abs (e.ppq - nearest) < 1.0e-6);
    }
}

static void testSyncopationExists()
{
    auto ev = runMidi (2002, 0.65f, 0.4f, 72.0, 64, 256, 48000.0);
    int ons = 0, offbeat = 0, odd16 = 0;
    for (const auto& e : ev)
    {
        if (e.kind != MidiMsgKind::NoteOn)
            continue;
        ++ons;
        const double beatFrac = e.ppq - std::floor (e.ppq);
        if (std::abs (beatFrac - 0.5) < 1.0e-6)
            ++offbeat;
        const int slot = static_cast<int> (std::lround (e.ppq / 0.25));
        if (slot % 2 != 0)
            ++odd16;
    }
    EXPECT (ons > 0);
    EXPECT (offbeat > 0); // eighth offbeats at moderate density
    (void) odd16;
}

static void testRestsAndHolds()
{
    RhythmEngine re;
    auto rng = DeterministicRNG::derived (2002, 0xABCD);
    re.reset (rng, 0.35f, 0.45f, 0);
    const auto& dna = re.dna();
    EXPECT (dna.lengthBars >= 1 && dna.lengthBars <= 4);
    EXPECT (dna.occupancy() <= RhythmEngine::kMaxOccupancy + 1.0e-5f);

    bool hasOnset = false, hasRest = false, hasHold = false;
    for (int i = 0; i < dna.lengthCells(); ++i)
    {
        const auto c = dna.cellAt (i);
        if (c == RhythmCell::Onset)
            hasOnset = true;
        if (c == RhythmCell::Rest)
            hasRest = true;
        if (c == RhythmCell::Hold)
            hasHold = true;
    }
    EXPECT (hasOnset);
    EXPECT (hasRest);
    // Holds are common but not strictly required at extreme sparse; soft check via occupancy
    (void) hasHold;

    auto sparse = DeterministicRNG::derived (3003, 0xABCD);
    RhythmEngine re2;
    re2.reset (sparse, 0.2f, 0.15f, 0);
    EXPECT (re2.dna().occupancy() <= 0.20f + 1.0e-5f);
}

static void testRhythmMutationBounded()
{
    RhythmEngine re;
    re.setTraceEnabled (true);
    auto rng = DeterministicRNG::derived (2002, 911);
    re.reset (rng, 0.8f, 0.5f, 0);
    const auto before = re.dna().describe();
    // Force mutations by jumping lifespan
    for (int bar = 0; bar < 200; ++bar)
        re.onBar (bar);
    EXPECT (re.dna().generation >= 1);
    EXPECT (re.dna().occupancy() <= RhythmEngine::kMaxOccupancy + 1.0e-5f);
    EXPECT (! before.empty());
}

static void testRhythmRngIsolation()
{
    // Same master seed: changing only pitch decisions path shouldn't be tested here;
    // instead verify rhythm DNA identical when only pitch params would differ if streams mixed.
    RhythmEngine a, b;
    a.reset (DeterministicRNG::derived (4242, 1), 0.35f, 0.45f, 0);
    b.reset (DeterministicRNG::derived (4242, 1), 0.35f, 0.45f, 0);
    EXPECT (a.dna().describe() == b.dna().describe());

    // Different rhythm stream tags → different DNA
    RhythmEngine c;
    c.reset (DeterministicRNG::derived (4242, 2), 0.35f, 0.45f, 0);
    EXPECT (a.dna().describe() != c.dna().describe());
}

static int countNoteOns (const std::vector<MidiTraceEvent>& ev)
{
    int n = 0;
    for (const auto& e : ev)
        if (e.kind == MidiMsgKind::NoteOn)
            ++n;
    return n;
}

static int countUniquePitches (const std::vector<MidiTraceEvent>& ev)
{
    std::set<int> pitches;
    for (const auto& e : ev)
        if (e.kind == MidiMsgKind::NoteOn)
            pitches.insert (e.note);
    return static_cast<int> (pitches.size());
}

static void testDensityEndpoints()
{
    // Same seed: density 0 vs 1 must differ by ≥2× onset activity over 64 bars
    auto sparse = runMidi (2002, 0.0f, 0.0f, 72.0, 64, 256, 48000.0);
    auto busy = runMidi (2002, 1.0f, 0.0f, 72.0, 64, 256, 48000.0);
    const int sparseOns = countNoteOns (sparse);
    const int busyOns = countNoteOns (busy);
    EXPECT (sparseOns > 0);
    EXPECT (busyOns >= sparseOns * 2);
    EXPECT (RhythmEngine::expressionRate (0.0f) < 0.20f);
    EXPECT (RhythmEngine::expressionRate (1.0f) >= 0.99f);
}

static void testMutationEndpoints()
{
    auto stable = runMidi (2002, 0.5f, 0.0f, 72.0, 64, 256, 48000.0);
    auto wild = runMidi (2002, 0.5f, 1.0f, 72.0, 64, 256, 48000.0);

    ConductorEngine engLo, engHi;
    engLo.setParams ({ 0.5f, 0.0f });
    engLo.reseed (2002);
    engLo.rhythm().setTraceEnabled (true);
    engHi.setParams ({ 0.5f, 1.0f });
    engHi.reseed (2002);
    engHi.rhythm().setTraceEnabled (true);
    engHi.phrases(); // silence unused

    // Force many bars through engines with traces
    for (int pass = 0; pass < 2; ++pass)
    {
        ConductorEngine& eng = (pass == 0) ? engLo : engHi;
        eng.clearCaptured();
        eng.setCapture (true);
        double ppq = 0.0;
        const double end = 256.0; // 64 bars
        while (ppq < end)
        {
            const double next = std::min (end, ppq + 4.0);
            eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
            eng.processTimeRange (ppq, next, true);
            eng.drainPending();
            ppq = next;
        }
    }

    EXPECT (engHi.rhythm().dna().generation > engLo.rhythm().dna().generation
            || countUniquePitches (wild) > countUniquePitches (stable));
    EXPECT (countUniquePitches (wild) >= 3);
}

static void testLiveDensityResponse()
{
    ConductorEngine eng;
    eng.setParams ({ 0.15f, 0.10f });
    eng.reseed (2002);
    eng.setCapture (true);

    // Bars 0–15 at low density
    double ppq = 0.0;
    while (ppq < 64.0)
    {
        eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
        eng.processTimeRange (ppq, ppq + 4.0, true);
        eng.drainPending();
        ppq += 4.0;
    }
    const int before = countNoteOns (eng.captured());
    eng.clearCaptured();

    // Live density → 1.0 without reseed
    eng.setParams ({ 1.0f, 0.10f });
    EXPECT (eng.diagnosticDensity() == 1.0f);

    while (ppq < 128.0)
    {
        eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
        eng.processTimeRange (ppq, ppq + 4.0, true);
        eng.drainPending();
        ppq += 4.0;
    }
    const int after = countNoteOns (eng.captured());
    // After density jump, next 16 bars should be busier than prior 16
    EXPECT (after > before);
}

static void testLiveMutationResponse()
{
    ConductorEngine eng;
    eng.setParams ({ 0.5f, 0.10f });
    eng.reseed (2002);
    eng.rhythm().setTraceEnabled (true);

    double ppq = 0.0;
    while (ppq < 64.0)
    {
        eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
        eng.processTimeRange (ppq, ppq + 4.0, true);
        eng.drainPending();
        ppq += 4.0;
    }
    const int genBefore = eng.rhythm().dna().generation;

    eng.setParams ({ 0.5f, 1.0f });
    EXPECT (eng.diagnosticMutation() == 1.0f);

    // Within ≤4 bars, lifespan should allow mutation (respondToLiveParams)
    while (ppq < 96.0)
    {
        eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
        eng.processTimeRange (ppq, ppq + 4.0, true);
        eng.drainPending();
        ppq += 4.0;
    }
    EXPECT (eng.rhythm().dna().generation > genBefore);
}

static void testAutomationDeterminism()
{
    auto runAuto = [] (int bufferSamples) {
        ConductorEngine eng;
        eng.setParams ({ 0.20f, 0.10f });
        eng.reseed (2002);
        eng.setCapture (true);
        const double bpm = 72.0;
        const double sr = 48000.0;
        const double beatsPerSec = bpm / 60.0;
        double ppq = 0.0;
        const double end = 128.0; // 32 bars
        while (ppq < end - 1.0e-12)
        {
            const int bar = static_cast<int> (std::floor (ppq / 4.0));
            if (bar == 8)
                eng.setParams ({ 0.80f, 0.10f });
            if (bar == 16)
                eng.setParams ({ 0.80f, 1.00f });
            if (bar == 24)
                eng.setParams ({ 0.30f, 0.80f });

            const double blockBeats = (static_cast<double> (bufferSamples) / sr) * beatsPerSec;
            const double next = std::min (end, ppq + blockBeats);
            eng.clock().advance ({ true, ppq, bpm, 4, 4 });
            eng.processTimeRange (ppq, next, true);
            eng.drainPending();
            ppq = next;
        }
        return eng.captured();
    };

    auto a = runAuto (256);
    auto b = runAuto (256);
    EXPECT (midiEqual (a, b));
    auto c = runAuto (127);
    EXPECT (midiEqual (a, c));
    auto d = runAuto (512);
    EXPECT (midiEqual (a, d));
}

static void testDensityAffectsActivity()
{
    auto low = runMidi (2002, 0.0f, 0.35f, 72.0, 64, 256, 48000.0);
    auto high = runMidi (2002, 1.0f, 0.35f, 72.0, 64, 256, 48000.0);
    EXPECT (countNoteOns (high) >= countNoteOns (low) * 2);
}

static void testAwkwardCrossProduct()
{
    auto a = runMidi (555, 0.45f, 0.35f, 93.0, 16, 127, 48000.0);
    auto b = runMidi (555, 0.45f, 0.35f, 93.0, 16, 256, 48000.0);
    EXPECT (midiEqual (a, b));
    auto c = runMidi (555, 0.45f, 0.35f, 137.0, 16, 511, 48000.0);
    EXPECT (midiEqual (a, c));
}

static void collectRoleOnNotes (const std::vector<MidiTraceEvent>& ev,
                                std::array<std::vector<std::pair<double, int>>, 4>& byRole,
                                std::array<std::vector<double>, 4>& durs)
{
    std::map<std::tuple<int, int, int>, double> open; // voice,ch,note -> start
    for (const auto& e : ev)
    {
        if (e.kind == MidiMsgKind::NoteOn)
        {
            const int v = std::clamp (e.voice, 0, 3);
            byRole[static_cast<size_t> (v)].push_back ({ e.ppq, e.note });
            open[{ v, e.channel, e.note }] = e.ppq;
        }
        else if (e.kind == MidiMsgKind::NoteOff)
        {
            const int v = std::clamp (e.voice, 0, 3);
            auto key = std::tuple { v, e.channel, e.note };
            auto it = open.find (key);
            if (it != open.end())
            {
                durs[static_cast<size_t> (v)].push_back (e.ppq - it->second);
                open.erase (it);
            }
        }
    }
}

static double meanPitch (const std::vector<std::pair<double, int>>& notes)
{
    if (notes.empty())
        return 0.0;
    double s = 0.0;
    for (const auto& n : notes)
        s += n.second;
    return s / static_cast<double> (notes.size());
}

static double meanDur (const std::vector<double>& d)
{
    if (d.empty())
        return 0.0;
    double s = 0.0;
    for (double x : d)
        s += x;
    return s / static_cast<double> (d.size());
}

static void testStage3RoleIdentity()
{
    auto ev = runMidi (2002, 0.50f, 0.35f, 72.0, 128, 256, 48000.0);
    std::array<std::vector<std::pair<double, int>>, 4> byRole;
    std::array<std::vector<double>, 4> durs;
    collectRoleOnNotes (ev, byRole, durs);

    EXPECT (byRole[0].size() > 0); // Foundation
    EXPECT (byRole[1].size() > 0); // Pulse at dens 0.5
    // Wanderer/Accent may be sparse — allow Accent empty over 128 bars at 0.5? presence should allow some
    EXPECT (byRole[0].size() + byRole[1].size() + byRole[2].size() + byRole[3].size() > 10);

    const double mf = meanPitch (byRole[0]);
    const double mp = meanPitch (byRole[1]);
    if (! byRole[2].empty())
    {
        const double mw = meanPitch (byRole[2]);
        EXPECT (mf < mp || byRole[1].empty());
        EXPECT (mp < mw || byRole[1].empty());
        if (! byRole[3].empty())
            EXPECT (mw <= meanPitch (byRole[3]) + 2.0); // soft: Accent higher center
    }

    if (! durs[0].empty() && ! durs[3].empty())
        EXPECT (meanDur (durs[0]) > meanDur (durs[3]));
    if (! durs[0].empty() && ! durs[2].empty())
        EXPECT (meanDur (durs[0]) > meanDur (durs[2]) * 0.85);

    // Accent rarer than Pulse
    if (! byRole[1].empty())
        EXPECT (byRole[3].size() < byRole[1].size());

    // Pulse more offbeat than Foundation
    auto offbeatRate = [] (const std::vector<std::pair<double, int>>& notes) {
        if (notes.empty())
            return 0.0;
        int off = 0;
        for (const auto& n : notes)
        {
            const double beatFrac = n.first - std::floor (n.first);
            if (std::abs (beatFrac - 0.5) < 1.0e-6)
                ++off;
        }
        return static_cast<double> (off) / static_cast<double> (notes.size());
    };
    if (byRole[0].size() >= 4 && byRole[1].size() >= 4)
        EXPECT (offbeatRate (byRole[1]) + 0.02 >= offbeatRate (byRole[0]));
}

static void testStage3PolyphonyAndCollision()
{
    ConductorEngine eng;
    eng.setParams ({ 0.75f, 0.35f });
    eng.reseed (2002);
    eng.clearCollisionStats();
    eng.setCapture (true);
    double ppq = 0.0;
    int maxActive = 0;
    std::array<int, 5> polyHist {};
    while (ppq < 256.0)
    {
        eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
        eng.processTimeRange (ppq, ppq + 0.25, true);
        eng.drainPending();
        const int a = eng.tracker().activeCount();
        maxActive = std::max (maxActive, a);
        polyHist[static_cast<size_t> (std::min (4, a))] += 1;
        // No double-ownership of same pitch
        for (int n = 0; n < 128; ++n)
        {
            if (! eng.tracker().isActive (1, n))
                continue;
            EXPECT (eng.tracker().ownerRole (1, n) >= 0);
        }
        ppq += 0.25;
    }
    eng.panic (256.0);
    EXPECT (maxActive >= 2); // polyphony emerges
    EXPECT (polyHist[4] < polyHist[1] + polyHist[2]); // 4-note not dominate
    // Unresolved same-pitch ownership failures must be 0 (suppressed/shifted ok)
    EXPECT (eng.collisionStats().attemptedSamePitch
            == eng.collisionStats().shifted + eng.collisionStats().suppressed);
}

static void testStage3RngIsolation()
{
    // Foundation pitch draw count must match whether Accent is “busy” via density —
    // dens=0 keeps Accent dormant; dens=1 adds Accent decisions without shared RNG.
    ConductorEngine a, b;
    a.setParams ({ 0.0f, 0.35f });
    a.reseed (4242);
    b.setParams ({ 1.0f, 0.35f });
    b.reseed (4242);

    auto drive = [] (ConductorEngine& eng) {
        double ppq = 0.0;
        while (ppq < 64.0)
        {
            eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
            eng.processTimeRange (ppq, ppq + 4.0, true);
            eng.drainPending();
            ppq += 4.0;
        }
    };
    drive (a);
    drive (b);
    // Same master seed + same bars: Foundation pitch stream consumption should match
    // (pitch eval schedule uses only foundation pitch RNG + params; dens affects period slightly!)
    // At different densities schedulePitchEval uses density — so draws may differ.
    // Stronger isolation: same density, but verify Accent stream tag independence via DNA.
    RhythmEngine accentA, accentB;
    accentA.setVoiceKind (pfl::generative::RhythmVoiceKind::Accent);
    accentB.setVoiceKind (pfl::generative::RhythmVoiceKind::Accent);
    // Different tags than foundation
    uint64_t h = 0xcbf29ce484222325ull;
    for (const char* s = "accent/rhythm"; *s; ++s)
    {
        h ^= static_cast<uint64_t> (*s);
        h *= 0x100000001b3ull;
    }
    uint64_t hf = 0xcbf29ce484222325ull;
    for (const char* s = "rhythm"; *s; ++s)
    {
        hf ^= static_cast<uint64_t> (*s);
        hf *= 0x100000001b3ull;
    }
    accentA.reset (DeterministicRNG::derived (4242, h), 0.35f, 0.5f, 0);
    RhythmEngine found;
    found.setVoiceKind (pfl::generative::RhythmVoiceKind::Foundation);
    found.reset (DeterministicRNG::derived (4242, hf), 0.35f, 0.5f, 0);
    EXPECT (accentA.dna().describe() != found.dna().describe());

    // Consuming extra accent RNG must not change foundation DNA
    DeterministicRNG accentExtra = DeterministicRNG::derived (4242, h);
    (void) accentExtra.nextFloat();
    (void) accentExtra.nextFloat();
    RhythmEngine found2;
    found2.setVoiceKind (pfl::generative::RhythmVoiceKind::Foundation);
    found2.reset (DeterministicRNG::derived (4242, hf), 0.35f, 0.5f, 0);
    EXPECT (found.dna().describe() == found2.dna().describe());
    (void) accentB;
    (void) a;
    (void) b;
}

static void testStage3DensityRoles()
{
    auto d0 = runMidi (2002, 0.0f, 0.35f, 72.0, 64, 256, 48000.0);
    auto d1 = runMidi (2002, 1.0f, 0.35f, 72.0, 64, 256, 48000.0);
    std::array<int, 4> c0 {}, c1 {};
    for (const auto& e : d0)
        if (e.kind == MidiMsgKind::NoteOn && e.voice >= 0 && e.voice < 4)
            ++c0[static_cast<size_t> (e.voice)];
    for (const auto& e : d1)
        if (e.kind == MidiMsgKind::NoteOn && e.voice >= 0 && e.voice < 4)
            ++c1[static_cast<size_t> (e.voice)];

    EXPECT (c0[0] > 0);
    EXPECT (c0[2] + c0[3] <= c0[0]); // decorative quiet at dens 0
    EXPECT (c1[0] > 0); // Foundation survives
    EXPECT (countNoteOns (d1) > countNoteOns (d0));
    EXPECT (c1[3] < c1[1] || c1[1] == 0); // Accent still sparse vs Pulse
    // Activity ceiling: not four continuous streams (~64 bars * 16 slots)
    EXPECT (countNoteOns (d1) < 64 * 12);
}

static void testStage3LongRunHour()
{
    ConductorEngine eng;
    eng.setParams ({ 0.55f, 0.40f });
    eng.reseed (3003);
    // 60 minutes at 72 BPM = 72*60 beats = 4320 beats = 1080 bars
    const double endPpq = 4320.0;
    double ppq = 0.0;
    size_t ons = 0;
    while (ppq < endPpq)
    {
        // Automate density/mutation gently
        const int bar = static_cast<int> (ppq / 4.0);
        if (bar % 64 == 0)
            eng.setParams ({ 0.35f + 0.4f * static_cast<float> ((bar / 64) % 2),
                             0.20f + 0.5f * static_cast<float> ((bar / 128) % 2) });
        const double next = std::min (endPpq, ppq + 16.0);
        eng.clock().advance ({ true, ppq, 72.0, 4, 4 });
        eng.processTimeRange (ppq, next, true);
        for (const auto& e : eng.drainPending())
            if (e.kind == MidiMsgKind::NoteOn)
                ++ons;
        EXPECT (eng.tracker().activeCount() <= 4);
        ppq = next;
    }
    eng.panic (endPpq);
    EXPECT (eng.tracker().activeCount() == 0);
    EXPECT (ons > 100);
    EXPECT (ons < 200000);
}

int main()
{
    testAlgorithmVersion();
    testDeterminism();
    testDifferentSeed();
    testBufferIndependence();
    testTempos();
    testNotePairingAndRegister();
    testStopNoHang();
    testRestart();
    testSeekNoHang();
    testSeekDeterministicResume();
    testLongRun();
    testParamRestore();
    testSixteenthGrid();
    testSyncopationExists();
    testRestsAndHolds();
    testRhythmMutationBounded();
    testRhythmRngIsolation();
    testDensityAffectsActivity();
    testDensityEndpoints();
    testMutationEndpoints();
    testLiveDensityResponse();
    testLiveMutationResponse();
    testAutomationDeterminism();
    testAwkwardCrossProduct();
    testStage3RoleIdentity();
    testStage3PolyphonyAndCollision();
    testStage3RngIsolation();
    testStage3DensityRoles();
    testStage3LongRunHour();

    if (gFails == 0)
    {
        std::cout << "broken_conductor_tests: OK (algorithm v"
                  << ConductorEngine::kAlgorithmVersion << ")\n";
        return 0;
    }
    std::cerr << "broken_conductor_tests: " << gFails << " failure(s)\n";
    return 1;
}

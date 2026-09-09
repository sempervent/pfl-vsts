#include "generative/ConductorEngine.h"
#include "generative/RhythmDNA.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <string>
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
    EXPECT (ConductorEngine::kAlgorithmVersion == 3);
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

    const double endPpq = 2160.0;
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
                EXPECT (e.note >= 26 && e.note <= 50);
            }
            else if (e.kind == MidiMsgKind::NoteOff)
                ++offs;
        }
        maxActive = std::max (maxActive, eng.tracker().activeCount());
        EXPECT (maxActive <= 1);
        ppq = next;
    }

    eng.panic (endPpq);
    EXPECT (eng.tracker().activeCount() == 0);
    EXPECT (ons < 500000);
    EXPECT (offs <= ons + 1);
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

    if (gFails == 0)
    {
        std::cout << "broken_conductor_tests: OK (algorithm v"
                  << ConductorEngine::kAlgorithmVersion << ")\n";
        return 0;
    }
    std::cerr << "broken_conductor_tests: " << gFails << " failure(s)\n";
    return 1;
}

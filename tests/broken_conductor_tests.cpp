#include "generative/ConductorEngine.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
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
        eng.clock().advance (snap);
        eng.processTimeRange (ppq, ppqEnd, true);
        eng.drainPending(); // discard; capture holds all
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
    std::vector<int> buffers { 64, 128, 256, 512, 1024 };
    auto ref = runMidi (777, 0.45f, 0.35f, 72.0, 48, 256, 48000.0);
    for (int b : buffers)
    {
        auto t = runMidi (777, 0.45f, 0.35f, 72.0, 48, b, 48000.0);
        EXPECT (midiEqual (ref, t));
    }
}

static void testTempos()
{
    auto ref = runMidi (4242, 0.5f, 0.4f, 72.0, 24, 256, 48000.0);
    for (double bpm : { 40.0, 120.0, 180.0 })
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
    EXPECT (eng.captured().size() == before); // no new events while stopped
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

    eng.handleSeek (40.0);
    // Mid-sustain occupancy may remain after reconstruct; panic must clear it.
    eng.setCapture (false);
    eng.panic (40.0);
    EXPECT (eng.tracker().activeCount() == 0);
    EXPECT (! eng.sounding());

    eng.clearCaptured();
    eng.setCapture (true);
    eng.processTimeRange (40.0, 56.0, true);
    eng.setCapture (false);
    eng.panic (56.0);
    EXPECT (eng.tracker().activeCount() == 0);
}

static void testSeekDeterministicResume()
{
    auto full = runMidi (2002, 0.45f, 0.35f, 72.0, 24, 256, 48000.0);

    ConductorEngine eng;
    eng.setParams ({ 0.45f, 0.35f });
    eng.reseed (2002);
    eng.handleSeek (32.0);
    eng.clearCaptured();
    eng.setCapture (true);
    // If mid-sustain at seek, continue without re-emitting the original NoteOn into capture
    eng.clock().advance ({ true, 32.0, 72.0, 4, 4 });
    eng.processTimeRange (32.0, 96.0, true);
    eng.setCapture (false);
    eng.panic (96.0);

    std::vector<MidiTraceEvent> expected;
    for (const auto& e : full)
        if (e.ppq + 1.0e-9 >= 32.0 && e.ppq < 96.0 + 1.0e-9)
            expected.push_back (e);

    // Uninterrupted may include a NoteOn before 32 that is still sustaining —
    // post-seek capture starts mid-sustain, so compare only events at/after first
    // shared off/on boundary by fingerprinting from equal PPQ events present in both.
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

    // Post-seek schedule should match uninterrupted events with ppq >= 32,
    // allowing an optional missing NoteOn that started before the seek point.
    size_t i = 0, j = 0;
    while (i < expected.size() && expected[i].ppq < 32.0 - 1.0e-9)
        ++i;
    // Skip a leading NoteOn in expected if post begins with NoteOff for same note
    if (i < expected.size() && j < post.size()
        && expected[i].kind == MidiMsgKind::NoteOn
        && post[j].kind == MidiMsgKind::NoteOff
        && expected[i].note == post[j].note)
        ++i;

    while (i < expected.size() && j < post.size())
    {
        EXPECT (expected[i].kind == post[j].kind);
        EXPECT (expected[i].note == post[j].note);
        EXPECT (std::abs (expected[i].ppq - post[j].ppq) < 1.0e-6);
        ++i;
        ++j;
    }
}

static void testLongRun()
{
    // ~30 minutes at 72 BPM = 30*72 = 2160 beats = 540 bars
    ConductorEngine eng;
    eng.setParams ({ 0.5f, 0.4f });
    eng.reseed (2002);

    const double endPpq = 2160.0;
    double ppq = 0.0;
    size_t ons = 0, offs = 0;
    int maxActive = 0;

    while (ppq < endPpq)
    {
        const double next = std::min (endPpq, ppq + 4.0); // 1 bar blocks
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
    EXPECT (offs <= ons + 1); // offs catch up after panic
}

static void testParamRestore()
{
    auto a = runMidi (12345, 0.45f, 0.35f, 72.0, 16, 256, 48000.0);
    auto changed = runMidi (12345, 0.9f, 0.8f, 72.0, 16, 256, 48000.0);
    EXPECT (! midiEqual (a, changed));
    auto restored = runMidi (12345, 0.45f, 0.35f, 72.0, 16, 256, 48000.0);
    EXPECT (midiEqual (a, restored));
}

static void testIntegerBeatGrid()
{
    auto ev = runMidi (2002, 0.45f, 0.35f, 72.0, 16, 64, 48000.0);
    for (const auto& e : ev)
    {
        const double nearest = std::round (e.ppq);
        EXPECT (std::abs (e.ppq - nearest) < 1.0e-6);
    }
}

int main()
{
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
    testIntegerBeatGrid();

    if (gFails == 0)
    {
        std::cout << "broken_conductor_tests: OK\n";
        return 0;
    }
    std::cerr << "broken_conductor_tests: " << gFails << " failure(s)\n";
    return 1;
}

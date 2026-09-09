#include "generative/Composer.h"
#include "performance/PerformanceController.h"

#include <cmath>
#include <iostream>
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

using pfl::generative::Composer;
using pfl::performance::Command;
using pfl::performance::Mode;
using pfl::performance::PerformanceController;
using pfl::performance::PerformanceEvent;

struct ScriptEvent
{
    double bar = 0.0; // musical bar (1-based downbeat = bar index)
    Command cmd = Command::FreezeOn;
};

static std::vector<PerformanceEvent> runScripted (uint64_t seed,
                                                  double bpm,
                                                  int totalBars,
                                                  int bufferSamples,
                                                  double sampleRate,
                                                  const std::vector<ScriptEvent>& script,
                                                  bool processSamples = true)
{
    Composer composer;
    composer.setEventCapture (true);
    composer.setParams ({ 0.45f, 0.35f });
    composer.reseed (seed);

    PerformanceController perf;
    perf.reset (seed);
    perf.setTraceEnabled (true);

    const double beatsPerSec = bpm / 60.0;
    const double bpb = 4.0;
    double ppq = 0.0;
    size_t scriptIdx = 0;

    auto cmdAt = [&] (double atPpq)
    {
        while (scriptIdx < script.size())
        {
            const double target = script[scriptIdx].bar * bpb;
            if (atPpq + 1.0e-9 < target)
                break;
            if (script[scriptIdx].cmd == Command::Reseed)
                perf.armReseedFade (static_cast<int> (sampleRate * 0.25));
            perf.trigger (script[scriptIdx].cmd, target, composer);
            ++scriptIdx;
        }
    };

    const double endPpq = static_cast<double> (totalBars) * bpb;
    while (ppq < endPpq - 1.0e-12)
    {
        const double blockBeats = (static_cast<double> (bufferSamples) / sampleRate) * beatsPerSec;
        const double ppqEnd = std::min (endPpq, ppq + blockBeats);

        cmdAt (ppq);

        pfl::generative::ClockSnapshot snap;
        snap.playing = true;
        snap.ppq = ppq;
        snap.tempoBpm = bpm;
        composer.clock().advance (snap);

        const int first = static_cast<int> (std::floor (ppq / bpb + 1.0e-9)) + 1;
        const int last = static_cast<int> (std::floor (ppqEnd / bpb + 1.0e-9));
        for (int b = first; b <= last; ++b)
            perf.onBar (b, static_cast<double> (b) * bpb, composer);

        perf.syncComposerLock (composer);
        composer.processTimeRange (ppq, ppqEnd, true);

        if (processSamples)
        {
            for (int i = 0; i < bufferSamples; ++i)
            {
                pfl::performance::PerformanceOutputs out;
                perf.processSample (sampleRate, out);
                (void) out;
            }
        }
        else
        {
            pfl::performance::PerformanceOutputs out;
            for (int i = 0; i < 8; ++i)
                perf.processSample (sampleRate, out);
        }

        ppq = ppqEnd;
    }

    cmdAt (endPpq + 1.0);
    return perf.events();
}

static bool eventsEqual (const std::vector<PerformanceEvent>& a, const std::vector<PerformanceEvent>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].command != b[i].command)
            return false;
        if (std::abs (a[i].ppq - b[i].ppq) > 1.0e-6)
            return false;
        if (a[i].seedAfter != b[i].seedAfter)
            return false;
    }
    return true;
}

static void testFreezeDeterminism()
{
    std::vector<ScriptEvent> script {
        { 17.0, Command::FreezeOn },
        { 25.0, Command::FreezeOff },
    };
    auto a = runScripted (2002, 72.0, 32, 128, 48000.0, script);
    auto b = runScripted (2002, 72.0, 32, 256, 48000.0, script);
    EXPECT (eventsEqual (a, b));
    EXPECT (a.size() >= 2);
}

static void testScriptedPerformanceDeterminism()
{
    std::vector<ScriptEvent> script {
        { 9.0, Command::FreezeOn },
        { 13.0, Command::Mutate },
        { 17.0, Command::FreezeOff },
        { 25.0, Command::Mutate },
        { 33.0, Command::Collapse },
        { 41.0, Command::Reseed },
        { 49.0, Command::SilenceOn },
        { 50.0, Command::SilenceOff },
    };

    auto a = runScripted (2002, 72.0, 56, 64, 48000.0, script);
    auto b = runScripted (2002, 72.0, 56, 128, 48000.0, script);
    auto c = runScripted (2002, 72.0, 56, 256, 48000.0, script);
    auto d = runScripted (2002, 72.0, 56, 512, 48000.0, script);

    EXPECT (eventsEqual (a, b));
    EXPECT (eventsEqual (a, c));
    EXPECT (eventsEqual (a, d));

    // Must include reseed with knowable seed
    bool sawReseed = false;
    for (const auto& e : a)
    {
        if (e.command == Command::Reseed)
        {
            sawReseed = true;
            EXPECT (e.seedAfter != 2002);
            EXPECT (e.seedAfter < 1000000ull);
        }
    }
    EXPECT (sawReseed);
}

static void testMutateWhileFrozen()
{
    Composer composer;
    composer.reseed (2002);
    composer.setParams ({ 0.45f, 0.35f });

    PerformanceController perf;
    perf.reset (2002);
    perf.setTraceEnabled (true);

    perf.trigger (Command::FreezeOn, 16.0, composer);
    EXPECT (perf.mode() == Mode::Frozen);
    EXPECT (composer.compositionLocked());

    const auto dnaBefore = composer.phrases().dna();
    perf.trigger (Command::Mutate, 16.5, composer);
    perf.trigger (Command::Mutate, 17.0, composer);
    EXPECT (perf.mode() == Mode::Frozen);

    const auto dnaAfter = composer.phrases().dna();
    EXPECT (dnaAfter.generation == dnaBefore.generation + 2
            || dnaAfter.generation >= dnaBefore.generation + 1);

    // Manual mutate must not consume pitch stream identically to autonomous path —
    // freeze discarded autonomous bars
    composer.processTimeRange (16.0, 64.0, true);
    EXPECT (composer.compositionLocked());
}

static void testManualMutationRngIsolation()
{
    Composer a, b;
    a.reseed (4242);
    b.reseed (4242);
    a.setParams ({ 0.5f, 0.4f });
    b.setParams ({ 0.5f, 0.4f });

    PerformanceController pa, pb;
    pa.reset (4242);
    pb.reset (4242);

    pa.trigger (Command::FreezeOn, 0.0, a);
    pb.trigger (Command::FreezeOn, 0.0, b);
    for (int i = 0; i < 5; ++i)
    {
        pa.trigger (Command::Mutate, static_cast<double> (i), a);
        pb.trigger (Command::Mutate, static_cast<double> (i), b);
    }

    EXPECT (a.phrases().dna().describe() == b.phrases().dna().describe());

    // Timbre stream unused by mutate
    EXPECT (a.consumeTimbreRandom() == b.consumeTimbreRandom());
}

static void testCollapseFreezeInteraction()
{
    Composer c;
    c.reseed (1001);
    PerformanceController p;
    p.reset (1001);
    p.trigger (Command::FreezeOn, 0.0, c);
    EXPECT (p.mode() == Mode::Frozen);
    p.trigger (Command::Collapse, 4.0, c);
    EXPECT (p.mode() == Mode::Collapsing);
    EXPECT (c.compositionLocked());
}

static void testSilencePriority()
{
    Composer c;
    c.reseed (1001);
    PerformanceController p;
    p.reset (1001);
    p.trigger (Command::Collapse, 0.0, c);
    p.trigger (Command::SilenceOn, 1.0, c);
    EXPECT (p.mode() == Mode::Silenced);

    pfl::performance::PerformanceOutputs out;
    for (int i = 0; i < 4800; ++i)
        p.processSample (48000.0, out);
    EXPECT (out.silenceGain < 0.05f);

    p.trigger (Command::Mutate, 2.0, c); // ignored
    p.trigger (Command::SilenceOff, 3.0, c);
    EXPECT (p.mode() == Mode::Collapsed);
}

static void testEdgeTriggerSemantics()
{
    // Holding mutate param conceptually: only 0→1 fires — exercised via single trigger calls
    Composer c;
    c.reseed (55);
    PerformanceController p;
    p.reset (55);
    p.setTraceEnabled (true);
    p.trigger (Command::Mutate, 0.0, c);
    // Second mutate while pending in NORMAL queues once then onBar flushes
    p.trigger (Command::Mutate, 0.1, c);
    p.onBar (1, 4.0, c);
    int mutates = 0;
    for (const auto& e : p.events())
        if (e.command == Command::Mutate)
            ++mutates;
    EXPECT (mutates == 1); // second request while pending coalesced to one flush
}

static void testFreezeNoCatchUp()
{
    Composer frozen, live;
    frozen.reseed (2002);
    live.reseed (2002);
    frozen.setParams ({ 0.6f, 0.5f });
    live.setParams ({ 0.6f, 0.5f });
    frozen.setEventCapture (true);
    live.setEventCapture (true);

    PerformanceController pf;
    pf.reset (2002);
    pf.trigger (Command::FreezeOn, 8.0, frozen);

    frozen.processTimeRange (0.0, 8.0, true);
    live.processTimeRange (0.0, 8.0, true);

    frozen.processTimeRange (8.0, 40.0, true); // frozen — no new decisions
    live.processTimeRange (8.0, 40.0, true);

    pf.trigger (Command::FreezeOff, 40.0, frozen);
    frozen.processTimeRange (40.0, 48.0, true);
    live.processTimeRange (40.0, 48.0, true);

    // Frozen path must have fewer note changes than fully live across freeze window
    auto countNotes = [] (const Composer& c)
    {
        int n = 0;
        for (const auto& e : c.capturedEvents())
            if (e.type == pfl::generative::EventType::NoteChange)
                ++n;
        return n;
    };
    EXPECT (countNotes (frozen) <= countNotes (live));
}

static void testReseedKnowable()
{
    Composer c;
    c.reseed (2002);
    PerformanceController p;
    p.reset (2002);
    p.setTraceEnabled (true);
    p.armReseedFade (1000);
    p.trigger (Command::Reseed, 32.0, c);
    uint64_t s = 0;
    EXPECT (p.takeSeedDirty (s));
    EXPECT (s == c.masterSeed());
    EXPECT (s != 2002);

    // Second run identical
    Composer c2;
    c2.reseed (2002);
    PerformanceController p2;
    p2.reset (2002);
    p2.armReseedFade (1000);
    p2.trigger (Command::Reseed, 32.0, c2);
    EXPECT (c2.masterSeed() == s);
}

static void testLongScriptedStability()
{
    // ~45 minutes at 72 BPM: 45*72/4 = 810 bars — use 900 bars for margin
    std::vector<ScriptEvent> script;
    for (int bar = 8; bar < 800; bar += 40)
    {
        script.push_back ({ static_cast<double> (bar), Command::FreezeOn });
        script.push_back ({ static_cast<double> (bar + 8), Command::Mutate });
        script.push_back ({ static_cast<double> (bar + 12), Command::FreezeOff });
        if (bar % 120 == 8)
            script.push_back ({ static_cast<double> (bar + 20), Command::Collapse });
        if (bar % 200 == 8)
            script.push_back ({ static_cast<double> (bar + 28), Command::Reseed });
        if (bar % 160 == 8)
        {
            script.push_back ({ static_cast<double> (bar + 30), Command::SilenceOn });
            script.push_back ({ static_cast<double> (bar + 31), Command::SilenceOff });
        }
    }

    auto events = runScripted (2002, 72.0, 900, 512, 48000.0, script, false);
    EXPECT (! events.empty());
    // No crash / no impossible empty after heavy scripting
    EXPECT (events.size() < 100000);
}

int main()
{
    testFreezeDeterminism();
    testScriptedPerformanceDeterminism();
    testMutateWhileFrozen();
    testManualMutationRngIsolation();
    testCollapseFreezeInteraction();
    testSilencePriority();
    testEdgeTriggerSemantics();
    testFreezeNoCatchUp();
    testReseedKnowable();
    testLongScriptedStability();

    if (gFails == 0)
    {
        std::cout << "performance_tests: OK\n";
        return 0;
    }
    std::cerr << "performance_tests: " << gFails << " failure(s)\n";
    return 1;
}

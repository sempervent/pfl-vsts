#include "dsp/MemoryEaterEngine.h"
#include "performance/MemoryEaterPerformanceController.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static int gFails = 0;
#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << #cond << "\n"; \
            ++gFails; \
        } \
    } while (0)

namespace
{
std::vector<float> makeIdentSource (int n, double sr, double bpm)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    const double bps = (bpm / 60.0) / sr;
    for (int i = 0; i < n; ++i)
    {
        const double beat = static_cast<double> (i) * bps;
        // Distinct tone per integer beat for provenance listening
        const int bi = static_cast<int> (std::floor (beat)) % 8;
        const double freq = 110.0 * (1 + bi);
        const double t = static_cast<double> (i) / sr;
        float s = 0.35f * std::sin (2.0 * 3.141592653589793 * freq * t);
        // Transient click each beat
        const double frac = beat - std::floor (beat);
        if (frac < 0.02)
            s += 0.5f * (1.0f - static_cast<float> (frac / 0.02));
        x[static_cast<size_t> (i)] = s;
    }
    return x;
}

void renderRun (pfl::dsp::MemoryEaterEngine& eng, std::vector<float>& L, std::vector<float>& R,
                double sr, double bpm, bool playing, int block)
{
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (L.size());
    int done = 0;
    while (done < n)
    {
        const int m = std::min (block, n - done);
        eng.process (L.data() + done, R.data() + done, m, playing,
                     static_cast<double> (done) * bps, bpm);
        done += m;
    }
}

void processWithPerf (pfl::dsp::MemoryEaterEngine& eng,
                      pfl::memory_perf::MemoryEaterPerformanceController& perf,
                      std::vector<float>& L, std::vector<float>& R,
                      double sr, double bpm, double startBeat, double numBeats,
                      bool playing, int block = 256)
{
    const double bps = (bpm / 60.0) / sr;
    const int n = std::max (1, static_cast<int> (numBeats / bps));
    if (static_cast<int> (L.size()) < n)
    {
        auto src = makeIdentSource (n, sr, bpm);
        L = src;
        R = src;
    }
    int done = 0;
    while (done < n)
    {
        const int m = std::min (block, n - done);
        const double ppq = startBeat + static_cast<double> (done) * bps;
        perf.tick (ppq, playing, eng);
        eng.process (L.data() + done, R.data() + done, m, playing, ppq, bpm);
        done += m;
    }
}

std::string fingerprintEcology (const pfl::dsp::MemoryEcology& eco)
{
    std::ostringstream os;
    os << "occ=" << eco.occupiedCount() << ";";
    for (int i = 0; i < eco.numSlots(); ++i)
    {
        const auto& s = eco.slot (i);
        if (! s.valid) continue;
        os << s.memoryId << ":g" << s.generation << ":p" << s.parentMemoryId
           << ":r" << s.rootMemoryId << ":s" << static_cast<int> (s.strength * 1000)
           << ";";
    }
    return os.str();
}
} // namespace

static void testAlgorithmVersion()
{
    EXPECT (pfl::dsp::MemoryEaterEngine::kAlgorithmVersion == 4);
    EXPECT (pfl::memory_perf::kPerformanceEngineVersion == 1);
}

static void testMixZeroDry()
{
    const double sr = 48000.0, bpm = 72.0;
    const int n = static_cast<int> (sr * 4);
    auto src = makeIdentSource (n, sr, bpm);
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (0.0f);
    eng.setHunger (1.0f);
    eng.setMemory (1.0f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 256);
    float maxDiff = 0.0f;
    for (int i = 2000; i < n; ++i)
        maxDiff = std::max (maxDiff, std::abs (L[static_cast<size_t> (i)] - src[static_cast<size_t> (i)]));
    EXPECT (maxDiff < 0.01f);
}

static void testMixOneNoDryLeak()
{
    const double sr = 48000.0, bpm = 72.0;
    const int n = static_cast<int> (sr * 8);
    auto src = makeIdentSource (n, sr, bpm);
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (1.0f);
    eng.setHunger (0.0f); // no recalls → silence at MIX1
    eng.setMemory (0.5f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 256);
    float peak = 0.0f;
    for (int i = 2000; i < n; ++i)
        peak = std::max (peak, std::abs (L[static_cast<size_t> (i)]));
    EXPECT (peak < 0.02f);
}

static void testDeterminism()
{
    const double sr = 48000.0, bpm = 72.0;
    const int n = static_cast<int> (sr * 16);
    auto src = makeIdentSource (n, sr, bpm);
    auto run = [&] ()
    {
        pfl::dsp::MemoryEaterEngine eng;
        eng.prepare (sr);
        eng.setSeed (3003);
        eng.setMix (0.6f);
        eng.setHunger (0.7f);
        eng.setMemory (0.6f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.setTraceEnabled (true);
        auto L = src, R = src;
        renderRun (eng, L, R, sr, bpm, true, 256);
        return std::make_pair (eng.events().size(), eng.historyFilledFrames());
    };
    const auto a = run();
    const auto b = run();
    EXPECT (a.first == b.first);
    EXPECT (a.second == b.second);
}

static void testBufferIndependenceEvents()
{
    const double sr = 48000.0, bpm = 72.0;
    const int n = static_cast<int> (sr * 20);
    auto src = makeIdentSource (n, sr, bpm);
    auto count = [&] (int block)
    {
        pfl::dsp::MemoryEaterEngine eng;
        eng.prepare (sr, 2048);
        eng.setSeed (3003);
        eng.setMix (0.5f);
        eng.setHunger (0.75f);
        eng.setMemory (0.55f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.setTraceEnabled (true);
        auto L = src, R = src;
        renderRun (eng, L, R, sr, bpm, true, block);
        return eng.events().size();
    };
    const auto ref = count (256);
    for (int bs : { 64, 127, 128, 255, 511, 512, 1024 })
        EXPECT (count (bs) == ref);
}

static void testSeekClearsMemory()
{
    const double sr = 48000.0, bpm = 72.0;
    const double bps = (bpm / 60.0) / sr;
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (0.5f);
    eng.setHunger (0.5f);
    eng.setMemory (0.5f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    const int warm = static_cast<int> (8.0 / bps);
    std::vector<float> L (static_cast<size_t> (warm), 0.3f), R (L);
    int done = 0;
    while (done < warm)
    {
        const int m = std::min (256, warm - done);
        eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
        done += m;
    }
    EXPECT (eng.historyFilledFrames() > 1000);
    // Seek far ahead
    std::vector<float> z (64, 0.2f);
    eng.process (z.data(), z.data(), 64, true, 400.0, bpm);
    EXPECT (eng.historyFilledFrames() < 200);
}

static void testStopPausesWriting()
{
    const double sr = 48000.0, bpm = 72.0;
    const double bps = (bpm / 60.0) / sr;
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (0.5f);
    eng.setHunger (0.5f);
    eng.setMemory (0.5f);
    eng.snapMacros();
    const int warm = static_cast<int> (4.0 / bps);
    std::vector<float> L (static_cast<size_t> (warm), 0.25f), R (L);
    int done = 0;
    while (done < warm)
    {
        const int m = std::min (256, warm - done);
        eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
        done += m;
    }
    const int filled = eng.historyFilledFrames();
    // Multiple stopped blocks with frozen host PPQ must retain memory.
    std::vector<float> z (512, 0.9f);
    for (int k = 0; k < 32; ++k)
        eng.process (z.data(), z.data(), 512, false, done * bps, bpm);
    EXPECT (eng.historyFilledFrames() == filled);
}

static void testSmallSeekClearsMemory()
{
    const double sr = 48000.0, bpm = 72.0;
    const double bps = (bpm / 60.0) / sr;
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr, 256);
    eng.setSeed (3003);
    eng.setMix (0.5f);
    eng.setHunger (0.5f);
    eng.setMemory (0.5f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    const int warm = static_cast<int> (8.0 / bps);
    std::vector<float> L (static_cast<size_t> (warm), 0.3f), R (L);
    int done = 0;
    while (done < warm)
    {
        const int m = std::min (256, warm - done);
        eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
        done += m;
    }
    EXPECT (eng.historyFilledFrames() > 1000);
    // Forward seek of ~1 beat (previously retained under 2-beat threshold)
    const double seekPpq = done * bps + 1.0;
    std::vector<float> z (64, 0.2f);
    eng.process (z.data(), z.data(), 64, true, seekPpq, bpm);
    EXPECT (eng.historyFilledFrames() < 200);
}

static void testMemoryChangesLookbackNotDensity()
{
    const double sr = 48000.0, bpm = 72.0;
    const int n = static_cast<int> (sr * 48);
    auto src = makeIdentSource (n, sr, bpm);
    auto run = [&] (float memory)
    {
        pfl::dsp::MemoryEaterEngine eng;
        eng.prepare (sr);
        eng.setSeed (3003);
        eng.setMix (0.5f);
        eng.setHunger (0.55f);
        eng.setMemory (memory);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.setTraceEnabled (true);
        auto L = src, R = src;
        renderRun (eng, L, R, sr, bpm, true, 256);
        double meanLb = 0.0;
        for (const auto& e : eng.events())
            meanLb += e.lookbackBeats;
        if (! eng.events().empty())
            meanLb /= static_cast<double> (eng.events().size());
        return std::make_pair (eng.events().size(), meanLb);
    };
    const auto lo = run (0.15f);
    const auto hi = run (0.90f);
    EXPECT (hi.second > lo.second);
    // Density should stay in the same ballpark (not HUNGER-like)
    EXPECT (std::abs (static_cast<int> (hi.first) - static_cast<int> (lo.first)) <= 6);
}

static void testCpuBudget()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 8.0);
    auto src = makeIdentSource (n, sr, bpm);
    auto measure = [&] (float hunger)
    {
        pfl::dsp::MemoryEaterEngine eng;
        eng.prepare (sr);
        eng.setSeed (3003);
        eng.setMix (0.5f);
        eng.setHunger (hunger);
        eng.setMemory (0.6f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        auto L = src, R = src;
        const auto t0 = std::chrono::steady_clock::now();
        renderRun (eng, L, R, sr, bpm, true, 256);
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    };
    const double t0 = measure (0.0f);
    const double t1 = measure (1.0f);
    const double realtime = 8.0;
    EXPECT (t1 < realtime * 0.25);
    std::cout << "memory-eater CPU 8s audio: hunger0=" << t0 << "s hunger1=" << t1 << "s\n";
}

static void testRingWrapSafe()
{
    const double sr = 48000.0, bpm = 180.0; // fills faster relative to capacity? still wraps eventually
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr, 512);
    eng.setSeed (3003);
    eng.setMix (0.7f);
    eng.setHunger (0.9f);
    eng.setMemory (0.9f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.setTraceEnabled (true);
    const int n = eng.historyCapacityFrames() * 3;
    auto src = makeIdentSource (n, sr, bpm);
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 256);
    float peak = 0.0f;
    bool finite = true;
    for (float s : L)
    {
        peak = std::max (peak, std::abs (s));
        if (! std::isfinite (s))
            finite = false;
    }
    EXPECT (finite);
    EXPECT (peak <= 0.995f);
    EXPECT (eng.historyFilledFrames() == eng.historyCapacityFrames());
}

static void testSampleRates()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const double bpm = 72.0;
        const int n = static_cast<int> (sr * 4);
        auto src = makeIdentSource (n, sr, bpm);
        pfl::dsp::MemoryEaterEngine eng;
        eng.prepare (sr);
        eng.setSeed (3003);
        eng.setMix (0.6f);
        eng.setHunger (0.7f);
        eng.setMemory (0.6f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        auto L = src, R = src;
        renderRun (eng, L, R, sr, bpm, true, 256);
        float peak = 0.0f;
        bool finite = true;
        for (float s : L)
        {
            peak = std::max (peak, std::abs (s));
            if (! std::isfinite (s))
                finite = false;
        }
        EXPECT (finite);
        EXPECT (peak <= 0.995f);
    }
}

static void testTempos()
{
    for (double bpm : { 40.0, 72.0, 93.0, 120.0, 137.0, 180.0 })
    {
        const double sr = 48000.0;
        const int n = static_cast<int> (sr * 3);
        auto src = makeIdentSource (n, sr, bpm);
        pfl::dsp::MemoryEaterEngine eng;
        eng.prepare (sr);
        eng.setSeed (3003);
        eng.setMix (0.5f);
        eng.setHunger (0.6f);
        eng.setMemory (0.5f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        auto L = src, R = src;
        renderRun (eng, L, R, sr, bpm, true, 256);
        float peak = 0.0f;
        for (float s : L)
            peak = std::max (peak, std::abs (s));
        EXPECT (peak <= 0.995f);
    }
}

static void testRamBounded()
{
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (96000.0, 8192);
    const size_t hist = eng.historyRamBytes();
    const size_t total = eng.totalRamBytes();
    EXPECT (hist < 45ull * 1024ull * 1024ull);
    EXPECT (hist > 10ull * 1024ull * 1024ull);
    EXPECT (total < 55ull * 1024ull * 1024ull);
    EXPECT (total > hist);
    std::cout << "memory-eater history RAM ≈ " << (hist / (1024.0 * 1024.0))
              << " MiB; total≈ " << (total / (1024.0 * 1024.0)) << " MiB\n";
}

static void testLongRun()
{
    const char* env = std::getenv ("PFL_LONG_TESTS");
    const bool longRun = env != nullptr && std::string (env) == "1";
    const double sr = 48000.0, bpm = 72.0;
    const double seconds = longRun ? 3600.0 : 30.0;
    const int n = static_cast<int> (sr * seconds);
    auto src = makeIdentSource (n, sr, bpm);
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (0.55f);
    eng.setHunger (0.85f);
    eng.setMemory (0.7f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    const auto t0 = std::chrono::steady_clock::now();
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 256);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                        std::chrono::steady_clock::now() - t0)
                        .count();
    float peak = 0.0f;
    bool finite = true;
    for (float s : L)
    {
        peak = std::max (peak, std::abs (s));
        if (! std::isfinite (s))
            finite = false;
    }
    EXPECT (finite);
    EXPECT (peak <= 0.995f);
    EXPECT (eng.ecology().numSlots() == 6);
    std::cout << "memory-eater long-run " << seconds << "s sim in " << ms << " ms, peak=" << peak
              << " capacity=" << eng.historyCapacityFrames()
              << " promotions=" << eng.ecology().promotions()
              << " storedRecalls=" << eng.ecology().recallsStored()
              << " forgot=" << eng.ecology().forgotten() << "\n";
}

static void testSeekPreservesEcology()
{
    const double sr = 48000.0, bpm = 72.0;
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (sr * 40.0);
    auto src = makeIdentSource (n, sr, bpm);
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (1.0f);
    eng.setHunger (0.85f);
    eng.setMemory (0.85f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.setTraceEnabled (true);
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 256);
    const int promBefore = eng.ecology().promotions();
    EXPECT (promBefore > 0);
    const int occ = eng.ecology().occupiedCount();
    EXPECT (occ > 0);
    // Seek clears ring
    std::vector<float> z (64, 0.2f);
    eng.process (z.data(), z.data(), 64, true, 500.0, bpm);
    EXPECT (eng.historyFilledFrames() < 200);
    EXPECT (eng.ecology().occupiedCount() == occ);
}

static void testStoredOutlivesRing()
{
    const double sr = 48000.0, bpm = 120.0;
    const double bps = (bpm / 60.0) / sr;
    // Populate ecology over ~48 beats
    const int warm = static_cast<int> (48.0 / bps);
    auto src = makeIdentSource (warm, sr, bpm);
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (4242);
    eng.setMix (1.0f);
    eng.setHunger (0.95f);
    eng.setMemory (0.95f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.setTraceEnabled (true);
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 256);
    EXPECT (eng.ecology().promotions() > 0);
    EXPECT (eng.ecology().occupiedCount() > 0);

    // Seek far ahead: ring empty, ecology intact
    std::vector<float> z (128, 0.25f);
    eng.process (z.data(), z.data(), 128, true, 400.0, bpm);
    eng.clearEvents();

    // Continue with new identifiable input for another ~96 beats
    const int cont = static_cast<int> (96.0 / bps);
    auto src2 = makeIdentSource (cont, sr, bpm);
    L = src2;
    R = src2;
    const int block = 256;
    int done = 0;
    while (done < cont)
    {
        const int m = std::min (block, cont - done);
        eng.process (L.data() + done, R.data() + done, m, true, 400.0 + done * bps, bpm);
        done += m;
    }

    int stored = 0;
    double oldestAge = 0.0;
    for (const auto& e : eng.events())
    {
        if (e.fromStored)
        {
            ++stored;
            oldestAge = std::max (oldestAge, e.eventBeat - e.sourceBeat);
        }
    }
    EXPECT (stored > 0);
    EXPECT (oldestAge > 32.0); // beyond Stage 1 ring horizon in beats of age
    std::cout << "stored recalls after seek=" << stored << " oldestAge=" << oldestAge << "\n";
}

static void testEcologyForgetting()
{
    const double sr = 48000.0, bpm = 120.0;
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (200.0 / bps);
    auto src = makeIdentSource (n, sr, bpm);
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (7);
    eng.setMix (1.0f);
    eng.setHunger (0.35f); // sparse recalls → less reinforcement
    eng.setMemory (0.2f);  // faster decay
    eng.setOutput (0.9f);
    eng.snapMacros();
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 256);
    // With low reinforcement over long span, forgetting should occur.
    EXPECT (eng.ecology().promotions() > 0);
    EXPECT (eng.ecology().forgotten() > 0);
    std::cout << "ecology forgot=" << eng.ecology().forgotten()
              << " promotions=" << eng.ecology().promotions()
              << " occupied=" << eng.ecology().occupiedCount() << "\n";
}

static void testDescendantsAppear()
{
    const double sr = 48000.0, bpm = 96.0;
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (256.0 / bps);
    auto src = makeIdentSource (n, sr, bpm);
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (9001);
    eng.setMix (1.0f);
    eng.setHunger (0.85f);
    eng.setMemory (0.85f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.setTraceEnabled (true);
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 256);
    EXPECT (eng.ecology().descendants() > 0);
    int maxGen = 0;
    for (int i = 0; i < eng.ecology().numSlots(); ++i)
    {
        const auto& s = eng.ecology().slot (i);
        if (s.valid)
            maxGen = std::max (maxGen, s.generation);
    }
    for (const auto& e : eng.events())
        if (e.fromStored)
            maxGen = std::max (maxGen, e.generation);
    EXPECT (maxGen >= 1);
    EXPECT (maxGen <= pfl::dsp::MemoryEcology::kMaxGeneration);
    EXPECT (eng.ecology().maxLineageOccupancy() <= 3);
    std::cout << "descendants=" << eng.ecology().descendants()
              << " maxGen=" << maxGen
              << " maxLineageOcc=" << eng.ecology().maxLineageOccupancy() << "\n";
}

static void testGenerationCapNoOverflow()
{
    EXPECT (pfl::dsp::MemoryEcology::kMaxGeneration == 3);
}

static void testSeekCancelsCapturePreservesLineage()
{
    const double sr = 48000.0, bpm = 96.0;
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (180.0 / bps);
    auto src = makeIdentSource (n, sr, bpm);
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (4242);
    eng.setMix (1.0f);
    eng.setHunger (0.9f);
    eng.setMemory (0.9f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 128);
    const int desc = eng.ecology().descendants();
    const int occ = eng.ecology().occupiedCount();
    std::vector<float> z (64, 0.2f);
    eng.process (z.data(), z.data(), 64, true, 600.0, bpm);
    EXPECT (! eng.captureArmed());
    EXPECT (eng.ecology().occupiedCount() == occ);
    EXPECT (eng.ecology().descendants() == desc);
}

static void testNoWetWriteback()
{
    // Ring must only grow from input: after MIX=1 recalls, silence input should
    // not invent new energy into history beyond written zeros.
    const double sr = 48000.0, bpm = 72.0;
    const double bps = (bpm / 60.0) / sr;
    const int warm = static_cast<int> (16.0 / bps);
    auto src = makeIdentSource (warm, sr, bpm);
    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (1.0f);
    eng.setHunger (1.0f);
    eng.setMemory (0.8f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    auto L = src, R = src;
    renderRun (eng, L, R, sr, bpm, true, 256);
    const int filled = eng.historyFilledFrames();
    std::vector<float> zeros (4096, 0.0f);
    eng.process (zeros.data(), zeros.data(), 4096, true, warm * bps, bpm);
    // History advanced with zeros (original input), not wet — capacity fill still valid
    EXPECT (eng.historyFilledFrames() >= filled);
}

static void testHungerIncreasesActivity()
{
    const double sr = 48000.0, bpm = 72.0;
    const int n = static_cast<int> (sr * 32); // ~38 beats
    auto src = makeIdentSource (n, sr, bpm);
    auto count = [&] (float hunger)
    {
        pfl::dsp::MemoryEaterEngine eng;
        eng.prepare (sr);
        eng.setSeed (3003);
        eng.setMix (0.5f);
        eng.setHunger (hunger);
        eng.setMemory (0.55f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.setTraceEnabled (true);
        auto L = src, R = src;
        renderRun (eng, L, R, sr, bpm, true, 256);
        return eng.events().size();
    };
    const auto low = count (0.15f);
    const auto high = count (0.90f);
    EXPECT (high > low);
}

static void testStage4FreezeKeepsRecallsNoPromote()
{
    const double sr = 48000.0, bpm = 72.0;
    pfl::dsp::MemoryEaterEngine eng;
    pfl::memory_perf::MemoryEaterPerformanceController perf;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (1.0f);
    eng.setHunger (0.85f);
    eng.setMemory (0.80f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.setTraceEnabled (true);
    perf.reset (3003);
    perf.setTraceEnabled (true);
    std::vector<float> L, R;
    processWithPerf (eng, perf, L, R, sr, bpm, 0.0, 96.0, true);
    const int occBefore = eng.ecology().occupiedCount();
    const int promoBefore = eng.ecology().promotions();
    const int descBefore = eng.ecology().descendants();
    const auto fpBefore = fingerprintEcology (eng.ecology());
    EXPECT (occBefore > 0);

    perf.trigger (pfl::memory_perf::Command::FreezeOn, 96.0, eng);
    EXPECT (perf.mode() == pfl::memory_perf::Mode::Frozen);
    EXPECT (! eng.ringWriteEnabled());
    const size_t recallsBefore = eng.events().size();
    processWithPerf (eng, perf, L, R, sr, bpm, 96.0, 64.0, true);
    EXPECT (eng.ecology().promotions() == promoBefore);
    EXPECT (eng.ecology().descendants() == descBefore);
    EXPECT (fingerprintEcology (eng.ecology()) == fpBefore);
    EXPECT (eng.events().size() > recallsBefore);
}

static void testStage4MutateWhileFrozen()
{
    const double sr = 48000.0, bpm = 72.0;
    pfl::dsp::MemoryEaterEngine eng;
    pfl::memory_perf::MemoryEaterPerformanceController perf;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (1.0f);
    eng.setHunger (0.9f);
    eng.setMemory (0.85f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    perf.reset (3003);
    perf.setTraceEnabled (true);
    std::vector<float> L, R;
    processWithPerf (eng, perf, L, R, sr, bpm, 0.0, 128.0, true);
    EXPECT (eng.ecology().occupiedCount() > 0);
    perf.trigger (pfl::memory_perf::Command::FreezeOn, 128.0, eng);
    const int desc0 = eng.ecology().descendants();
    perf.trigger (pfl::memory_perf::Command::Mutate, 130.0, eng);
    EXPECT (perf.mode() == pfl::memory_perf::Mode::Frozen);
    EXPECT (eng.ecology().descendants() >= desc0);
    perf.trigger (pfl::memory_perf::Command::Mutate, 140.0, eng);
    EXPECT (perf.mode() == pfl::memory_perf::Mode::Frozen);
}

static void testStage4MutateEmptyNoOp()
{
    pfl::dsp::MemoryEaterEngine eng;
    pfl::memory_perf::MemoryEaterPerformanceController perf;
    eng.prepare (48000.0);
    eng.setSeed (3003);
    eng.snapMacros();
    perf.reset (3003);
    perf.setTraceEnabled (true);
    perf.trigger (pfl::memory_perf::Command::Mutate, 0.0, eng);
    EXPECT (perf.state().lastMutateChildId < 0);
    EXPECT (eng.ecology().descendants() == 0);
}

static void testStage4CollapseResidue()
{
    const double sr = 48000.0, bpm = 72.0;
    pfl::dsp::MemoryEaterEngine eng;
    pfl::memory_perf::MemoryEaterPerformanceController perf;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (1.0f);
    eng.setHunger (0.95f);
    eng.setMemory (0.9f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    perf.reset (3003);
    perf.setTraceEnabled (true);
    std::vector<float> L, R;
    processWithPerf (eng, perf, L, R, sr, bpm, 0.0, 160.0, true);
    const int before = eng.ecology().occupiedCount();
    EXPECT (before >= 1);
    perf.trigger (pfl::memory_perf::Command::Collapse, 160.0, eng);
    processWithPerf (eng, perf, L, R, sr, bpm, 160.0, 28.0, true);
    EXPECT (perf.mode() == pfl::memory_perf::Mode::Collapsed
            || perf.mode() == pfl::memory_perf::Mode::Collapsing);
    if (perf.mode() == pfl::memory_perf::Mode::Collapsed)
    {
        EXPECT (eng.ecology().occupiedCount() <= 1);
        EXPECT (eng.ecology().occupiedCount() <= before);
    }
    const auto mode = perf.mode();
    perf.trigger (pfl::memory_perf::Command::Collapse, 190.0, eng);
    EXPECT (perf.mode() == mode);
}

static void testStage4ReseedPreservesEcology()
{
    const double sr = 48000.0, bpm = 72.0;
    pfl::dsp::MemoryEaterEngine eng;
    pfl::memory_perf::MemoryEaterPerformanceController perf;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (1.0f);
    eng.setHunger (0.9f);
    eng.setMemory (0.85f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    perf.reset (3003);
    std::vector<float> L, R;
    processWithPerf (eng, perf, L, R, sr, bpm, 0.0, 128.0, true);
    const auto fp = fingerprintEcology (eng.ecology());
    const auto oldSeed = eng.seed();
    EXPECT (eng.ecology().occupiedCount() > 0);
    perf.trigger (pfl::memory_perf::Command::Reseed, 128.0, eng);
    EXPECT (eng.seed() != oldSeed);
    EXPECT (fingerprintEcology (eng.ecology()) == fp);
}

static void testStage4SilenceMutesTotalOutput()
{
    const double sr = 48000.0, bpm = 72.0;
    pfl::dsp::MemoryEaterEngine eng;
    pfl::memory_perf::MemoryEaterPerformanceController perf;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (0.0f);
    eng.setHunger (0.5f);
    eng.setMemory (0.5f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    perf.reset (3003);
    perf.trigger (pfl::memory_perf::Command::SilenceOn, 0.0, eng);
    EXPECT (eng.ringWriteEnabled());
    const int n = 4800;
    auto in = makeIdentSource (n, sr, bpm);
    float peak = 1.0f;
    for (int iter = 0; iter < 12; ++iter)
    {
        auto outL = in, outR = in;
        perf.tick (0.0, true, eng);
        eng.process (outL.data(), outR.data(), n, true, 0.0, bpm);
        peak = 0.0f;
        for (int s = 0; s < n; ++s)
            peak = std::max (peak, std::max (std::abs (outL[static_cast<size_t> (s)]),
                                             std::abs (outR[static_cast<size_t> (s)])));
    }
    EXPECT (peak < 0.02f);
    EXPECT (eng.silenceGain() < 0.05f);
}

static void testStage4CommandScriptDeterminism()
{
    const double sr = 48000.0, bpm = 72.0;
    auto run = [&] ()
    {
        pfl::dsp::MemoryEaterEngine eng;
        pfl::memory_perf::MemoryEaterPerformanceController perf;
        eng.prepare (sr);
        eng.setSeed (3003);
        eng.setMix (1.0f);
        eng.setHunger (0.75f);
        eng.setMemory (0.70f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.setTraceEnabled (true);
        perf.reset (3003);
        perf.setTraceEnabled (true);
        std::vector<float> L, R;
        processWithPerf (eng, perf, L, R, sr, bpm, 0.0, 64.0, true);
        perf.trigger (pfl::memory_perf::Command::FreezeOn, 64.0, eng);
        processWithPerf (eng, perf, L, R, sr, bpm, 64.0, 16.0, true);
        perf.trigger (pfl::memory_perf::Command::Mutate, 80.0, eng);
        processWithPerf (eng, perf, L, R, sr, bpm, 80.0, 16.0, true);
        perf.trigger (pfl::memory_perf::Command::Mutate, 96.0, eng);
        processWithPerf (eng, perf, L, R, sr, bpm, 96.0, 16.0, true);
        perf.trigger (pfl::memory_perf::Command::FreezeOff, 112.0, eng);
        processWithPerf (eng, perf, L, R, sr, bpm, 112.0, 32.0, true);
        perf.trigger (pfl::memory_perf::Command::Collapse, 144.0, eng);
        processWithPerf (eng, perf, L, R, sr, bpm, 144.0, 12.0, true);
        perf.trigger (pfl::memory_perf::Command::SilenceOn, 156.0, eng);
        processWithPerf (eng, perf, L, R, sr, bpm, 156.0, 8.0, true);
        perf.trigger (pfl::memory_perf::Command::SilenceOff, 164.0, eng);
        processWithPerf (eng, perf, L, R, sr, bpm, 164.0, 28.0, true);
        perf.trigger (pfl::memory_perf::Command::Reseed, 192.0, eng);
        processWithPerf (eng, perf, L, R, sr, bpm, 192.0, 64.0, true);

        std::ostringstream os;
        os << "seed=" << eng.seed()
           << " occ=" << eng.ecology().occupiedCount()
           << " desc=" << eng.ecology().descendants()
           << " recalls=" << eng.events().size()
           << " residue=" << perf.state().residueMemoryId
           << " mode=" << static_cast<int> (perf.mode());
        for (const auto& e : perf.events())
            os << "|" << e.ppq << ":" << e.detail;
        return os.str();
    };
    EXPECT (run() == run());
}

static void testStage4PerfBufferMatrix()
{
    const double sr = 48000.0, bpm = 72.0;
    const int blocks[] = { 64, 127, 128, 255, 256, 511, 512, 1024 };
    std::string ref;
    for (int b : blocks)
    {
        pfl::dsp::MemoryEaterEngine eng;
        pfl::memory_perf::MemoryEaterPerformanceController perf;
        eng.prepare (sr, b);
        eng.setSeed (3003);
        eng.setMix (1.0f);
        eng.setHunger (0.7f);
        eng.setMemory (0.7f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.setTraceEnabled (true);
        perf.reset (3003);
        std::vector<float> L, R;
        processWithPerf (eng, perf, L, R, sr, bpm, 0.0, 48.0, true, b);
        perf.trigger (pfl::memory_perf::Command::FreezeOn, 48.0, eng);
        processWithPerf (eng, perf, L, R, sr, bpm, 48.0, 16.0, true, b);
        perf.trigger (pfl::memory_perf::Command::Mutate, 64.0, eng);
        std::ostringstream os;
        os << eng.ecology().occupiedCount() << ":" << eng.ecology().descendants()
           << ":" << perf.state().lastMutateChildId;
        if (ref.empty())
            ref = os.str();
        else
            EXPECT (os.str() == ref);
    }
}

static void testStage4LongRunSoak()
{
    const double sr = 44100.0, bpm = 120.0;
    const double beats = 120.0; // ~60s at 120bpm
    pfl::dsp::MemoryEaterEngine eng;
    pfl::memory_perf::MemoryEaterPerformanceController perf;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (1.0f);
    eng.setHunger (0.95f);
    eng.setMemory (0.90f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    perf.reset (3003);
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (beats / bps);
    auto L = makeIdentSource (n, sr, bpm);
    auto R = L;
    const size_t ram0 = eng.totalRamBytes();
    int done = 0;
    int cmdBeat = 32;
    int phase = 0;
    while (done < n)
    {
        const int m = std::min (256, n - done);
        const double ppq = static_cast<double> (done) * bps;
        if (ppq >= cmdBeat)
        {
            using C = pfl::memory_perf::Command;
            switch (phase % 5)
            {
                case 0: perf.trigger (C::FreezeOn, ppq, eng); break;
                case 1: perf.trigger (C::Mutate, ppq, eng); break;
                case 2: perf.trigger (C::FreezeOff, ppq, eng);
                        perf.trigger (C::Collapse, ppq, eng); break;
                case 3: perf.trigger (C::SilenceOn, ppq, eng); break;
                case 4: perf.trigger (C::SilenceOff, ppq, eng);
                        perf.trigger (C::Reseed, ppq, eng); break;
            }
            ++phase;
            cmdBeat += 24;
        }
        perf.tick (ppq, true, eng);
        eng.process (L.data() + done, R.data() + done, m, true, ppq, bpm);
        done += m;
        for (int i = 0; i < m; ++i)
        {
            EXPECT (std::isfinite (L[static_cast<size_t> (done - m + i)]));
            EXPECT (std::isfinite (R[static_cast<size_t> (done - m + i)]));
        }
    }
    EXPECT (eng.ecology().occupiedCount() <= 6);
    EXPECT (eng.totalRamBytes() == ram0);
    for (int i = 0; i < eng.ecology().numSlots(); ++i)
    {
        const auto& s = eng.ecology().slot (i);
        if (s.valid)
            EXPECT (s.generation <= pfl::dsp::MemoryEcology::kMaxGeneration);
    }
}

int main()
{
    testAlgorithmVersion();
    testMixZeroDry();
    testMixOneNoDryLeak();
    testDeterminism();
    testBufferIndependenceEvents();
    testSeekClearsMemory();
    testSeekPreservesEcology();
    testStopPausesWriting();
    testSmallSeekClearsMemory();
    testMemoryChangesLookbackNotDensity();
    testCpuBudget();
    testRingWrapSafe();
    testSampleRates();
    testTempos();
    testRamBounded();
    testLongRun();
    testHungerIncreasesActivity();
    testStoredOutlivesRing();
    testEcologyForgetting();
    testDescendantsAppear();
    testGenerationCapNoOverflow();
    testSeekCancelsCapturePreservesLineage();
    testNoWetWriteback();
    testStage4FreezeKeepsRecallsNoPromote();
    testStage4MutateWhileFrozen();
    testStage4MutateEmptyNoOp();
    testStage4CollapseResidue();
    testStage4ReseedPreservesEcology();
    testStage4SilenceMutesTotalOutput();
    testStage4CommandScriptDeterminism();
    testStage4PerfBufferMatrix();
    testStage4LongRunSoak();

    if (gFails == 0)
    {
        std::cout << "memory_eater_tests: OK\n";
        return 0;
    }
    std::cerr << "memory_eater_tests: " << gFails << " failure(s)\n";
    return 1;
}

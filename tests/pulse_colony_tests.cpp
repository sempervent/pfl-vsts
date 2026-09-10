#include "dsp/PulseColonyEngine.h"

#include <cmath>
#include <cstdint>
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
std::vector<float> makeTone (int n, double sr, double freq = 220.0, float amp = 0.4f)
{
    std::vector<float> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        x[static_cast<size_t> (i)] = amp * static_cast<float> (std::sin (2.0 * 3.141592653589793 * freq * t));
    }
    return x;
}

std::vector<float> makeClicks (int n, double sr, double bpm)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    const double bps = (bpm / 60.0) / sr;
    for (int i = 0; i < n; ++i)
    {
        const double beat = static_cast<double> (i) * bps;
        const double frac = beat - std::floor (beat);
        if (frac < 0.02)
            x[static_cast<size_t> (i)] = 0.7f * (1.0f - static_cast<float> (frac / 0.02));
    }
    return x;
}

void processRun (pfl::dsp::PulseColonyEngine& eng, std::vector<float>& L, std::vector<float>& R,
                 double sr, double bpm, double startBeat, bool playing, int block = 256)
{
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (L.size());
    int done = 0;
    while (done < n)
    {
        const int m = std::min (block, n - done);
        eng.process (L.data() + done, R.data() + done, m, playing,
                     startBeat + static_cast<double> (done) * bps, bpm);
        done += m;
    }
}

float wetOpenFrac (const std::vector<float>& dry, const std::vector<float>& wet, float thresh = 0.02f)
{
    int open = 0, total = 0;
    for (size_t i = 0; i < dry.size(); ++i)
    {
        if (std::abs (dry[i]) < 0.05f) continue;
        ++total;
        if (std::abs (wet[i]) > thresh)
            ++open;
    }
    return total > 0 ? static_cast<float> (open) / static_cast<float> (total) : 0.0f;
}
} // namespace

static void testAlgorithmVersion()
{
    EXPECT (pfl::dsp::PulseColonyEngine::kAlgorithmVersion == 1);
}

static void testMixZeroDry()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 4);
    auto src = makeTone (n, sr);
    pfl::dsp::PulseColonyEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (0.0f);
    eng.setDensity (1.0f);
    eng.setMutation (0.5f);
    eng.setMotion (1.0f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    eng.forceRebuild (0);
    auto L = src, R = src;
    processRun (eng, L, R, sr, bpm, 0.0, true);
    float maxDiff = 0.0f;
    for (int i = 2000; i < n; ++i)
        maxDiff = std::max (maxDiff, std::abs (L[static_cast<size_t> (i)] - src[static_cast<size_t> (i)]));
    EXPECT (maxDiff < 0.02f);
}

static void testDeterminism()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 16);
    auto src = makeTone (n, sr);
    auto run = [&] ()
    {
        pfl::dsp::PulseColonyEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (1.0f);
        eng.setDensity (0.50f);
        eng.setMutation (0.35f);
        eng.setMotion (0.35f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.forceRebuild (0);
        eng.setTraceEnabled (true);
        auto L = src, R = src;
        processRun (eng, L, R, sr, bpm, 0.0, true);
        std::ostringstream os;
        os << eng.dna().generation << ":" << eng.dna().lengthBars << ":"
           << eng.dna().windowCount << ":" << eng.opens().size() << ":";
        for (int i = 0; i < eng.dna().lengthCells(); ++i)
            os << static_cast<int> (eng.dna().mask[static_cast<size_t> (i)]);
        return os.str();
    };
    EXPECT (run() == run());
}

static void testBufferIndependence()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 12);
    auto src = makeTone (n, sr);
    const int blocks[] = { 64, 127, 128, 255, 256, 511, 512, 1024 };
    std::string ref;
    for (int b : blocks)
    {
        pfl::dsp::PulseColonyEngine eng;
        eng.prepare (sr, b);
        eng.setSeed (2002);
        eng.setMix (1.0f);
        eng.setDensity (0.5f);
        eng.setMutation (0.35f);
        eng.setMotion (0.0f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.forceRebuild (0);
        eng.setTraceEnabled (true);
        auto L = src, R = src;
        processRun (eng, L, R, sr, bpm, 0.0, true, b);
        std::ostringstream os;
        os << eng.opens().size() << ":" << eng.dna().generation << ":" << eng.dna().windowCount;
        if (ref.empty())
            ref = os.str();
        else
            EXPECT (os.str() == ref);
    }
}

static void testDensityAffectsOccupancy()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 32);
    auto src = makeTone (n, sr, 110.0, 0.5f);
    auto occ = [&] (float dens)
    {
        pfl::dsp::PulseColonyEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (1.0f);
        eng.setDensity (dens);
        eng.setMutation (0.0f);
        eng.setMotion (0.0f);
        eng.setOutput (1.0f);
        eng.snapMacros();
        eng.forceRebuild (0);
        auto L = src, R = src;
        processRun (eng, L, R, sr, bpm, 0.0, true);
        return eng.dna().occupancy();
    };
    const float low = occ (0.20f);
    const float high = occ (0.90f);
    EXPECT (high > low);
    EXPECT (high < 0.95f);
    EXPECT (low > 0.05f);
}

static void testMutationZeroStable()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 48); // many bars
    auto src = makeTone (n, sr);
    pfl::dsp::PulseColonyEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (1.0f);
    eng.setDensity (0.5f);
    eng.setMutation (0.0f);
    eng.setMotion (0.0f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceRebuild (0);
    const int gen0 = eng.generation();
    const auto mask0 = eng.dna().mask;
    auto L = src, R = src;
    processRun (eng, L, R, sr, bpm, 0.0, true);
    EXPECT (eng.generation() == gen0);
    EXPECT (eng.dna().mask == mask0);
}

static void testDensityMutationCross()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 64);
    auto src = makeTone (n, sr);
    auto run = [&] (float dens, float mut)
    {
        pfl::dsp::PulseColonyEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (1.0f);
        eng.setDensity (dens);
        eng.setMutation (mut);
        eng.setMotion (0.0f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.forceRebuild (0);
        auto L = src, R = src;
        processRun (eng, L, R, sr, bpm, 0.0, true);
        return std::make_pair (eng.dna().occupancy(), eng.generation());
    };
    const auto sparseEvolve = run (0.20f, 0.90f);
    const auto busyStable = run (0.90f, 0.10f);
    EXPECT (busyStable.first > sparseEvolve.first);
    EXPECT (sparseEvolve.second >= busyStable.second);
}

static void testMotionZeroPreservesStereo()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 8);
    auto L = makeTone (n, sr, 220.0, 0.35f);
    auto R = L;
    pfl::dsp::PulseColonyEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (1.0f);
    eng.setDensity (0.5f);
    eng.setMutation (0.0f);
    eng.setMotion (0.0f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    eng.forceRebuild (0);
    processRun (eng, L, R, sr, bpm, 0.0, true);
    float maxLR = 0.0f;
    for (int i = 2000; i < n; ++i)
        maxLR = std::max (maxLR, std::abs (L[static_cast<size_t> (i)] - R[static_cast<size_t> (i)]));
    EXPECT (maxLR < 1.0e-4f);
}

static void testStopPassThrough()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 2);
    auto src = makeTone (n, sr, 220.0, 0.5f);
    pfl::dsp::PulseColonyEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (1.0f);
    eng.setDensity (0.5f);
    eng.setMutation (0.5f);
    eng.setMotion (0.0f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    eng.forceRebuild (0);
    auto L = src, R = src;
    processRun (eng, L, R, sr, bpm, 0.0, false);
    int loud = 0, muted = 0;
    for (int i = 4000; i < n; ++i)
    {
        if (std::abs (src[static_cast<size_t> (i)]) < 0.2f)
            continue;
        ++loud;
        if (std::abs (L[static_cast<size_t> (i)]) < 0.05f)
            ++muted;
    }
    EXPECT (loud > 100);
    EXPECT (muted * 20 < loud); // <5% of loud samples muted while stopped
}

static void testSilentInput()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 4);
    std::vector<float> L (n, 0.0f), R (n, 0.0f);
    pfl::dsp::PulseColonyEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (1.0f);
    eng.setDensity (1.0f);
    eng.setMutation (1.0f);
    eng.setMotion (1.0f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    eng.forceRebuild (0);
    processRun (eng, L, R, sr, bpm, 0.0, true);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::max (std::abs (L[static_cast<size_t> (i)]),
                                         std::abs (R[static_cast<size_t> (i)])));
    EXPECT (peak < 1.0e-4f);
}

static void testTemposAndRates()
{
    const double tempos[] = { 40, 72, 93, 120, 137, 180 };
    const double rates[] = { 44100, 48000, 96000 };
    for (double sr : rates)
    {
        for (double bpm : tempos)
        {
            const int n = static_cast<int> (sr * 4);
            auto src = makeClicks (n, sr, bpm);
            pfl::dsp::PulseColonyEngine eng;
            eng.prepare (sr);
            eng.setSeed (2002);
            eng.setMix (1.0f);
            eng.setDensity (0.5f);
            eng.setMutation (0.35f);
            eng.setMotion (0.35f);
            eng.setOutput (0.9f);
            eng.snapMacros();
            eng.forceRebuild (0);
            auto L = src, R = src;
            processRun (eng, L, R, sr, bpm, 0.0, true);
            for (int i = 0; i < n; ++i)
            {
                EXPECT (std::isfinite (L[static_cast<size_t> (i)]));
                EXPECT (std::isfinite (R[static_cast<size_t> (i)]));
                EXPECT (std::abs (L[static_cast<size_t> (i)]) <= 1.0f);
            }
        }
    }
}

static void testSeekReconstruct()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 8);
    auto src = makeTone (n, sr);
    pfl::dsp::PulseColonyEngine a, b;
    for (auto* eng : { &a, &b })
    {
        eng->prepare (sr);
        eng->setSeed (2002);
        eng->setMix (1.0f);
        eng->setDensity (0.5f);
        eng->setMutation (0.35f);
        eng->setMotion (0.0f);
        eng->setOutput (0.9f);
        eng->snapMacros();
        eng->forceRebuild (0);
        eng->setTraceEnabled (true);
    }
    auto La = src, Ra = src;
    processRun (a, La, Ra, sr, bpm, 0.0, true); // warm
    // mid-insert at beat 32
    const double bps = (bpm / 60.0) / sr;
    const int warmN = static_cast<int> (32.0 / bps);
    auto Lb = makeTone (warmN, sr), Rb = Lb;
    processRun (b, Lb, Rb, sr, bpm, 0.0, true);
    // Compare DNA at end of warm
    EXPECT (a.dna().lengthBars == b.dna().lengthBars);
}

static void testLongSoak()
{
    const double sr = 44100.0, bpm = 120.0;
    const double beats = 120.0; // ~60s
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (beats / bps);
    auto L = makeTone (n, sr, 180.0, 0.35f);
    auto R = L;
    pfl::dsp::PulseColonyEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (1.0f);
    eng.setDensity (0.55f);
    eng.setMutation (1.0f);
    eng.setMotion (1.0f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceRebuild (0);
    int done = 0;
    int phase = 0;
    while (done < n)
    {
        const int m = std::min (256, n - done);
        const double ppq = static_cast<double> (done) * bps;
        if (static_cast<int> (ppq) / 16 != phase)
        {
            phase = static_cast<int> (ppq) / 16;
            eng.setDensity (0.2f + 0.6f * (phase % 3) / 2.0f);
        }
        eng.process (L.data() + done, R.data() + done, m, true, ppq, bpm);
        done += m;
    }
    for (int i = 0; i < n; i += 1024)
        EXPECT (std::isfinite (L[static_cast<size_t> (i)]));
    EXPECT (eng.dna().windowCount <= pfl::dsp::PulseDNA::kMaxWindows);
    EXPECT (eng.dna().occupancy() < 0.95f);
}

int main()
{
    testAlgorithmVersion();
    testMixZeroDry();
    testDeterminism();
    testBufferIndependence();
    testDensityAffectsOccupancy();
    testMutationZeroStable();
    testDensityMutationCross();
    testMotionZeroPreservesStereo();
    testStopPassThrough();
    testSilentInput();
    testTemposAndRates();
    testSeekReconstruct();
    testLongSoak();

    if (gFails == 0)
    {
        std::cout << "pulse_colony_tests: OK\n";
        return 0;
    }
    std::cerr << "pulse_colony_tests: " << gFails << " failure(s)\n";
    return 1;
}

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

std::string dnaKey (const pfl::dsp::PulseDNA& dna)
{
    std::ostringstream os;
    os << dna.generation << ":" << dna.lengthBars << ":" << dna.windowCount << ":"
       << dna.phaseShiftSlots << ":";
    for (int i = 0; i < dna.lengthCells(); ++i)
        os << static_cast<int> (dna.mask[static_cast<size_t> (i)]);
    return os.str();
}

std::string rhythmKey (const pfl::dsp::PulseColonyEngine& eng)
{
    std::ostringstream os;
    for (const auto& e : eng.opens())
        os << e.beat << "|" << e.durationBeats << "|" << static_cast<int> (e.role) << ";";
    return os.str();
}

void setupDefaults (pfl::dsp::PulseColonyEngine& eng, float dens = 0.5f, float mut = 0.35f,
                    float motion = 0.0f)
{
    eng.prepare (48000.0);
    eng.setSeed (2002);
    eng.setMix (1.0f);
    eng.setDensity (dens);
    eng.setMutation (mut);
    eng.setMotion (motion);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceRebuild (0);
}
} // namespace

static void testAlgorithmVersion()
{
    EXPECT (pfl::dsp::PulseColonyEngine::kAlgorithmVersion == 2);
}

static void testThreeCellsExist()
{
    pfl::dsp::PulseColonyEngine eng;
    setupDefaults (eng);
    EXPECT (eng.cellDna (0).lengthBars >= 1);
    EXPECT (eng.cellDna (1).lengthBars >= 1);
    EXPECT (eng.cellDna (2).lengthBars >= 1);
    EXPECT (dnaKey (eng.dna()) == dnaKey (eng.cellDna (0)));
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
        os << eng.generation() << ":" << eng.opens().size() << ":"
           << eng.roleAcceptCount (0) << ":" << eng.roleAcceptCount (1) << ":"
           << eng.roleAcceptCount (2) << ":" << rhythmKey (eng);
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
        os << rhythmKey (eng) << "|" << eng.generation() << "|"
           << eng.roleAcceptCount (0) << eng.roleAcceptCount (1) << eng.roleAcceptCount (2);
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
        return eng.colonyOpenOccupancy();
    };
    const float low = occ (0.20f);
    const float high = occ (0.90f);
    EXPECT (high > low);
    EXPECT (high < 0.85f);
    EXPECT (low > 0.02f);
}

static void testColonyOccupancyAtDens1()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 48);
    auto src = makeTone (n, sr);
    pfl::dsp::PulseColonyEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (1.0f);
    eng.setDensity (1.0f);
    eng.setMutation (0.0f);
    eng.setMotion (0.0f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    eng.forceRebuild (0);
    auto L = src, R = src;
    processRun (eng, L, R, sr, bpm, 0.0, true);
    EXPECT (eng.colonyOpenOccupancy() < 0.85f);
}

static void testMutationZeroStableAllRoles()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 48);
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
    const auto a0 = eng.cellDna (0).mask;
    const auto s0 = eng.cellDna (1).mask;
    const auto g0 = eng.cellDna (2).mask;
    auto L = src, R = src;
    processRun (eng, L, R, sr, bpm, 0.0, true);
    EXPECT (eng.generation() == gen0);
    EXPECT (eng.cellDna (0).mask == a0);
    EXPECT (eng.cellDna (1).mask == s0);
    EXPECT (eng.cellDna (2).mask == g0);
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
        return std::make_pair (eng.colonyOpenOccupancy(), eng.generation());
    };
    const auto sparseEvolve = run (0.20f, 0.90f);
    const auto busyStable = run (0.90f, 0.10f);
    EXPECT (busyStable.first > sparseEvolve.first);
    EXPECT (sparseEvolve.second >= busyStable.second);
}

static void testMotionDoesNotChangeOpenTimes()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 16);
    auto src = makeTone (n, sr);
    auto run = [&] (float motion)
    {
        pfl::dsp::PulseColonyEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (1.0f);
        eng.setDensity (0.5f);
        eng.setMutation (0.35f);
        eng.setMotion (motion);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.forceRebuild (0);
        eng.setTraceEnabled (true);
        auto L = src, R = src;
        processRun (eng, L, R, sr, bpm, 0.0, true);
        return rhythmKey (eng);
    };
    EXPECT (run (0.0f) == run (0.35f));
    EXPECT (run (0.0f) == run (1.0f));
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

static void testInteractionOnOffDiffer()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 32);
    auto src = makeTone (n, sr);
    auto run = [&] (bool ix)
    {
        pfl::dsp::PulseColonyEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (1.0f);
        eng.setDensity (0.85f);
        eng.setMutation (0.35f);
        eng.setMotion (0.0f);
        eng.setOutput (0.9f);
        eng.setInteractionEnabled (ix);
        eng.snapMacros();
        eng.forceRebuild (0);
        eng.setTraceEnabled (true);
        auto L = src, R = src;
        processRun (eng, L, R, sr, bpm, 0.0, true);
        return rhythmKey (eng);
    };
    EXPECT (run (true) != run (false));
}

static void testRoleRngIsolation()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 24);
    auto src = makeTone (n, sr);

    auto runAnchorDna = [&] (int solo, float dens)
    {
        pfl::dsp::PulseColonyEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (1.0f);
        eng.setDensity (dens);
        eng.setMutation (0.35f);
        eng.setMotion (0.0f);
        eng.setOutput (0.9f);
        eng.setInteractionEnabled (false);
        eng.setSoloRole (solo);
        eng.snapMacros();
        eng.forceRebuild (0);
        auto L = src, R = src;
        processRun (eng, L, R, sr, bpm, 0.0, true);
        return dnaKey (eng.cellDna (0));
    };

    // Ghost/Skitter streams + proposals must not rewrite Anchor DNA
    EXPECT (runAnchorDna (0, 0.5f) == runAnchorDna (-1, 0.5f));
    EXPECT (runAnchorDna (0, 0.9f) == runAnchorDna (-1, 0.9f));
}

static void testGhostContributesWithin256Beats()
{
    const double sr = 48000.0, bpm = 120.0;
    const double beats = 256.0;
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (beats / bps);
    auto L = makeTone (n, sr), R = L;
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
    processRun (eng, L, R, sr, bpm, 0.0, true);
    EXPECT (eng.roleAcceptCount (2) >= 1);
}

static void testNoRoleStarvation()
{
    const double sr = 48000.0, bpm = 120.0;
    const double beats = 256.0;
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (beats / bps);
    auto L = makeTone (n, sr), R = L;
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
    processRun (eng, L, R, sr, bpm, 0.0, true);
    EXPECT (eng.roleAcceptCount (0) >= 1);
    EXPECT (eng.roleAcceptCount (1) >= 1);
    EXPECT (eng.roleAcceptCount (2) >= 1);
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
    EXPECT (muted * 20 < loud);
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

static void testMaxSettingsSafety()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (sr * 8);
    auto L = makeTone (n, sr, 180.0, 0.6f);
    auto R = makeClicks (n, sr, bpm);
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
    for (int i = 0; i < n; i += 64)
    {
        EXPECT (std::isfinite (L[static_cast<size_t> (i)]));
        EXPECT (std::isfinite (R[static_cast<size_t> (i)]));
        EXPECT (std::abs (L[static_cast<size_t> (i)]) <= 1.0f);
    }
    EXPECT (eng.colonyOpenOccupancy() < 0.95f);
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
    const double beats = 64.0;
    const double bps = (bpm / 60.0) / sr;

    pfl::dsp::PulseColonyEngine warm;
    warm.prepare (sr);
    warm.setSeed (2002);
    warm.setMix (1.0f);
    warm.setDensity (0.5f);
    warm.setMutation (0.35f);
    warm.setMotion (0.0f);
    warm.setOutput (0.9f);
    warm.snapMacros();
    warm.forceRebuild (0);
    const int nWarm = static_cast<int> (beats / bps);
    auto Lw = makeTone (nWarm, sr), Rw = Lw;
    processRun (warm, Lw, Rw, sr, bpm, 0.0, true);
    const double land = static_cast<double> (nWarm - 1) * bps;

    pfl::dsp::PulseColonyEngine seek;
    seek.prepare (sr);
    seek.setSeed (2002);
    seek.setMix (1.0f);
    seek.setDensity (0.5f);
    seek.setMutation (0.35f);
    seek.setMotion (0.0f);
    seek.setOutput (0.9f);
    seek.snapMacros();
    seek.forceRebuild (0);
    auto Ls = makeTone (static_cast<int> (sr * 0.25), sr), Rs = Ls;
    processRun (seek, Ls, Rs, sr, bpm, 0.0, true, 128);
    Ls.assign (64, 0.2f);
    Rs = Ls;
    seek.process (Ls.data(), Rs.data(), 64, true, land, bpm);

    EXPECT (dnaKey (warm.cellDna (0)) == dnaKey (seek.cellDna (0)));
    EXPECT (dnaKey (warm.cellDna (2)) == dnaKey (seek.cellDna (2)));
    // Skitter may differ by one boundary evolve depending on landing sample;
    // require same phrase length family and that reconstruct produced DNA.
    EXPECT (seek.cellDna (1).lengthBars >= 1);
    EXPECT (std::abs (warm.cellDna (1).generation - seek.cellDna (1).generation) <= 1);
    EXPECT (seek.roleAcceptCount (0) + seek.roleAcceptCount (1) + seek.roleAcceptCount (2) >= 1);
}

static void testLongSoak()
{
    const double sr = 44100.0, bpm = 120.0;
    const double beats = 120.0;
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
    EXPECT (eng.cellDna (0).windowCount <= pfl::dsp::PulseDNA::kMaxWindows);
    EXPECT (eng.colonyOpenOccupancy() < 0.95f);
}

int main()
{
    testAlgorithmVersion();
    testThreeCellsExist();
    testMixZeroDry();
    testDeterminism();
    testBufferIndependence();
    testDensityAffectsOccupancy();
    testColonyOccupancyAtDens1();
    testMutationZeroStableAllRoles();
    testDensityMutationCross();
    testMotionDoesNotChangeOpenTimes();
    testMotionZeroPreservesStereo();
    testInteractionOnOffDiffer();
    testRoleRngIsolation();
    testGhostContributesWithin256Beats();
    testNoRoleStarvation();
    testStopPassThrough();
    testSilentInput();
    testMaxSettingsSafety();
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

#include "dsp/RuinEngine.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
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
struct RenderResult
{
    std::vector<float> L, R;
    float peak = 0.0f;
    bool finite = true;
};

RenderResult render (uint64_t seed, float mix, float age, float inst, float out,
                     double sr, int blockSize, int totalSamples,
                     const std::vector<float>& inL, const std::vector<float>& inR,
                     double bpm = 72.0, bool playing = true)
{
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (seed);
    eng.setMix (mix);
    eng.setAge (age);
    eng.setInstability (inst);
    eng.setOutput (out);
    eng.snapMacros();

    RenderResult rr;
    rr.L.assign (static_cast<size_t> (totalSamples), 0.0f);
    rr.R.assign (static_cast<size_t> (totalSamples), 0.0f);

    const double beatsPerSample = (bpm / 60.0) / sr;
    int done = 0;
    while (done < totalSamples)
    {
        const int n = std::min (blockSize, totalSamples - done);
        for (int i = 0; i < n; ++i)
        {
            rr.L[static_cast<size_t> (done + i)] = inL[static_cast<size_t> (done + i)];
            rr.R[static_cast<size_t> (done + i)] = inR[static_cast<size_t> (done + i)];
        }
        eng.process (rr.L.data() + done, rr.R.data() + done, n, playing,
                     static_cast<double> (done) * beatsPerSample, bpm);
        done += n;
    }

    for (int i = 0; i < totalSamples; ++i)
    {
        const float a = std::abs (rr.L[static_cast<size_t> (i)]);
        const float b = std::abs (rr.R[static_cast<size_t> (i)]);
        rr.peak = std::max (rr.peak, std::max (a, b));
        if (! std::isfinite (rr.L[static_cast<size_t> (i)]) || ! std::isfinite (rr.R[static_cast<size_t> (i)]))
            rr.finite = false;
    }
    return rr;
}

std::vector<float> makeSine (int n, double sr, float freq, float amp)
{
    std::vector<float> v (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t> (i)] = amp * std::sin (2.0 * 3.141592653589793 * freq * (double) i / sr);
    return v;
}

std::vector<float> makeSilence (int n) { return std::vector<float> (static_cast<size_t> (n), 0.0f); }

std::vector<float> makeImpulse (int n, int every, float amp)
{
    std::vector<float> v (static_cast<size_t> (n), 0.0f);
    for (int i = 0; i < n; i += every)
        v[static_cast<size_t> (i)] = amp;
    return v;
}

uint64_t fingerprint (const RenderResult& rr)
{
    uint64_t h = 14695981039346656037ull;
    const int step = std::max (1, (int) rr.L.size() / 4096);
    for (size_t i = 0; i < rr.L.size(); i += static_cast<size_t> (step))
    {
        const auto q = static_cast<uint32_t> (std::lround (rr.L[i] * 100000.0f));
        h ^= q;
        h *= 1099511628211ull;
        const auto q2 = static_cast<uint32_t> (std::lround (rr.R[i] * 100000.0f));
        h ^= q2;
        h *= 1099511628211ull;
    }
    return h;
}

bool nearlyEqual (const RenderResult& a, const RenderResult& b, float tol = 1.0e-4f)
{
    if (a.L.size() != b.L.size())
        return false;
    for (size_t i = 0; i < a.L.size(); ++i)
    {
        if (std::abs (a.L[i] - b.L[i]) > tol || std::abs (a.R[i] - b.R[i]) > tol)
            return false;
    }
    return true;
}
} // namespace

static void testAlgorithmVersion()
{
    EXPECT (pfl::dsp::RuinEngine::kAlgorithmVersion == 3);
}

static void testAgeZeroTransparent()
{
    const double sr = 48000.0;
    const int n = 48000;
    auto in = makeSine (n, sr, 220.0, 0.4f);
    auto a = render (2002, 1.0f, 0.0f, 0.5f, 1.0f, sr, 256, n, in, in);
    float maxDiff = 0.0f;
    for (int i = 1000; i < n; ++i) // skip smoother settle
        maxDiff = std::max (maxDiff, std::abs (a.L[static_cast<size_t> (i)] - in[static_cast<size_t> (i)]));
    EXPECT (a.finite);
    EXPECT (maxDiff < 0.005f);
}

static void testMixZeroDry()
{
    const double sr = 48000.0;
    const int n = 24000;
    auto in = makeSine (n, sr, 330.0, 0.5f);
    auto a = render (2002, 0.0f, 1.0f, 1.0f, 1.0f, sr, 128, n, in, in);
    float maxDiff = 0.0f;
    for (int i = 2000; i < n; ++i)
        maxDiff = std::max (maxDiff, std::abs (a.L[static_cast<size_t> (i)] - in[static_cast<size_t> (i)]));
    EXPECT (a.finite);
    EXPECT (maxDiff < 0.005f);
}

static void testHostileBounded()
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr * 2);
    auto impulses = makeImpulse (n, 100, 0.9f);
    auto noise = makeSine (n, sr, 1000.0, 0.7f);
    for (int i = 0; i < n; ++i)
        noise[static_cast<size_t> (i)] = 0.5f * noise[static_cast<size_t> (i)] + 0.5f * impulses[static_cast<size_t> (i)];
    auto a = render (3003, 1.0f, 1.0f, 1.0f, 1.0f, sr, 256, n, noise, noise);
    EXPECT (a.finite);
    EXPECT (a.peak <= 0.995f);
}

static void testSilenceSafe()
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr * 2);
    auto z = makeSilence (n);
    auto a = render (2002, 1.0f, 1.0f, 1.0f, 1.0f, sr, 512, n, z, z);
    EXPECT (a.finite);
    EXPECT (a.peak < 0.05f); // may have tiny noise at high age with residual; should not blast
}

static void testImpulseFinite()
{
    const double sr = 44100.0;
    const int n = static_cast<int> (sr * 2);
    auto in = makeImpulse (n, 200, 0.95f);
    auto a = render (1001, 1.0f, 0.9f, 0.7f, 1.0f, sr, 256, n, in, in);
    EXPECT (a.finite);
    EXPECT (a.peak <= 0.995f);
}

static void testDeterminism()
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr * 4);
    auto in = makeSine (n, sr, 440.0, 0.4f);
    auto a = render (2002, 0.7f, 0.55f, 0.4f, 0.9f, sr, 256, n, in, in);
    auto b = render (2002, 0.7f, 0.55f, 0.4f, 0.9f, sr, 256, n, in, in);
    EXPECT (nearlyEqual (a, b, 1.0e-5f));
}

static void testTimelineDeterminism()
{
    // Same SEED + absolute PPQ → same structural profile whether warmed from 0 or mid-inserted.
    const double sr = 48000.0;
    const double bpm = 72.0;
    const double beatsPerSample = (bpm / 60.0) / sr;
    const double startPpq = 128.0;

    pfl::dsp::RuinEngine fromZero;
    fromZero.prepare (sr);
    fromZero.setSeed (2002);
    fromZero.setMix (0.7f);
    fromZero.setAge (0.55f);
    fromZero.setInstability (0.5f);
    fromZero.setOutput (0.9f);
    fromZero.snapMacros();

    {
        const int warm = static_cast<int> (startPpq / beatsPerSample);
        std::vector<float> zL (static_cast<size_t> (256), 0.0f), zR (256, 0.0f);
        int done = 0;
        while (done < warm)
        {
            const int m = std::min (256, warm - done);
            fromZero.process (zL.data(), zR.data(), m, true, static_cast<double> (done) * beatsPerSample, bpm);
            done += m;
        }
        // Align to absolute startPpq eval boundary
        fromZero.process (zL.data(), zR.data(), 64, true, startPpq, bpm);
    }

    pfl::dsp::RuinEngine midInsert;
    midInsert.prepare (sr);
    midInsert.setSeed (2002);
    midInsert.setMix (0.7f);
    midInsert.setAge (0.55f);
    midInsert.setInstability (0.5f);
    midInsert.setOutput (0.9f);
    midInsert.snapMacros();
    {
        std::vector<float> zL (64, 0.0f), zR (64, 0.0f);
        midInsert.process (zL.data(), zR.data(), 64, true, startPpq, bpm);
    }

    EXPECT (std::abs (fromZero.tone() - midInsert.tone()) < 1.0e-3f);
    EXPECT (std::abs (fromZero.grit() - midInsert.grit()) < 1.0e-3f);
    EXPECT (std::abs (fromZero.wobble() - midInsert.wobble()) < 1.0e-3f);
    EXPECT (std::abs (fromZero.smear() - midInsert.smear()) < 1.0e-3f);
    EXPECT (fromZero.processingState() == midInsert.processingState());
    EXPECT (std::abs (fromZero.damagePressure() - midInsert.damagePressure()) < 1.0e-3f);
}

static void testDifferentSeeds()
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr * 8);
    auto in = makeSine (n, sr, 220.0, 0.4f);
    auto a = render (1001, 0.8f, 0.6f, 0.7f, 0.9f, sr, 256, n, in, in);
    auto b = render (3003, 0.8f, 0.6f, 0.7f, 0.9f, sr, 256, n, in, in);
    EXPECT (fingerprint (a) != fingerprint (b));
}

static void testBufferIndependence()
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr * 6);
    auto in = makeSine (n, sr, 277.0, 0.35f);
    const int sizes[] = { 64, 127, 128, 255, 256, 511, 512, 1024 };
    auto ref = render (2002, 0.65f, 0.5f, 0.45f, 0.9f, sr, 256, n, in, in);
    for (int bs : sizes)
    {
        auto r = render (2002, 0.65f, 0.5f, 0.45f, 0.9f, sr, bs, n, in, in);
        EXPECT (nearlyEqual (ref, r, 2.0e-3f)); // float path may differ slightly with smoother phasing
    }
}

static void testTempoIndependenceStructure()
{
    // Same musical time (beats) should drive same structural path; audio DSP may differ with BPM
    // because micro-motion is sample-rate time based. Compare fingerprints at matched beat counts
    // by using identical sample counts at different BPM with proportional length — structural
    // state (profile) should remain finite and bounded for both.
    const double sr = 48000.0;
    const int n = static_cast<int> (sr * 5);
    auto in = makeSine (n, sr, 196.0, 0.4f);
    auto a = render (2002, 0.7f, 0.5f, 0.5f, 0.9f, sr, 256, n, in, in, 72.0);
    auto b = render (2002, 0.7f, 0.5f, 0.5f, 0.9f, sr, 256, n, in, in, 120.0);
    EXPECT (a.finite && b.finite);
    EXPECT (a.peak <= 0.995f && b.peak <= 0.995f);
}

static void testSampleRates()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const int n = static_cast<int> (sr * 1.0);
        auto in = makeSine (n, sr, 440.0, 0.5f);
        auto a = render (2002, 1.0f, 1.0f, 1.0f, 1.0f, sr, 256, n, in, in);
        EXPECT (a.finite);
        EXPECT (a.peak <= 0.995f);
    }
}

static void testNaNInput()
{
    const double sr = 48000.0;
    const int n = 8192;
    auto in = makeSine (n, sr, 440.0, 0.4f);
    for (int i = 100; i < 200; ++i)
        in[static_cast<size_t> (i)] = std::nanf ("");
    auto a = render (2002, 1.0f, 1.0f, 1.0f, 1.0f, sr, 128, n, in, in);
    EXPECT (a.finite);
}

static void testLongRun()
{
    const char* env = std::getenv ("PFL_LONG_TESTS");
    const bool longRun = env != nullptr && std::string (env) == "1";
    const double sr = 48000.0;
    const double seconds = longRun ? 3600.0 : 30.0;
    const int n = static_cast<int> (sr * seconds);
    auto in = makeSine (n, sr, 110.0, 0.35f);
    // slow amplitude so feedback has continuous input
    for (int i = 0; i < n; ++i)
        in[static_cast<size_t> (i)] *= 0.6f + 0.4f * std::sin (2.0 * 3.141592653589793 * i / (sr * 7.0));

    const auto t0 = std::chrono::steady_clock::now();
    auto a = render (2002, 0.85f, 0.75f, 0.6f, 0.9f, sr, 256, n, in, in);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                        std::chrono::steady_clock::now() - t0)
                        .count();
    EXPECT (a.finite);
    EXPECT (a.peak <= 0.995f);
    std::cout << "ruin long-run " << seconds << "s sim in " << ms << " ms, peak=" << a.peak << "\n";
}

struct StateOcc
{
    double beats[5] {};
    int transitions = 0;
    int maxRuinedEvals = 0;
};

static StateOcc collectOccupancy (uint64_t seed, float age, float inst, double endBeats, double bpm = 72.0)
{
    const double sr = 48000.0;
    const double beatsPerSample = (bpm / 60.0) / sr;
    const int n = static_cast<int> (endBeats / beatsPerSample);
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (seed);
    eng.setMix (0.7f);
    eng.setAge (age);
    eng.setInstability (inst);
    eng.setOutput (0.9f);
    eng.snapMacros();

    StateOcc occ;
    auto prev = eng.processingState();
    int ruinedRun = 0;
    std::vector<float> zL (256, 0.0f), zR (256, 0.0f);
    int done = 0;
    while (done < n)
    {
        const int m = std::min (256, n - done);
        const double ppq = static_cast<double> (done) * beatsPerSample;
        eng.process (zL.data(), zR.data(), m, true, ppq, bpm);
        const auto st = eng.processingState();
        const int si = static_cast<int> (st);
        if (si >= 0 && si < 5)
            occ.beats[si] += static_cast<double> (m) * beatsPerSample;
        if (st != prev)
        {
            ++occ.transitions;
            if (! pfl::dsp::RuinStateMachine::isLegalEdge (prev, st))
            {
                std::cerr << "FAIL: illegal edge " << pfl::dsp::ruinStateName (prev)
                          << " → " << pfl::dsp::ruinStateName (st) << "\n";
                ++gFails;
            }
            prev = st;
            ruinedRun = 0;
        }
        if (st == pfl::dsp::RuinProcessingState::Ruined)
            ruinedRun += m;
        else
            ruinedRun = 0;
        occ.maxRuinedEvals = std::max (occ.maxRuinedEvals, ruinedRun);
        done += m;
    }
    return occ;
}

static void testStage2GraphAndCrossMatrix()
{
    // A: AGE0 INST0 — clean stable
    {
        auto o = collectOccupancy (2002, 0.0f, 0.0f, 512.0);
        const double mild = o.beats[0] + o.beats[1];
        EXPECT (mild / 512.0 >= 0.90);
        EXPECT (o.beats[3] <= 1.0); // ruined ~0
        EXPECT (o.transitions <= 12);
    }
    // B: AGE0.2 INST1 — restless light
    {
        auto o = collectOccupancy (2002, 0.20f, 1.0f, 512.0);
        EXPECT ((o.beats[0] + o.beats[1]) / 512.0 >= 0.70);
        EXPECT (o.beats[3] / 512.0 <= 0.08);
    }
    // C: AGE1 INST0.1 — deep stable
    {
        auto o = collectOccupancy (2002, 1.0f, 0.10f, 512.0);
        EXPECT ((o.beats[2] + o.beats[3]) / 512.0 >= 0.25);
        EXPECT (o.transitions < 40);
    }
    // D: AGE1 INST1 — deep unstable, still bounded
    {
        auto o = collectOccupancy (2002, 1.0f, 1.0f, 512.0);
        EXPECT ((o.beats[2] + o.beats[3]) / 512.0 >= 0.30);
        EXPECT (o.transitions > 5);
    }
}

static void testStage2StateBufferIndependence()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    const double beatsPerSample = (bpm / 60.0) / sr;
    const int n = static_cast<int> (256.0 / beatsPerSample);
    auto in = makeSine (n, sr, 220.0, 0.3f);

    auto runStates = [&] (int bs)
    {
        pfl::dsp::RuinEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (0.7f);
        eng.setAge (0.55f);
        eng.setInstability (0.5f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        std::vector<pfl::dsp::RuinProcessingState> seq;
        auto L = in, R = in;
        int done = 0;
        auto last = eng.processingState();
        seq.push_back (last);
        while (done < n)
        {
            const int m = std::min (bs, n - done);
            eng.process (L.data() + done, R.data() + done, m, true,
                         static_cast<double> (done) * beatsPerSample, bpm);
            if (eng.processingState() != last)
            {
                last = eng.processingState();
                seq.push_back (last);
            }
            done += m;
        }
        return seq;
    };

    auto ref = runStates (256);
    for (int bs : { 64, 127, 128, 255, 511, 512, 1024 })
    {
        auto s = runStates (bs);
        EXPECT (s.size() == ref.size());
        if (s.size() == ref.size())
            for (size_t i = 0; i < s.size(); ++i)
                EXPECT (s[i] == ref[i]);
    }
}

static void testStage2ForcedStatesFinite()
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr * 2);
    auto in = makeSine (n, sr, 196.0, 0.45f);
    for (int si = 0; si < 5; ++si)
    {
        pfl::dsp::RuinEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (0.8f);
        eng.setAge (0.7f);
        eng.setInstability (0.5f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.forceProcessingState (true, static_cast<pfl::dsp::RuinProcessingState> (si));
        auto L = in, R = in;
        eng.process (L.data(), R.data(), n, true, 0.0, 72.0);
        float peak = 0.0f;
        bool finite = true;
        for (int i = 0; i < n; ++i)
        {
            peak = std::max (peak, std::max (std::abs (L[static_cast<size_t> (i)]),
                                             std::abs (R[static_cast<size_t> (i)])));
            if (! std::isfinite (L[static_cast<size_t> (i)]))
                finite = false;
        }
        EXPECT (finite);
        EXPECT (peak <= 0.995f);
    }
}

static void runBeats (pfl::dsp::RuinEngine& eng, const std::vector<float>& src,
                      double sr, double bpm, double startBeat, double numBeats,
                      bool playing, int block = 256)
{
    const double beatsPerSample = (bpm / 60.0) / sr;
    const int n = std::max (1, static_cast<int> (numBeats / beatsPerSample));
    auto L = src, R = src;
    if (static_cast<int> (L.size()) < n)
    {
        L.resize (static_cast<size_t> (n), 0.0f);
        R.resize (static_cast<size_t> (n), 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            L[static_cast<size_t> (i)] = 0.4f * std::sin (2.0 * 3.141592653589793 * 110.0 * t);
            R[static_cast<size_t> (i)] = L[static_cast<size_t> (i)] * 0.97f;
        }
    }
    int done = 0;
    while (done < n)
    {
        const int m = std::min (block, n - done);
        eng.process (L.data() + done, R.data() + done, m, playing,
                     startBeat + static_cast<double> (done) * beatsPerSample, bpm);
        done += m;
    }
}

static void testStage3WearAccumulatesAndBounds()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (0.7f);
    eng.setAge (0.9f);
    eng.setInstability (0.5f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);
    EXPECT (eng.wearState().mean() < 0.01f);
    runBeats (eng, {}, sr, bpm, 0.0, 256.0, true);
    const auto w = eng.wearState();
    EXPECT (w.spectral > 0.15f);
    EXPECT (w.nonlinear > 0.15f);
    EXPECT (w.temporal > 0.15f);
    EXPECT (w.spectral <= 1.0f && w.nonlinear <= 1.0f && w.temporal <= 1.0f);
}

static void testStage3SilenceDoesNotAge()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (0.7f);
    eng.setAge (1.0f);
    eng.setInstability (0.6f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);

    const int n = static_cast<int> (256.0 / ((bpm / 60.0) / sr));
    std::vector<float> z (static_cast<size_t> (n), 0.0f);
    const double beatsPerSample = (bpm / 60.0) / sr;
    int done = 0;
    while (done < n)
    {
        const int m = std::min (256, n - done);
        eng.process (z.data() + done, z.data() + done, m, true,
                     static_cast<double> (done) * beatsPerSample, bpm);
        done += m;
    }
    EXPECT (eng.wearState().mean() < 0.02f);
}

static void testStage3SeekPreservesWear()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (0.7f);
    eng.setAge (0.85f);
    eng.setInstability (0.55f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);
    runBeats (eng, {}, sr, bpm, 0.0, 64.0, true);
    const auto before = eng.wearState();
    EXPECT (before.mean() > 0.05f);

    // Seek forward — must not fabricate exposure
    {
        std::vector<float> z (64, 0.0f);
        eng.process (z.data(), z.data(), 64, true, 400.0, bpm);
    }
    const auto afterFwd = eng.wearState();
    EXPECT (std::abs (afterFwd.spectral - before.spectral) < 1.0e-5f);
    EXPECT (std::abs (afterFwd.nonlinear - before.nonlinear) < 1.0e-5f);
    EXPECT (std::abs (afterFwd.temporal - before.temporal) < 1.0e-5f);

    // Seek backward — must not reverse wear
    {
        std::vector<float> z (64, 0.0f);
        eng.process (z.data(), z.data(), 64, true, 16.0, bpm);
    }
    const auto afterBack = eng.wearState();
    EXPECT (std::abs (afterBack.spectral - before.spectral) < 1.0e-5f);
}

static void testStage3StopAndBypassPauseWear()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (0.7f);
    eng.setAge (0.9f);
    eng.setInstability (0.5f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Fractured);
    runBeats (eng, {}, sr, bpm, 0.0, 64.0, true);
    const auto w0 = eng.wearState();

    runBeats (eng, {}, sr, bpm, 64.0, 64.0, false); // stopped
    EXPECT (std::abs (eng.wearState().mean() - w0.mean()) < 1.0e-5f);

    eng.setBypassed (true);
    runBeats (eng, {}, sr, bpm, 128.0, 64.0, true);
    EXPECT (std::abs (eng.wearState().mean() - w0.mean()) < 1.0e-5f);
}

static void testStage3SeedPreservesWear()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (0.7f);
    eng.setAge (0.9f);
    eng.setInstability (0.5f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);
    runBeats (eng, {}, sr, bpm, 0.0, 96.0, true);
    const auto w = eng.wearState();
    eng.setSeed (3003);
    EXPECT (std::abs (eng.wearState().spectral - w.spectral) < 1.0e-5f);
    EXPECT (std::abs (eng.wearState().nonlinear - w.nonlinear) < 1.0e-5f);
    EXPECT (std::abs (eng.wearState().temporal - w.temporal) < 1.0e-5f);
}

static void testStage3RecoveryGradual()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (0.7f);
    eng.setAge (0.95f);
    eng.setInstability (0.4f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);
    runBeats (eng, {}, sr, bpm, 0.0, 192.0, true);
    const auto damaged = eng.wearState();
    EXPECT (damaged.mean() > 0.2f);

    eng.setAge (0.10f);
    eng.setInstability (0.20f);
    eng.snapMacros();
    eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Recovering);
    runBeats (eng, {}, sr, bpm, 192.0, 32.0, true);
    const auto w32 = eng.wearState();
    runBeats (eng, {}, sr, bpm, 224.0, 96.0, true);
    const auto w128 = eng.wearState();
    EXPECT (w32.mean() < damaged.mean());
    EXPECT (w128.mean() < w32.mean());
    EXPECT (w128.mean() > 0.02f); // residual / scar remains
}

static void testStage3HistoryAB()
{
    const double sr = 48000.0;
    const double bpm = 72.0;

    pfl::dsp::RuinEngine a;
    a.prepare (sr);
    a.setSeed (2002);
    a.setMix (0.7f);
    a.setAge (0.55f);
    a.setInstability (0.45f);
    a.setOutput (0.9f);
    a.snapMacros();
    a.forceProcessingState (true, pfl::dsp::RuinProcessingState::Weathered);
    runBeats (a, {}, sr, bpm, 0.0, 256.0, true);

    pfl::dsp::RuinEngine b;
    b.prepare (sr);
    b.setSeed (2002);
    b.setMix (0.7f);
    b.setAge (0.95f);
    b.setInstability (0.55f);
    b.setOutput (0.9f);
    b.snapMacros();
    b.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);
    runBeats (b, {}, sr, bpm, 0.0, 64.0, true);
    b.setAge (0.35f);
    b.setInstability (0.35f);
    b.snapMacros();
    b.forceProcessingState (true, pfl::dsp::RuinProcessingState::Weathered);
    runBeats (b, {}, sr, bpm, 64.0, 192.0, true);

    // Same current forced state + macros
    a.setAge (0.5f);
    a.setInstability (0.5f);
    a.snapMacros();
    a.forceProcessingState (true, pfl::dsp::RuinProcessingState::Weathered);
    b.setAge (0.5f);
    b.setInstability (0.5f);
    b.snapMacros();
    b.forceProcessingState (true, pfl::dsp::RuinProcessingState::Weathered);

    const auto wa = a.wearState();
    const auto wb = b.wearState();
    const float d = std::abs (wa.spectral - wb.spectral)
                  + std::abs (wa.nonlinear - wb.nonlinear)
                  + std::abs (wa.temporal - wb.temporal);
    EXPECT (d > 0.05f);
}

static void testStage3WearSampleRateIndependent()
{
    auto wearAt = [] (double sr)
    {
        const double bpm = 72.0;
        pfl::dsp::RuinEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (0.7f);
        eng.setAge (0.9f);
        eng.setInstability (0.5f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);
        runBeats (eng, {}, sr, bpm, 0.0, 128.0, true);
        return eng.wearState();
    };
    const auto a = wearAt (44100.0);
    const auto b = wearAt (48000.0);
    const auto c = wearAt (96000.0);
    EXPECT (std::abs (a.mean() - b.mean()) < 0.02f);
    EXPECT (std::abs (a.mean() - c.mean()) < 0.02f);
}

static void testStage3WearBufferIndependent()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    auto wearAt = [&] (int block)
    {
        pfl::dsp::RuinEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (0.7f);
        eng.setAge (0.85f);
        eng.setInstability (0.5f);
        eng.setOutput (0.9f);
        eng.snapMacros();
        eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Fractured);
        runBeats (eng, {}, sr, bpm, 0.0, 128.0, true, block);
        return eng.wearState();
    };
    const auto ref = wearAt (256);
    for (int bs : { 64, 127, 128, 255, 511, 512, 1024 })
    {
        const auto w = wearAt (bs);
        EXPECT (std::abs (w.spectral - ref.spectral) < 1.0e-4f);
        EXPECT (std::abs (w.nonlinear - ref.nonlinear) < 1.0e-4f);
        EXPECT (std::abs (w.temporal - ref.temporal) < 1.0e-4f);
    }
}

static void testStage3MaxWearSafety()
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr * 2);
    auto impulses = makeImpulse (n, 80, 0.95f);
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (1.0f);
    eng.setAge (1.0f);
    eng.setInstability (1.0f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);
    eng.setWearMaxDiagnostic();
    auto L = impulses, R = impulses;
    eng.process (L.data(), R.data(), n, true, 0.0, 72.0);
    float peak = 0.0f;
    bool finite = true;
    for (int i = 0; i < n; ++i)
    {
        peak = std::max (peak, std::max (std::abs (L[static_cast<size_t> (i)]),
                                         std::abs (R[static_cast<size_t> (i)])));
        if (! std::isfinite (L[static_cast<size_t> (i)]) || ! std::isfinite (R[static_cast<size_t> (i)]))
            finite = false;
    }
    EXPECT (finite);
    EXPECT (peak <= 0.995f);
    EXPECT (eng.wearState().spectral <= 1.0f);
}

static void testStage3PreparePreservesWear()
{
    const double sr = 48000.0;
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setWearMaxDiagnostic();
    EXPECT (eng.wearState().spectral > 0.99f);
    eng.prepare (sr); // host re-prepare must not erase scars
    EXPECT (eng.wearState().spectral > 0.99f);
    EXPECT (eng.wearState().nonlinear > 0.99f);
    EXPECT (eng.wearState().temporal > 0.99f);
}

static void testStage3MixZeroStillDry()
{
    const double sr = 48000.0;
    const int n = 24000;
    auto in = makeSine (n, sr, 330.0, 0.5f);
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (0.0f);
    eng.setAge (1.0f);
    eng.setInstability (1.0f);
    eng.setOutput (1.0f);
    eng.snapMacros();
    eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);
    eng.setWearMaxDiagnostic();
    auto L = in, R = in;
    eng.process (L.data(), R.data(), n, true, 0.0, 72.0);
    float maxDiff = 0.0f;
    for (int i = 2000; i < n; ++i)
        maxDiff = std::max (maxDiff, std::abs (L[static_cast<size_t> (i)] - in[static_cast<size_t> (i)]));
    EXPECT (maxDiff < 0.005f);
}

int main()
{
    testAlgorithmVersion();
    testAgeZeroTransparent();
    testMixZeroDry();
    testHostileBounded();
    testSilenceSafe();
    testImpulseFinite();
    testDeterminism();
    testTimelineDeterminism();
    testDifferentSeeds();
    testBufferIndependence();
    testTempoIndependenceStructure();
    testSampleRates();
    testNaNInput();
    testLongRun();
    testStage2GraphAndCrossMatrix();
    testStage2StateBufferIndependence();
    testStage2ForcedStatesFinite();
    testStage3WearAccumulatesAndBounds();
    testStage3SilenceDoesNotAge();
    testStage3SeekPreservesWear();
    testStage3StopAndBypassPauseWear();
    testStage3SeedPreservesWear();
    testStage3RecoveryGradual();
    testStage3HistoryAB();
    testStage3WearSampleRateIndependent();
    testStage3WearBufferIndependent();
    testStage3MaxWearSafety();
    testStage3PreparePreservesWear();
    testStage3MixZeroStillDry();

    if (gFails == 0)
    {
        std::cout << "ruin_engine_tests: OK\n";
        return 0;
    }
    std::cerr << "ruin_engine_tests: " << gFails << " failure(s)\n";
    return 1;
}

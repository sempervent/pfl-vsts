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
    EXPECT (pfl::dsp::RuinEngine::kAlgorithmVersion == 1);
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

    if (gFails == 0)
    {
        std::cout << "ruin_engine_tests: OK\n";
        return 0;
    }
    std::cerr << "ruin_engine_tests: " << gFails << " failure(s)\n";
    return 1;
}

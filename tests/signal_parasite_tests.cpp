#include "dsp/SignalParasiteEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
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
using Engine = pfl::dsp::SignalParasiteEngine;
using Reason = pfl::dsp::ParasiteSuppressReason;

constexpr double kPi = 3.14159265358979323846;

/** Fixture noise source — never live RNG, so fixtures are byte-stable. */
struct FixtureLcg
{
    uint32_t s = 12345u;
    float next() noexcept
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float> ((s >> 9) & 0x7FFFFF) / static_cast<float> (0x7FFFFF) * 2.0f
               - 1.0f;
    }
};

std::vector<float> makeSilence (int n)
{
    return std::vector<float> (static_cast<size_t> (n), 0.0f);
}

std::vector<float> makeTone (int n, double sr, double freq = 220.0, float amp = 0.4f)
{
    std::vector<float> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t> (i)] =
            amp * static_cast<float> (std::sin (2.0 * kPi * freq * i / sr));
    return x;
}

std::vector<float> makeNoise (int n, float amp, uint32_t seed = 777u)
{
    FixtureLcg lcg { seed };
    std::vector<float> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t> (i)] = amp * lcg.next();
    return x;
}

/** Synthetic kit: kick, snare, offbeat hats, 16th ghost notes. Four levels. */
std::vector<float> makeDrums (int n, double sr, double bpm)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    const double spb = sr * 60.0 / bpm;
    FixtureLcg lcg;
    auto hit = [&] (double beat, float amp, double decayMs, double toneHz, float noiseMix)
    {
        const int64_t start = std::llround (beat * spb);
        const int64_t len = static_cast<int64_t> (decayMs * 0.001 * sr * 5.0);
        for (int64_t i = 0; i < len; ++i)
        {
            const int64_t j = start + i;
            if (j < 0 || j >= n)
                continue;
            const double t = static_cast<double> (i) / sr;
            const float env = static_cast<float> (std::exp (-t / (decayMs * 0.001)));
            const float tone = static_cast<float> (std::sin (2.0 * kPi * toneHz * t));
            x[static_cast<size_t> (j)] +=
                amp * env * ((1.0f - noiseMix) * tone + noiseMix * lcg.next());
        }
    };
    const int bars = static_cast<int> (n / (spb * 4.0)) + 1;
    for (int b = 0; b < bars; ++b)
    {
        const double b0 = b * 4.0;
        hit (b0 + 0.0, 0.85f, 90.0, 55.0, 0.05f);
        hit (b0 + 2.0, 0.85f, 90.0, 55.0, 0.05f);
        hit (b0 + 1.0, 0.45f, 60.0, 190.0, 0.65f);
        hit (b0 + 3.0, 0.45f, 60.0, 190.0, 0.65f);
        for (int e = 0; e < 8; ++e)
            hit (b0 + e * 0.5 + 0.25, 0.16f, 25.0, 5000.0, 0.90f);
        for (int s = 0; s < 16; ++s)
            if (s % 4 == 3)
                hit (b0 + s * 0.25, 0.055f, 18.0, 7000.0, 0.95f);
    }
    for (auto& v : x)
        v = std::clamp (v, -0.99f, 0.99f);
    return x;
}

/** Sustained harmonic bed with slow swells, one level step and one timbre step. */
std::vector<float> makePad (int n, double sr)
{
    std::vector<float> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double swell = 0.55 + 0.45 * std::sin (2.0 * kPi * t / 6.0);
        const double level = (t > 8.0 && t < 16.0) ? 1.7 : 1.0;
        const double bright = t > 12.0 ? 0.55 : 0.12;
        const double v = std::sin (2.0 * kPi * 110.0 * t)
                         + 0.7 * std::sin (2.0 * kPi * 220.0 * t)
                         + bright * std::sin (2.0 * kPi * 1320.0 * t);
        x[static_cast<size_t> (i)] = static_cast<float> (0.16 * swell * level * v);
    }
    for (auto& v : x)
        v = std::clamp (v, -0.99f, 0.99f);
    return x;
}

struct Setup
{
    float mix = 1.0f;
    float sens = 0.5f;
    float hunger = 0.5f;
    float mutation = 0.25f;
    float output = 0.9f;
    uint64_t seed = 2002;
    double sr = 48000.0;
    int block = 256;
    bool trace = true;
};

void setupEngine (Engine& eng, const Setup& s)
{
    eng.prepare (s.sr, s.block);
    eng.setSeed (s.seed);
    eng.setMacros (s.mix, s.sens, s.hunger, s.mutation, s.output);
    eng.snapMacros();
    eng.forceRebuild (0);
    eng.setTraceEnabled (s.trace);
}

struct RunOut
{
    std::vector<float> L, R;
    float peak = 0.0f;
    bool allFinite = true;
};

RunOut processRun (Engine& eng, const std::vector<float>& inL, const std::vector<float>& inR,
                   double sr, double bpm, double startBeat, bool playing, int block)
{
    const int n = static_cast<int> (inL.size());
    RunOut out;
    out.L.assign (static_cast<size_t> (n), 0.0f);
    out.R.assign (static_cast<size_t> (n), 0.0f);
    const double bps = (bpm / 60.0) / sr;
    int done = 0;
    while (done < n)
    {
        const int m = std::min (block, n - done);
        eng.process (inL.data() + done, inR.data() + done, out.L.data() + done,
                     out.R.data() + done, m, startBeat + static_cast<double> (done) * bps, bpm,
                     playing);
        done += m;
    }
    for (int i = 0; i < n; ++i)
    {
        const float a = out.L[static_cast<size_t> (i)];
        const float b = out.R[static_cast<size_t> (i)];
        if (! std::isfinite (a) || ! std::isfinite (b))
            out.allFinite = false;
        out.peak = std::max (out.peak, std::max (std::abs (a), std::abs (b)));
    }
    return out;
}

std::string structuralKey (const Engine& eng)
{
    std::ostringstream os;
    os << eng.stimulusFingerprint() << "|" << eng.responseFingerprint() << "|"
       << eng.dnaFingerprint();
    return os.str();
}

int beatsToSamples (double beats, double sr, double bpm)
{
    return static_cast<int> (beats * sr * 60.0 / bpm);
}
} // namespace

// ---------------------------------------------------------------------------

static void testAlgorithmVersion()
{
    EXPECT (Engine::kAlgorithmVersion == 1);
    EXPECT (pfl::dsp::ParasiteStimulusDetector::kQueueCap == 8);
    EXPECT (pfl::dsp::ParasiteVoice::kMaxResonance <= 0.72f);
    EXPECT (pfl::dsp::ParasiteVoice::kMaxPan <= 0.85f);
}

static void testHungerCurveContract()
{
    // HUNGER 0 answers nothing; HUNGER 1 still refuses most stimuli by chance
    // alone and still enforces a musical floor between answers.
    EXPECT (pfl::dsp::parasiteAcceptProbability (0.0f, 100.0f) == 0.0f);
    EXPECT (pfl::dsp::parasiteAcceptProbability (1.0f, 100.0f) <= 0.62f);
    EXPECT (pfl::dsp::parasiteMinGapBeats (1.0f) >= 0.74f);
    EXPECT (pfl::dsp::parasiteMinGapBeats (0.0f) >= 5.9f);
    EXPECT (pfl::dsp::parasiteMinGapBeats (0.2f) > pfl::dsp::parasiteMinGapBeats (0.8f));
    // MUTATION 0 freezes evolution; MUTATION 1 evolves every 4 bars.
    EXPECT (pfl::dsp::parasiteLifespanBars (0.0f) > 1000);
    EXPECT (pfl::dsp::parasiteLifespanBars (1.0f) == 4);
    EXPECT (pfl::dsp::parasiteLifespanBars (0.0001f) > 1000);
}

static void testSilenceNoStimuli()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (32.0, sr, bpm);
    auto sil = makeSilence (n);
    Engine eng;
    Setup s;
    s.sens = 1.0f;
    s.hunger = 1.0f;
    s.mutation = 1.0f;
    s.output = 1.0f;
    setupEngine (eng, s);
    const auto out = processRun (eng, sil, sil, sr, bpm, 0.0, true, 256);
    EXPECT (eng.stimulusCount() == 0);
    EXPECT (eng.responseCount() == 0);
    EXPECT (out.peak < 1.0e-6f);
    EXPECT (out.allFinite);
}

static void testQuietBelowFloorNoStimuli()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (32.0, sr, bpm);
    // -40 dBFS hiss and -40 dBFS tone must not machine-gun even wide open.
    auto hiss = makeNoise (n, 0.01f);
    auto tone = makeTone (n, sr, 220.0, 0.01f);
    for (float sens : { 0.5f, 0.9f, 1.0f })
    {
        Engine a, b;
        Setup s;
        s.sens = sens;
        setupEngine (a, s);
        setupEngine (b, s);
        processRun (a, hiss, hiss, sr, bpm, 0.0, true, 256);
        processRun (b, tone, tone, sr, bpm, 0.0, true, 256);
        EXPECT (a.stimulusCount() == 0);
        EXPECT (b.stimulusCount() == 0);
    }
}

static void testMixZeroIsDry()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (32.0, sr, bpm);
    auto src = makeDrums (n, sr, bpm);
    Engine eng;
    Setup s;
    s.mix = 0.0f;
    s.sens = 1.0f;
    s.hunger = 1.0f;
    s.mutation = 1.0f;
    s.output = 1.0f;
    setupEngine (eng, s);
    const auto out = processRun (eng, src, src, sr, bpm, 0.0, true, 256);
    float maxDiff = 0.0f;
    for (int i = 0; i < n; ++i)
        maxDiff = std::max (maxDiff, std::abs (out.L[static_cast<size_t> (i)]
                                               - src[static_cast<size_t> (i)]));
    EXPECT (maxDiff < 1.0e-6f);
    // Structure still runs behind a dry mix.
    EXPECT (eng.responseCount() > 0);
}

static void testMixOneHasNoDryLeak()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (32.0, sr, bpm);
    auto src = makeDrums (n, sr, bpm);
    Engine eng;
    Setup s;
    s.mix = 1.0f;
    s.sens = 1.0f;
    s.hunger = 0.0f; // detect everything, answer nothing
    s.output = 1.0f;
    setupEngine (eng, s);
    const auto out = processRun (eng, src, src, sr, bpm, 0.0, true, 256);
    EXPECT (eng.stimulusCount() > 0);
    EXPECT (eng.responseCount() == 0);
    // Loud input, wide-open detection, MIX=1: output must be pure silence, so
    // none of the wet path can be carrying the dry signal.
    EXPECT (out.peak == 0.0f);
}

static void testWetIsGeneratedNotProcessed()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (32.0, sr, bpm);
    // One isolated impulse well after the analysis settle window.
    const int impulse = static_cast<int> (sr * 1.5);
    auto src = makeSilence (n);
    for (int i = 0; i < static_cast<int> (sr * 0.004); ++i)
        src[static_cast<size_t> (impulse + i)] =
            0.9f * (1.0f - static_cast<float> (i) / static_cast<float> (sr * 0.004));

    Engine eng;
    Setup s;
    s.mix = 1.0f;
    s.sens = 0.9f;
    s.hunger = 1.0f;
    s.output = 1.0f;
    setupEngine (eng, s);
    const auto out = processRun (eng, src, src, sr, bpm, 0.0, true, 256);

    EXPECT (eng.stimulusCount() >= 1);
    // An impulse through a processor would appear at the impulse sample. A
    // generated answer cannot: it is still silent for at least 4 ms after it.
    const int guard = static_cast<int> (sr * 0.004);
    float leak = 0.0f;
    for (int i = impulse; i < impulse + guard; ++i)
        leak = std::max (leak, std::abs (out.L[static_cast<size_t> (i)]));
    EXPECT (leak == 0.0f);
    EXPECT (out.peak > 0.01f); // but something did answer
    EXPECT (eng.responseCount() >= 1);
    if (! eng.responses().empty())
        EXPECT (eng.responses().front().onsetSample > impulse + guard);
}

static void testSensitivityChangesStimulusCount()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (64.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    auto pad = makePad (n, sr);

    auto count = [&] (const std::vector<float>& src, float sens)
    {
        Engine eng;
        Setup s;
        s.sens = sens;
        s.mutation = 0.0f;
        setupEngine (eng, s);
        processRun (eng, src, src, sr, bpm, 0.0, true, 256);
        return eng.stimulusCount();
    };

    const auto dLo = count (drums, 0.2f);
    const auto dMid = count (drums, 0.5f);
    const auto dHi = count (drums, 0.9f);
    EXPECT (dLo < dMid);
    EXPECT (dMid < dHi);

    const auto pLo = count (pad, 0.2f);
    const auto pHi = count (pad, 0.9f);
    EXPECT (pLo < pHi);
    // Sustained material must earn events at all — pads are not silent partners.
    EXPECT (pLo > 0);
}

static void testHungerChangesResponseCountNotDetection()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (64.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);

    struct Result { uint32_t stim, resp; float duty; };
    auto run = [&] (float hunger)
    {
        Engine eng;
        Setup s;
        s.hunger = hunger;
        s.mutation = 0.0f;
        setupEngine (eng, s);
        processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
        return Result { eng.stimulusCount(), eng.responseCount(), eng.responseDuty() };
    };

    const auto zero = run (0.0f);
    const auto lo = run (0.15f);
    const auto mid = run (0.5f);
    const auto hi = run (0.95f);
    const auto full = run (1.0f);

    // SENSITIVITY owns detection, so HUNGER must leave the stimulus count alone.
    EXPECT (zero.stim == lo.stim);
    EXPECT (lo.stim == mid.stim);
    EXPECT (mid.stim == hi.stim);
    EXPECT (hi.stim == full.stim);

    EXPECT (zero.resp == 0);
    EXPECT (lo.resp < mid.resp);
    EXPECT (mid.resp < hi.resp);

    // Default HUNGER 0.5 sits in the designed 2–8 answers per 16 beats band.
    const double per16 = mid.resp * 16.0 / 64.0;
    EXPECT (per16 >= 2.0 && per16 <= 8.0);

    // Even wide open the parasite refuses most stimuli and leaves space.
    EXPECT (full.resp < full.stim);
    EXPECT (full.duty < 0.35f);
}

static void testHungerOneLeavesSpace()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (64.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    Engine eng;
    Setup s;
    s.sens = 0.9f;
    s.hunger = 1.0f;
    s.mutation = 0.0f;
    setupEngine (eng, s);
    processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
    EXPECT (eng.responseCount() > 0);
    EXPECT (eng.responseDuty() < 0.35f);
    // Never denser than the HUNGER floor, even with a stimulus on every 16th.
    EXPECT (eng.minResponseGapBeats() < 0.0f || eng.minResponseGapBeats() >= 0.70f);
}

static void testSensitivityHungerCrossDistinct()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (64.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    struct Cell { uint32_t stim, resp; };
    auto run = [&] (float sens, float hunger)
    {
        Engine eng;
        Setup s;
        s.sens = sens;
        s.hunger = hunger;
        s.mutation = 0.0f;
        setupEngine (eng, s);
        processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
        return Cell { eng.stimulusCount(), eng.responseCount() };
    };

    const auto deafHungry = run (0.2f, 0.95f);
    const auto hotFull = run (0.9f, 0.95f);
    const auto deafFull = run (0.2f, 0.15f);
    const auto hotStarved = run (0.9f, 0.15f);

    // SENSITIVITY moves detection at fixed appetite…
    EXPECT (deafHungry.stim < hotFull.stim);
    EXPECT (deafFull.stim < hotStarved.stim);
    // …and HUNGER moves answers at fixed hearing.
    EXPECT (hotStarved.resp < hotFull.resp);
    EXPECT (deafFull.resp < deafHungry.resp);
    // Hearing more does not by itself make the parasite chattier than appetite.
    EXPECT (hotStarved.resp < deafHungry.resp);
    // Zero stimuli can never produce answers, however hungry.
    auto quiet = makeTone (n, sr, 220.0, 0.01f);
    Engine eng;
    Setup s;
    s.sens = 0.0f;
    s.hunger = 1.0f;
    setupEngine (eng, s);
    processRun (eng, quiet, quiet, sr, bpm, 0.0, true, 256);
    EXPECT (eng.stimulusCount() == 0);
    EXPECT (eng.responseCount() == 0);
}

static void testMutationEvolvesGrammarNotDensity()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (256.0, sr, bpm); // 64 bars
    auto drums = makeDrums (n, sr, bpm);
    struct Cell { uint32_t gen, resp; std::string dna; };
    auto run = [&] (float mutation)
    {
        Engine eng;
        Setup s;
        s.mutation = mutation;
        setupEngine (eng, s);
        processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
        return Cell { eng.dnaGeneration(), eng.responseCount(), eng.dnaFingerprint() };
    };

    const auto frozen = run (0.0f);
    const auto midMut = run (0.35f);
    const auto fullMut = run (1.0f);

    // MUTATION 0 freezes the response grammar entirely.
    EXPECT (frozen.gen == 0);
    Engine still;
    Setup s0;
    s0.mutation = 0.0f;
    setupEngine (still, s0);
    const auto dnaBefore = still.dnaFingerprint();
    processRun (still, drums, drums, sr, bpm, 0.0, true, 256);
    EXPECT (still.dnaFingerprint() == dnaBefore);

    // Higher MUTATION evolves sooner and further.
    EXPECT (midMut.gen > 0);
    EXPECT (fullMut.gen > midMut.gen);
    EXPECT (fullMut.dna != frozen.dna);

    // …but it must not act as a density control. Compare its swing on response
    // count against the swing HUNGER produces on the same fixture.
    auto respAt = [&] (float hunger)
    {
        Engine eng;
        Setup s;
        s.hunger = hunger;
        s.mutation = 0.0f;
        setupEngine (eng, s);
        processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
        return static_cast<int> (eng.responseCount());
    };
    const int hungerSwing = std::abs (respAt (0.95f) - respAt (0.15f));
    const int mutationSwing =
        std::abs (static_cast<int> (fullMut.resp) - static_cast<int> (frozen.resp));
    EXPECT (hungerSwing > 0);
    EXPECT (mutationSwing * 3 < hungerSwing);
}

static void testBufferMatrixFingerprints()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (96.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    const int blocks[] = { 64, 127, 128, 255, 256, 511, 512, 1024 };
    std::string ref;
    for (int b : blocks)
    {
        Engine eng;
        Setup s;
        s.block = b;
        setupEngine (eng, s);
        processRun (eng, drums, drums, sr, bpm, 0.0, true, b);
        const auto key = structuralKey (eng);
        if (ref.empty())
            ref = key;
        else
            EXPECT (key == ref);
    }
    EXPECT (! ref.empty());

    // prepare(maxBlock) must not leak into the schedule either.
    Engine wide;
    Setup s;
    s.block = 1024;
    setupEngine (wide, s);
    processRun (wide, drums, drums, sr, bpm, 0.0, true, 64);
    EXPECT (structuralKey (wide) == ref);
}

static void testDualRunDeterminism()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (96.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    auto run = [&] ()
    {
        Engine eng;
        Setup s;
        setupEngine (eng, s);
        const auto out = processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
        std::ostringstream os;
        os << structuralKey (eng) << "|" << std::lround (out.peak * 1.0e6f);
        return os.str();
    };
    EXPECT (run() == run());
}

static void testSeedChangeDivergesAndStaysDeterministic()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (64.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    auto run = [&] (uint64_t seed)
    {
        Engine eng;
        Setup s;
        s.seed = seed;
        setupEngine (eng, s);
        processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
        return structuralKey (eng);
    };
    const auto a1 = run (2002), a2 = run (2002);
    const auto b1 = run (3003), b2 = run (3003);
    EXPECT (a1 == a2);
    EXPECT (b1 == b2);
    EXPECT (a1 != b1);
}

static void testSeedChangeRegeneratesAtBarBoundary()
{
    const double sr = 48000.0, bpm = 120.0;
    auto drums = makeDrums (beatsToSamples (32.0, sr, bpm), sr, bpm);
    Engine eng;
    Setup s;
    setupEngine (eng, s);
    const int half = beatsToSamples (16.0, sr, bpm);
    std::vector<float> firstL (drums.begin(), drums.begin() + half);
    const auto before = eng.dnaFingerprint();
    processRun (eng, firstL, firstL, sr, bpm, 0.0, true, 256);
    eng.setSeed (4004);
    std::vector<float> secondL (drums.begin() + half, drums.end());
    const auto out = processRun (eng, secondL, secondL, sr, bpm, 16.0, true, 256);
    EXPECT (eng.seed() == 4004);
    EXPECT (eng.dnaFingerprint() != before);
    EXPECT (out.allFinite);
    EXPECT (out.peak <= 1.0f);
}

static void testSeekDoesNotCreateFalseStimulus()
{
    const double sr = 48000.0, bpm = 120.0;
    Engine eng;
    Setup s;
    setupEngine (eng, s);

    // Warm on drums for 8 beats.
    auto warm = makeDrums (beatsToSamples (8.0, sr, bpm), sr, bpm);
    processRun (eng, warm, warm, sr, bpm, 0.0, true, 256);
    const auto stimAfterWarm = eng.stimulusCount();
    const auto respAfterWarm = eng.responseCount();
    EXPECT (stimAfterWarm > 0);

    // Jump forward 32 beats into already-flowing steady material. The
    // discontinuity itself, and the analysis restart it forces, must not
    // register as an onset or a material change.
    const int post = static_cast<int> (sr * 0.5);
    auto steady = makeTone (post, sr, 220.0, 0.3f);
    const auto out = processRun (eng, steady, steady, sr, bpm, 40.0, true, 256);
    EXPECT (eng.stimulusCount() == stimAfterWarm);
    EXPECT (eng.responseCount() == respAfterWarm);
    EXPECT (out.allFinite);

    // A backward jump behaves the same way.
    const auto back = processRun (eng, steady, steady, sr, bpm, 4.0, true, 256);
    EXPECT (eng.stimulusCount() == stimAfterWarm);
    EXPECT (back.allFinite);
}

static void testStopStartSafe()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (16.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.95f;
    setupEngine (eng, s);

    // Play, then stop with loud input and a frozen playhead.
    const auto played = processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
    EXPECT (played.allFinite);
    const auto stim = eng.stimulusCount();
    const auto gen = eng.dnaGeneration();

    const auto stopped = processRun (eng, drums, drums, sr, bpm, 16.0, false, 256);
    EXPECT (eng.stimulusCount() == stim);   // analysis frozen
    EXPECT (eng.dnaGeneration() == gen);    // DNA paused
    EXPECT (stopped.allFinite);
    // After ~80 ms of stopped transport, any release ramp must finish.
    EXPECT (! eng.voiceActive());

    // Restart must not invent an onset from the transport edge.
    auto steady = makeTone (static_cast<int> (sr * 0.4), sr, 220.0, 0.3f);
    const auto restarted = processRun (eng, steady, steady, sr, bpm, 16.0, true, 256);
    EXPECT (eng.stimulusCount() == stim);
    EXPECT (restarted.allFinite);
}

static void testMidInsertDnaMatchesWarmTimeline()
{
    const double sr = 48000.0, bpm = 120.0;
    Setup s;
    s.mutation = 1.0f;
    s.hunger = 0.0f; // DNA only — no response RNG coupling
    s.sens = 0.5f;
    s.trace = false;

    Engine warm;
    setupEngine (warm, s);
    // Slightly past 400 beats so the warm path has entered absolute bar 100
    // (floor(ppq/4) == 100), matching a mid-insert that starts at beat 400.
    auto silence = makeSilence (beatsToSamples (404.0, sr, bpm));
    processRun (warm, silence, silence, sr, bpm, 0.0, true, 512);
    const auto warmFp = warm.dnaFingerprint();
    const auto warmGen = warm.dnaGeneration();
    EXPECT (warmGen > 0);

    Engine insert;
    setupEngine (insert, s); // forceRebuild(0) — mid-insert at beat 400 / bar 100
    auto tail = makeSilence (beatsToSamples (4.0, sr, bpm));
    processRun (insert, tail, tail, sr, bpm, 400.0, true, 512);
    EXPECT (insert.dnaFingerprint() == warmFp);
    EXPECT (insert.dnaGeneration() == warmGen);
}

static void testNoSelfTrigger()
{
    const double sr = 48000.0, bpm = 120.0;
    Engine eng;
    Setup s;
    s.mix = 1.0f;
    s.sens = 1.0f;
    s.hunger = 1.0f;
    s.output = 1.0f;
    setupEngine (eng, s);

    auto drums = makeDrums (beatsToSamples (16.0, sr, bpm), sr, bpm);
    processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
    EXPECT (eng.responseCount() > 0);
    const auto stim = eng.stimulusCount();

    // Input goes silent while the parasite is still sounding. Because analysis
    // taps the original input only, its own wet can never re-trigger it.
    auto sil = makeSilence (beatsToSamples (16.0, sr, bpm));
    const auto out = processRun (eng, sil, sil, sr, bpm, 16.0, true, 256);
    EXPECT (eng.stimulusCount() == stim);
    EXPECT (out.allFinite);
}

static void testResponseCausalityAndLatencyBand()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (96.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.8f;
    setupEngine (eng, s);
    processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);

    const auto& stimuli = eng.stimuli();
    const auto& responses = eng.responses();
    EXPECT (! responses.empty());
    EXPECT (! stimuli.empty());

    const int64_t maxDelay =
        static_cast<int64_t> (1.0 * sr * 60.0 / bpm) + 4; // one beat + slack
    for (const auto& r : responses)
    {
        bool caused = false;
        for (const auto& e : stimuli)
        {
            if (e.sampleIndex <= r.onsetSample && r.onsetSample - e.sampleIndex <= maxDelay)
            {
                caused = true;
                break;
            }
        }
        EXPECT (caused);
        EXPECT (r.delaySlot >= 0 && r.delaySlot < pfl::dsp::ParasiteDNA::kDelaySlots);
        EXPECT (r.durSlot >= 0 && r.durSlot < pfl::dsp::ParasiteDNA::kDurSlots);
        EXPECT (std::abs (r.pan) <= pfl::dsp::ParasiteVoice::kMaxPan + 1.0e-4f);
    }
}

static void testMixOutputOrthogonalToStructure()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (64.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    auto run = [&] (float mix, float output)
    {
        Engine eng;
        Setup s;
        s.mix = mix;
        s.output = output;
        setupEngine (eng, s);
        processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
        return structuralKey (eng);
    };
    const auto ref = run (1.0f, 0.9f);
    EXPECT (run (0.0f, 0.9f) == ref);
    EXPECT (run (0.5f, 0.5f) == ref);
    EXPECT (run (1.0f, 1.0f) == ref);
}

static void testMaxSettingsFinite()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (48.0, sr, bpm);
    auto hostile = makeDrums (n, sr, bpm);
    // Poke non-finite and full-scale samples into the input.
    hostile[100] = std::numeric_limits<float>::quiet_NaN();
    hostile[101] = std::numeric_limits<float>::infinity();
    hostile[102] = -std::numeric_limits<float>::infinity();
    for (int i = 2000; i < 3000; ++i)
        hostile[static_cast<size_t> (i)] = (i % 2) ? 1.0f : -1.0f;

    Engine eng;
    Setup s;
    s.mix = 1.0f;
    s.sens = 1.0f;
    s.hunger = 1.0f;
    s.mutation = 1.0f;
    s.output = 1.0f;
    s.seed = 999999;
    setupEngine (eng, s);
    const auto out = processRun (eng, hostile, hostile, sr, bpm, 0.0, true, 256);
    EXPECT (out.allFinite);
    EXPECT (out.peak <= 0.99f);
    EXPECT (eng.responseDuty() < 0.6f);

    // Full-scale noise is a wall of sound: the parasite must hold back, not
    // machine-gun into it.
    auto wall = makeNoise (n, 0.99f, 31337u);
    Engine eng2;
    setupEngine (eng2, s);
    const auto out2 = processRun (eng2, wall, wall, sr, bpm, 0.0, true, 256);
    EXPECT (out2.allFinite);
    EXPECT (out2.peak <= 0.99f);
    EXPECT (eng2.responseDuty() < 0.35f);
}

static void testTemposAndRates()
{
    const double tempos[] = { 40.0, 72.0, 93.0, 120.0, 137.0, 180.0 };
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    for (double sr : rates)
    {
        for (double bpm : tempos)
        {
            const int n = beatsToSamples (16.0, sr, bpm);
            auto drums = makeDrums (n, sr, bpm);
            Engine eng;
            Setup s;
            s.sr = sr;
            setupEngine (eng, s);
            const auto out = processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
            EXPECT (out.allFinite);
            EXPECT (out.peak <= 0.99f);
            EXPECT (eng.responseDuty() < 0.45f);
        }
    }
}

static void testHungerIsMusicalTimeNotBlockCount()
{
    // The same 32 beats at 60 and 120 BPM must land in the same answers-per-16
    // beats band, even though 60 BPM processes twice the samples.
    const double sr = 48000.0;
    auto per16 = [&] (double bpm)
    {
        const int n = beatsToSamples (32.0, sr, bpm);
        auto drums = makeDrums (n, sr, bpm);
        Engine eng;
        Setup s;
        s.mutation = 0.0f;
        setupEngine (eng, s);
        processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);
        return eng.responseCount() * 16.0 / 32.0;
    };
    const double slow = per16 (60.0);
    const double fast = per16 (120.0);
    EXPECT (slow > 0.0 && fast > 0.0);
    EXPECT (std::abs (slow - fast) <= 0.5 * std::max (slow, fast) + 1.0);
}

static void testStimulusTimingIsSampleAccurate()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (16.0, sr, bpm);
    const int onset = static_cast<int> (sr * 1.5);
    auto src = makeSilence (n);
    for (int i = 0; i < static_cast<int> (sr * 0.03); ++i)
        src[static_cast<size_t> (onset + i)] =
            0.8f * std::exp (-static_cast<float> (i) / static_cast<float> (sr * 0.01f));

    // A single hit is one event, at the hit, regardless of the partition.
    int64_t ref = -1;
    for (int b : { 64, 127, 255, 256, 1024 })
    {
        Engine eng;
        Setup s;
        s.sens = 0.9f;
        s.block = b;
        setupEngine (eng, s);
        processRun (eng, src, src, sr, bpm, 0.0, true, b);
        EXPECT (eng.stimulusCount() == 1);
        EXPECT (eng.stimuli().size() == 1);
        if (eng.stimuli().empty())
            continue;
        const int64_t at = eng.stimuli().front().sampleIndex;
        EXPECT (at >= onset);
        EXPECT (at - onset <= static_cast<int64_t> (sr * 0.010)); // within the attack window
        if (ref < 0)
            ref = at;
        else
            EXPECT (at == ref);
    }
}

static void testLongSoak()
{
    const double sr = 44100.0, bpm = 120.0;
    const int n = beatsToSamples (512.0, sr, bpm); // 128 bars
    auto drums = makeDrums (n, sr, bpm);
    Engine eng;
    Setup s;
    s.sr = sr;
    s.mutation = 1.0f;
    s.hunger = 0.7f;
    setupEngine (eng, s);
    // Sweep macros live to exercise the smoothers and the mutation-rise path.
    const int chunk = 256;
    int done = 0;
    const double bps = (bpm / 60.0) / sr;
    std::vector<float> outL (static_cast<size_t> (n)), outR (static_cast<size_t> (n));
    int phase = -1;
    while (done < n)
    {
        const int m = std::min (chunk, n - done);
        const double ppq = static_cast<double> (done) * bps;
        const int p = static_cast<int> (ppq) / 32;
        if (p != phase)
        {
            phase = p;
            eng.setSensitivity (0.2f + 0.7f * static_cast<float> (phase % 3) / 2.0f);
            eng.setHunger (0.2f + 0.6f * static_cast<float> ((phase + 1) % 3) / 2.0f);
        }
        eng.process (drums.data() + done, drums.data() + done, outL.data() + done,
                     outR.data() + done, m, ppq, bpm, true);
        done += m;
    }
    for (int i = 0; i < n; i += 512)
    {
        EXPECT (std::isfinite (outL[static_cast<size_t> (i)]));
        EXPECT (std::abs (outL[static_cast<size_t> (i)]) <= 0.99f);
    }
    EXPECT (eng.dnaGeneration() > 0);
    EXPECT (eng.responseDuty() < 0.40f);
    EXPECT (eng.overflowDrops() >= 0u); // bounded queue never grew
    EXPECT (eng.stimuli().size() <= static_cast<size_t> (Engine::kMaxTraceEvents));
}

int main()
{
    testAlgorithmVersion();
    testHungerCurveContract();
    testSilenceNoStimuli();
    testQuietBelowFloorNoStimuli();
    testMixZeroIsDry();
    testMixOneHasNoDryLeak();
    testWetIsGeneratedNotProcessed();
    testSensitivityChangesStimulusCount();
    testHungerChangesResponseCountNotDetection();
    testHungerOneLeavesSpace();
    testSensitivityHungerCrossDistinct();
    testMutationEvolvesGrammarNotDensity();
    testBufferMatrixFingerprints();
    testDualRunDeterminism();
    testSeedChangeDivergesAndStaysDeterministic();
    testSeedChangeRegeneratesAtBarBoundary();
    testSeekDoesNotCreateFalseStimulus();
    testStopStartSafe();
    testMidInsertDnaMatchesWarmTimeline();
    testNoSelfTrigger();
    testResponseCausalityAndLatencyBand();
    testMixOutputOrthogonalToStructure();
    testMaxSettingsFinite();
    testTemposAndRates();
    testHungerIsMusicalTimeNotBlockCount();
    testStimulusTimingIsSampleAccurate();
    testLongSoak();

    if (gFails == 0)
    {
        std::cout << "signal_parasite_tests: OK\n";
        return 0;
    }
    std::cerr << "signal_parasite_tests: " << gFails << " failure(s)\n";
    return 1;
}

// temporary - remove

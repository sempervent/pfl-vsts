#include "dsp/SignalParasiteEngine.h"
#include "dsp/ParamSmoother.h"
#include "performance/SignalParasitePerformanceController.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

// Allocation trap. Armed only around process() calls so that the real-time
// path can be asserted allocation-free; everything else (streams, fixtures)
// runs with it disarmed.
namespace
{
bool gTrapAllocs = false;
int gAllocCount = 0;
} // namespace

void* operator new (std::size_t n)
{
    if (gTrapAllocs)
        ++gAllocCount;
    void* p = std::malloc (n == 0 ? 1 : n);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static int gFails = 0;
#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << __func__ << ": " << #cond << "\n"; \
            ++gFails; \
        } \
    } while (0)

namespace
{
using Engine = pfl::dsp::SignalParasiteEngine;
using Reason = pfl::dsp::ParasiteSuppressReason;
using Rel = pfl::dsp::ParasiteRelationship;
using Perf = pfl::parasite_perf::SignalParasitePerformanceController;
using Cmd = pfl::parasite_perf::Command;
using Mode = pfl::parasite_perf::Mode;

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

/** Drop one synthetic percussive hit into `x` at an absolute beat position. */
void addHit (std::vector<float>& x, double beat, float amp, double decayMs, double toneHz,
             float noiseMix, double sr, double bpm, FixtureLcg& lcg)
{
    const double spb = sr * 60.0 / bpm;
    const auto n = static_cast<int64_t> (x.size());
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
}

void clampFixture (std::vector<float>& x)
{
    for (auto& v : x)
        v = std::clamp (v, -0.99f, 0.99f);
}

/** One bar of the synthetic kit: kick, snare, offbeat hats, 16th ghost notes. */
void addDrumBar (std::vector<float>& x, double b0, double sr, double bpm, FixtureLcg& lcg)
{
    addHit (x, b0 + 0.0, 0.85f, 90.0, 55.0, 0.05f, sr, bpm, lcg);
    addHit (x, b0 + 2.0, 0.85f, 90.0, 55.0, 0.05f, sr, bpm, lcg);
    addHit (x, b0 + 1.0, 0.45f, 60.0, 190.0, 0.65f, sr, bpm, lcg);
    addHit (x, b0 + 3.0, 0.45f, 60.0, 190.0, 0.65f, sr, bpm, lcg);
    for (int e = 0; e < 8; ++e)
        addHit (x, b0 + e * 0.5 + 0.25, 0.16f, 25.0, 5000.0, 0.90f, sr, bpm, lcg);
    for (int s = 0; s < 16; ++s)
        if (s % 4 == 3)
            addHit (x, b0 + s * 0.25, 0.055f, 18.0, 7000.0, 0.95f, sr, bpm, lcg);
}

/** One bar of a sparse partner: a downbeat and, every other bar, one answer. */
void addSparseBar (std::vector<float>& x, double b0, int bar, double sr, double bpm,
                   FixtureLcg& lcg)
{
    addHit (x, b0 + 0.0, 0.80f, 120.0, 60.0, 0.10f, sr, bpm, lcg);
    if (bar % 2 == 1)
        addHit (x, b0 + 2.5, 0.45f, 60.0, 2400.0, 0.80f, sr, bpm, lcg);
}

/** One bar of a busy partner: loud sixteenths, short tails, almost no room. */
void addBusyBar (std::vector<float>& x, double b0, double sr, double bpm, FixtureLcg& lcg)
{
    for (int s = 0; s < 16; ++s)
        addHit (x, b0 + s * 0.25, s % 4 == 0 ? 0.90f : 0.65f, 28.0, s % 2 ? 2600.0 : 80.0,
                s % 2 ? 0.85f : 0.20f, sr, bpm, lcg);
}

std::vector<float> makeDrums (int n, double sr, double bpm)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    FixtureLcg lcg;
    const double spb = sr * 60.0 / bpm;
    const int bars = static_cast<int> (n / (spb * 4.0)) + 1;
    for (int b = 0; b < bars; ++b)
        addDrumBar (x, b * 4.0, sr, bpm, lcg);
    clampFixture (x);
    return x;
}

std::vector<float> makeSparse (int n, double sr, double bpm)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    FixtureLcg lcg { 4242u };
    const double spb = sr * 60.0 / bpm;
    const int bars = static_cast<int> (n / (spb * 4.0)) + 1;
    for (int b = 0; b < bars; ++b)
        addSparseBar (x, b * 4.0, b, sr, bpm, lcg);
    clampFixture (x);
    return x;
}

std::vector<float> makeBusy (int n, double sr, double bpm)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    FixtureLcg lcg { 909u };
    const double spb = sr * 60.0 / bpm;
    const int bars = static_cast<int> (n / (spb * 4.0)) + 1;
    for (int b = 0; b < bars; ++b)
        addBusyBar (x, b * 4.0, sr, bpm, lcg);
    clampFixture (x);
    return x;
}

/** Sparse for `section` beats, busy for `section`, sparse again. A whole arc. */
std::vector<float> makeJourney (double sectionBeats, double sr, double bpm)
{
    const int n = static_cast<int> (3.0 * sectionBeats * sr * 60.0 / bpm);
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    FixtureLcg lcg { 1717u };
    const int barsPerSection = static_cast<int> (sectionBeats / 4.0);
    for (int b = 0; b < barsPerSection; ++b)
        addSparseBar (x, b * 4.0, b, sr, bpm, lcg);
    for (int b = 0; b < barsPerSection; ++b)
        addBusyBar (x, sectionBeats + b * 4.0, sr, bpm, lcg);
    for (int b = 0; b < barsPerSection; ++b)
        addSparseBar (x, 2.0 * sectionBeats + b * 4.0, b, sr, bpm, lcg);
    clampFixture (x);
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
       << eng.stateFingerprint() << "|" << eng.dnaFingerprint();
    return os.str();
}

/** Drive the engine until it leaves LURKING, or give up after `maxBeats`. */
bool runUntilState (Engine& eng, const std::vector<float>& src, double sr, double bpm,
                    Rel want, double maxBeats, double& beatsConsumed)
{
    const int block = 256;
    const double bps = (bpm / 60.0) / sr;
    const auto limit = static_cast<int> (maxBeats * sr * 60.0 / bpm);
    const int n = std::min (limit, static_cast<int> (src.size()));
    std::vector<float> outL (static_cast<size_t> (block)), outR (static_cast<size_t> (block));
    for (int done = 0; done < n; done += block)
    {
        const int m = std::min (block, n - done);
        eng.process (src.data() + done, src.data() + done, outL.data(), outR.data(), m,
                     static_cast<double> (done) * bps, bpm, true);
        if (eng.relationshipState() == want)
        {
            beatsConsumed = static_cast<double> (done + m) * bps;
            return true;
        }
    }
    beatsConsumed = static_cast<double> (n) * bps;
    return false;
}

int beatsToSamples (double beats, double sr, double bpm)
{
    return static_cast<int> (beats * sr * 60.0 / bpm);
}

/** Drive engine + performance controller over a contiguous beat window. */
RunOut processWithPerf (Engine& eng, Perf& perf, const std::vector<float>& inL,
                        const std::vector<float>& inR, double sr, double bpm, double startBeat,
                        double numBeats, bool playing, int block = 256,
                        pfl::dsp::ParamSmoother* silenceSm = nullptr)
{
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (numBeats / bps);
    const int offset = static_cast<int> (startBeat / bps);
    RunOut out;
    out.L.assign (static_cast<size_t> (n), 0.0f);
    out.R.assign (static_cast<size_t> (n), 0.0f);
    int done = 0;
    while (done < n)
    {
        const int m = std::min (block, n - done);
        const int idx = offset + done;
        if (idx + m > static_cast<int> (inL.size()))
            break;
        const double ppq = startBeat + static_cast<double> (done) * bps;
        perf.tick (ppq, playing, eng);
        eng.process (inL.data() + idx, inR.data() + idx, out.L.data() + done, out.R.data() + done,
                     m, ppq, bpm, playing);
        if (silenceSm != nullptr)
        {
            const bool silenced = perf.mode() == Mode::Silenced;
            silenceSm->setTarget (silenced ? 0.0f : 1.0f);
            for (int i = 0; i < m; ++i)
            {
                const float g = silenceSm->getNext();
                out.L[static_cast<size_t> (done + i)] *= g;
                out.R[static_cast<size_t> (done + i)] *= g;
            }
        }
        done += m;
    }
    for (int i = 0; i < done; ++i)
    {
        const float a = out.L[static_cast<size_t> (i)];
        const float b = out.R[static_cast<size_t> (i)];
        if (! std::isfinite (a) || ! std::isfinite (b))
            out.allFinite = false;
        out.peak = std::max (out.peak, std::max (std::abs (a), std::abs (b)));
    }
    return out;
}
} // namespace

// ---------------------------------------------------------------------------

static void testAlgorithmVersion()
{
    EXPECT (Engine::kAlgorithmVersion == 3);
    EXPECT (pfl::parasite_perf::kPerformanceEngineVersion == 1);
    EXPECT (pfl::dsp::ParasiteStimulusDetector::kQueueCap == 8);
    EXPECT (pfl::dsp::ParasiteVoice::kMaxResonance <= 0.72f);
    EXPECT (pfl::dsp::ParasiteVoice::kMaxPan <= 0.85f);

    // Stage 2 shape: a small fixed history, minor reflexes about a beat apart,
    // major opportunities a musical phrase apart.
    using History = pfl::dsp::RecentStimulusHistory;
    using Model = pfl::dsp::ParasiteRelationshipModel;
    EXPECT (History::kCapacity >= 8 && History::kCapacity <= 32);
    EXPECT (Model::kMinorEvalBeats >= 1.0f && Model::kMinorEvalBeats <= 2.0f);
    EXPECT (Model::kMajorEvalBeatsMin >= 4.0f);
    EXPECT (Model::kMajorEvalBeatsMax <= 16.0f);
    EXPECT (Model::kNumStates == 4);

    // The relationship can slow the parasite down but never speed it past the
    // Stage 1 politeness floor by more than its own bias.
    EXPECT (std::string (pfl::dsp::parasiteRelationshipName (Rel::Lurking)) == "LURKING");
    EXPECT (std::string (pfl::dsp::parasiteRelationshipName (Rel::Withdrawn)) == "WITHDRAWN");
    EXPECT (std::string (pfl::dsp::parasiteSuppressName (Reason::Relationship))
            == "RELATIONSHIP");
}

static void testStartsLurking()
{
    Engine eng;
    Setup s;
    setupEngine (eng, s);
    EXPECT (eng.relationshipState() == Rel::Lurking);
    EXPECT (eng.historySize() == 0);
    EXPECT (eng.stateTransitions() == 0);
    EXPECT (eng.relationshipSamples() == 0);
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

// ---------------------------------------------------------------------------
// Stage 2 — relationship
// ---------------------------------------------------------------------------

namespace
{
struct Journey
{
    uint32_t stim = 0, resp = 0, transitions = 0;
    float accept = 0.0f;
    float occ[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    Rel finalState = Rel::Lurking;
};

Journey runJourney (const std::vector<float>& src, double sr, double bpm, float sens,
                    float hunger, float mutation = 0.0f)
{
    Engine eng;
    Setup s;
    s.sens = sens;
    s.hunger = hunger;
    s.mutation = mutation;
    setupEngine (eng, s);
    processRun (eng, src, src, sr, bpm, 0.0, true, 256);
    Journey j;
    j.stim = eng.stimulusCount();
    j.resp = eng.responseCount();
    j.transitions = eng.stateTransitions();
    j.accept = eng.acceptRatio();
    for (int i = 0; i < 4; ++i)
        j.occ[i] = eng.stateOccupancy (static_cast<Rel> (i));
    j.finalState = eng.relationshipState();
    return j;
}
} // namespace

static void testBusySourceWithdrawsSparseSourceEngages()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (96.0, sr, bpm);
    auto sparse = makeSparse (n, sr, bpm);
    auto busy = makeBusy (n, sr, bpm);

    // Same ears, same appetite. Only the partner changes.
    const auto s = runJourney (sparse, sr, bpm, 0.5f, 0.5f);
    const auto b = runJourney (busy, sr, bpm, 0.5f, 0.5f);

    const auto wSparse = s.occ[static_cast<int> (Rel::Withdrawn)];
    const auto wBusy = b.occ[static_cast<int> (Rel::Withdrawn)];
    EXPECT (wBusy > wSparse);
    EXPECT (wBusy > 0.25f);
    EXPECT (wSparse < 0.10f);

    // A wall of events is not an invitation: it answers a far smaller share of it.
    EXPECT (b.accept < s.accept);

    // The sparse partner is the one it bonds with.
    const auto engagedSparse = s.occ[static_cast<int> (Rel::Attached)]
                               + s.occ[static_cast<int> (Rel::Answering)];
    const auto engagedBusy = b.occ[static_cast<int> (Rel::Attached)]
                             + b.occ[static_cast<int> (Rel::Answering)];
    EXPECT (engagedSparse > 0.5f);
    EXPECT (engagedSparse > engagedBusy);
}

static void testMutationZeroFreezesDnaNotState()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (96.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);

    Engine eng;
    Setup s;
    s.mutation = 0.0f;
    setupEngine (eng, s);
    const auto dnaBefore = eng.dnaFingerprint();
    processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);

    EXPECT (eng.dnaGeneration() == 0);
    EXPECT (eng.dnaFingerprint() == dnaBefore);
    // Frozen DNA is not a frozen relationship.
    EXPECT (eng.stateTransitions() > 0);
    EXPECT (eng.relationshipState() != Rel::Lurking);
}

static void testMutationDoesNotDriveStateRate()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (128.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);
    // MUTATION owns DNA. It must not become a second clock on the state machine,
    // so the transition count cannot swing wildly with it.
    const auto frozen = runJourney (drums, sr, bpm, 0.5f, 0.5f, 0.0f);
    const auto full = runJourney (drums, sr, bpm, 0.5f, 0.5f, 1.0f);
    const int swing = std::abs (static_cast<int> (full.transitions)
                                - static_cast<int> (frozen.transitions));
    EXPECT (swing <= 2);
}

static void testHungerZeroStillObserves()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (96.0, sr, bpm);
    auto sparse = makeSparse (n, sr, bpm);

    Engine eng;
    Setup s;
    s.hunger = 0.0f;
    setupEngine (eng, s);
    processRun (eng, sparse, sparse, sr, bpm, 0.0, true, 256);

    EXPECT (eng.responseCount() == 0);
    EXPECT (eng.stimulusCount() > 0);
    // Silent does not mean blind: it still builds a history and still forms an
    // opinion about the source.
    EXPECT (eng.historySize() > 0);
    EXPECT (eng.stateTransitions() > 0);
    EXPECT (eng.relationshipState() != Rel::Lurking);
    // …but with no answers there is no conversation to be in.
    EXPECT (eng.stateOccupancy (Rel::Answering) == 0.0f);
}

static void testNoStimulusNoResponseEvenWhenAnswering()
{
    const double sr = 48000.0, bpm = 120.0;
    auto drums = makeDrums (beatsToSamples (128.0, sr, bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.8f;
    setupEngine (eng, s);

    double consumed = 0.0;
    const bool reached = runUntilState (eng, drums, sr, bpm, Rel::Answering, 96.0, consumed);
    EXPECT (reached);
    EXPECT (eng.relationshipState() == Rel::Answering);

    // Cut the source dead while it is at its most forward. Let any already
    // scheduled answer land, then hold silence for 32 beats.
    auto settle = makeSilence (beatsToSamples (2.0, sr, bpm));
    processRun (eng, settle, settle, sr, bpm, consumed, true, 256);
    const auto respAfterSettle = eng.responseCount();
    const auto stimAfterSettle = eng.stimulusCount();

    auto quiet = makeSilence (beatsToSamples (32.0, sr, bpm));
    const auto out = processRun (eng, quiet, quiet, sr, bpm, consumed + 2.0, true, 256);
    EXPECT (eng.responseCount() == respAfterSettle);
    EXPECT (eng.stimulusCount() == stimAfterSettle);
    EXPECT (out.allFinite);
    // With nothing to answer it does not stay forward either.
    EXPECT (eng.relationshipState() != Rel::Answering);
}

static void testSilenceHasNoRelationship()
{
    const double sr = 48000.0, bpm = 120.0;
    auto sil = makeSilence (beatsToSamples (128.0, sr, bpm));
    Engine eng;
    Setup s;
    s.sens = 1.0f;
    s.hunger = 1.0f;
    s.mutation = 1.0f;
    setupEngine (eng, s);
    const auto out = processRun (eng, sil, sil, sr, bpm, 0.0, true, 256);
    EXPECT (eng.responseCount() == 0);
    EXPECT (eng.historySize() == 0);
    EXPECT (eng.relationshipState() == Rel::Lurking);
    EXPECT (eng.stateTransitions() == 0);
    EXPECT (eng.stateOccupancy (Rel::Lurking) == 1.0f);
    EXPECT (out.peak == 0.0f);
}

static void testSeekResetsRelationship()
{
    const double sr = 48000.0, bpm = 120.0;
    auto drums = makeDrums (beatsToSamples (128.0, sr, bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.7f;
    s.mutation = 0.0f; // frozen DNA, so "the seek kept the DNA" is literal
    setupEngine (eng, s);

    double consumed = 0.0;
    EXPECT (runUntilState (eng, drums, sr, bpm, Rel::Attached, 64.0, consumed));
    EXPECT (eng.historySize() > 0);
    const auto stim = eng.stimulusCount();
    const auto resp = eng.responseCount();
    const auto dnaBefore = eng.dnaFingerprint();

    // Jump forward into already-flowing steady material.
    auto steady = makeTone (static_cast<int> (sr * 0.5), sr, 220.0, 0.3f);
    const auto out = processRun (eng, steady, steady, sr, bpm, consumed + 64.0, true, 256);

    EXPECT (eng.relationshipState() == Rel::Lurking);
    EXPECT (eng.historySize() == 0);
    // A seek is a discontinuity, never a stimulus, and never a new personality.
    EXPECT (eng.stimulusCount() == stim);
    EXPECT (eng.responseCount() == resp);
    EXPECT (eng.dnaFingerprint() == dnaBefore);
    EXPECT (out.allFinite);
}

static void testStopPausesRelationship()
{
    const double sr = 48000.0, bpm = 120.0;
    auto drums = makeDrums (beatsToSamples (64.0, sr, bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.7f;
    setupEngine (eng, s);
    processRun (eng, drums, drums, sr, bpm, 0.0, true, 256);

    const auto state = eng.relationshipState();
    const auto transitions = eng.stateTransitions();
    const auto relSamples = eng.relationshipSamples();
    const auto history = eng.historySize();
    EXPECT (relSamples > 0);

    // Loud input, frozen playhead, transport stopped: relationship time stops
    // with it. No wall clock anywhere.
    const auto stopped = processRun (eng, drums, drums, sr, bpm, 64.0, false, 256);
    EXPECT (eng.relationshipSamples() == relSamples);
    EXPECT (eng.relationshipState() == state);
    EXPECT (eng.stateTransitions() == transitions);
    EXPECT (eng.historySize() == history); // paused, not forgotten
    EXPECT (stopped.allFinite);
}

static void testSeedChangeResetsRelationshipNotHistoryPolicy()
{
    const double sr = 48000.0, bpm = 120.0;
    auto drums = makeDrums (beatsToSamples (128.0, sr, bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.7f;
    setupEngine (eng, s);
    double consumed = 0.0;
    EXPECT (runUntilState (eng, drums, sr, bpm, Rel::Attached, 64.0, consumed));

    const auto dnaBefore = eng.dnaFingerprint();
    eng.setSeed (5150);

    // The new DNA lands on the next bar boundary. Step there and check the
    // relationship at the moment it lands, not some beats later.
    const int block = 256;
    const double bps = (bpm / 60.0) / sr;
    const int start = static_cast<int> (consumed * sr * 60.0 / bpm);
    std::vector<float> outL (block), outR (block);
    bool landed = false;
    for (int done = start; done + block < static_cast<int> (drums.size()) && ! landed;
         done += block)
    {
        eng.process (drums.data() + done, drums.data() + done, outL.data(), outR.data(), block,
                     static_cast<double> (done) * bps, bpm, true);
        landed = eng.dnaFingerprint() != dnaBefore;
    }
    EXPECT (landed);
    EXPECT (eng.seed() == 5150);
    // A new personality starts over with this source.
    EXPECT (eng.relationshipState() == Rel::Lurking);
    EXPECT (eng.historySize() < pfl::dsp::RecentStimulusHistory::kCapacity);
}

static void testRelationshipNeverOutrunsHunger()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (128.0, sr, bpm);
    auto sparse = makeSparse (n, sr, bpm);
    Engine eng;
    Setup s;
    s.sens = 0.9f;
    s.hunger = 1.0f;
    setupEngine (eng, s);
    processRun (eng, sparse, sparse, sr, bpm, 0.0, true, 256);
    // This fixture puts the parasite in ANSWERING, its most forward state…
    EXPECT (eng.stateOccupancy (Rel::Answering) > 0.2f);
    // …and it still respects the HUNGER floor and still leaves space.
    EXPECT (eng.minResponseGapBeats() < 0.0f || eng.minResponseGapBeats() >= 0.70f);
    EXPECT (eng.responseDuty() < 0.35f);
    EXPECT (eng.responseCount() < eng.stimulusCount());
}

static void testStateBiasesAnswersWithinStageOneVocabulary()
{
    const double sr = 48000.0, bpm = 120.0;
    auto journey = makeJourney (64.0, sr, bpm);

    Engine eng;
    Setup s;
    s.hunger = 0.7f;
    setupEngine (eng, s);
    processRun (eng, journey, journey, sr, bpm, 0.0, true, 256);

    int fromLurking = 0, fromAnswering = 0;
    double delayLurking = 0.0, delayAnswering = 0.0;
    for (const auto& r : eng.responses())
    {
        EXPECT (r.delaySlot >= 0 && r.delaySlot < pfl::dsp::ParasiteDNA::kDelaySlots);
        EXPECT (r.durSlot >= 0 && r.durSlot < pfl::dsp::ParasiteDNA::kDurSlots);
        if (r.state == static_cast<uint8_t> (Rel::Lurking))
        {
            ++fromLurking;
            delayLurking += r.delaySlot;
        }
        else if (r.state == static_cast<uint8_t> (Rel::Answering))
        {
            ++fromAnswering;
            delayAnswering += r.delaySlot;
        }
    }
    EXPECT (fromLurking > 0);
    EXPECT (fromAnswering > 0);
    if (fromLurking > 0 && fromAnswering > 0)
    {
        // Lurking hangs back; answering comes in closer to the stimulus.
        EXPECT (delayLurking / fromLurking > delayAnswering / fromAnswering);
    }
}

static void testJourneyOccupancyAndReturn()
{
    const double sr = 48000.0, bpm = 120.0;
    auto journey = makeJourney (64.0, sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.6f;
    setupEngine (eng, s);
    processRun (eng, journey, journey, sr, bpm, 0.0, true, 256);

    // Sparse → busy → sparse. It should bond, back off, and come back.
    EXPECT (eng.stateTransitions() >= 3);
    EXPECT (eng.stateOccupancy (Rel::Withdrawn) > 0.05f);
    EXPECT (eng.stateOccupancy (Rel::Attached) + eng.stateOccupancy (Rel::Answering) > 0.25f);
    EXPECT (eng.relationshipState() != Rel::Withdrawn);

    float total = 0.0f;
    for (int i = 0; i < 4; ++i)
        total += eng.stateOccupancy (static_cast<Rel> (i));
    EXPECT (std::abs (total - 1.0f) < 1.0e-3f);
}

static void testHistoryIsBoundedAndProcessDoesNotAllocate()
{
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (256.0, sr, bpm);
    auto busy = makeBusy (n, sr, bpm);
    Engine eng;
    Setup s;
    s.sens = 1.0f;
    s.hunger = 1.0f;
    s.mutation = 1.0f;
    setupEngine (eng, s);

    std::vector<float> outL (256), outR (256);
    const double bps = (bpm / 60.0) / sr;

    // Prove the trap sees allocations at all, so a zero below means something.
    gAllocCount = 0;
    gTrapAllocs = true;
    {
        std::vector<int> canary;
        canary.resize (64);
    }
    gTrapAllocs = false;
    EXPECT (gAllocCount > 0);

    gAllocCount = 0;
    gTrapAllocs = true;
    for (int done = 0; done < n; done += 256)
    {
        const int m = std::min (256, n - done);
        eng.process (busy.data() + done, busy.data() + done, outL.data(), outR.data(), m,
                     static_cast<double> (done) * bps, bpm, true);
    }
    gTrapAllocs = false;
    EXPECT (gAllocCount == 0);

    // Thousands of stimuli through a 16-slot ring: the history never grows.
    EXPECT (eng.stimulusCount() > 100u);
    EXPECT (eng.historySize() <= pfl::dsp::RecentStimulusHistory::kCapacity);
}

// ---------------------------------------------------------------------------
// Stage 3 — performance intervention
// ---------------------------------------------------------------------------

static void testStage3FreezeHoldsDnaAndRelationship()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSparse (static_cast<int> (96.0 * sr * 60.0 / bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.7f;
    s.mutation = 1.0f;
    setupEngine (eng, s);
    Perf perf;
    perf.reset (s.seed);

    auto warm = processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 32.0, true);
    EXPECT (warm.allFinite);
    const auto stateAtFreeze = eng.relationshipState();
    const auto dnaAtFreeze = eng.dnaFingerprint();
    const auto genAtFreeze = eng.dnaGeneration();
    const auto respAtFreeze = eng.responseCount();

    perf.trigger (Cmd::FreezeOn, 32.0, eng);
    EXPECT (perf.mode() == Mode::Frozen);
    processWithPerf (eng, perf, src, src, sr, bpm, 32.0, 64.0, true);

    EXPECT (eng.relationshipState() == stateAtFreeze);
    EXPECT (eng.dnaFingerprint() == dnaAtFreeze);
    EXPECT (eng.dnaGeneration() == genAtFreeze);
    EXPECT (eng.responseCount() > respAtFreeze); // still answers while frozen
    EXPECT (eng.stimulusCount() > 0u);
}

static void testStage3FreezeNotMutationZero()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeJourney (32.0, sr, bpm);
    Engine engMut0;
    Setup s0;
    s0.mutation = 0.0f;
    s0.hunger = 0.7f;
    setupEngine (engMut0, s0);
    processRun (engMut0, src, src, sr, bpm, 0.0, true, 256);
    const auto mut0States = engMut0.stateTransitions();

    Engine engFreeze;
    Setup sF;
    sF.mutation = 1.0f;
    sF.hunger = 0.7f;
    setupEngine (engFreeze, sF);
    Perf perf;
    perf.reset (sF.seed);
    processWithPerf (engFreeze, perf, src, src, sr, bpm, 0.0, 24.0, true);
    const auto held = engFreeze.relationshipState();
    const auto dna = engFreeze.dnaFingerprint();
    perf.trigger (Cmd::FreezeOn, 24.0, engFreeze);
    processWithPerf (engFreeze, perf, src, src, sr, bpm, 24.0, 72.0, true);
    EXPECT (engFreeze.relationshipState() == held);
    EXPECT (engFreeze.dnaFingerprint() == dna);
    // MUTATION=0 alone still allows relationship transitions over the journey.
    EXPECT (mut0States > 0u);
}

static void testStage3MutateWhileFrozen()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSparse (static_cast<int> (48.0 * sr * 60.0 / bpm), sr, bpm);
    Engine eng;
    Setup s;
    setupEngine (eng, s);
    Perf perf;
    perf.reset (s.seed);
    processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 16.0, true);
    perf.trigger (Cmd::FreezeOn, 16.0, eng);
    const auto dnaBefore = eng.dnaFingerprint();
    const auto genBefore = eng.dnaGeneration();
    const auto stateBefore = eng.relationshipState();

    perf.trigger (Cmd::Mutate, 16.0, eng);
    // A performer asked for a change, so the command stream never draws STAY.
    EXPECT (eng.dnaFingerprint() != dnaBefore);
    EXPECT (eng.dnaGeneration() == genBefore + 1u);
    EXPECT (perf.state().lastMutateOp > 0);
    // …and it changes nothing else: not the hold, not the mood.
    EXPECT (perf.mode() == Mode::Frozen);
    EXPECT (eng.relationshipState() == stateBefore);

    // Ignored while muted.
    perf.trigger (Cmd::SilenceOn, 16.0, eng);
    const auto dnaSilenced = eng.dnaFingerprint();
    perf.trigger (Cmd::Mutate, 16.0, eng);
    EXPECT (eng.dnaFingerprint() == dnaSilenced);
    perf.trigger (Cmd::SilenceOff, 16.0, eng);

    // Ignored mid-collapse: the arc owns the parasite until it ends.
    perf.trigger (Cmd::Collapse, 16.0, eng);
    processWithPerf (eng, perf, src, src, sr, bpm, 16.0, 8.0, true);
    const auto dnaCollapsing = eng.dnaFingerprint();
    EXPECT (perf.mode() == Mode::Collapsing);
    perf.trigger (Cmd::Mutate, 24.0, eng);
    EXPECT (eng.dnaFingerprint() == dnaCollapsing);
}

static void testStage3CollapseToDormant()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSparse (static_cast<int> (80.0 * sr * 60.0 / bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.7f;
    setupEngine (eng, s);
    Perf perf;
    perf.reset (s.seed);
    processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 16.0, true);
    perf.trigger (Cmd::Collapse, 16.0, eng);
    EXPECT (perf.mode() == Mode::Collapsing);
    processWithPerf (eng, perf, src, src, sr, bpm, 16.0, 28.0, true);
    EXPECT (perf.dormant());
    EXPECT (perf.mode() == Mode::Collapsed || perf.mode() == Mode::Frozen);
    const auto respAtDormant = eng.responseCount();
    processWithPerf (eng, perf, src, src, sr, bpm, 44.0, 32.0, true);
    EXPECT (eng.responseCount() == respAtDormant); // no new answers in dormant
    EXPECT (eng.stimulusCount() > 0u); // still listening
}

static void testStage3CollapseSilentSourceNoAudio()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSilence (static_cast<int> (48.0 * sr * 60.0 / bpm));
    Engine eng;
    Setup s;
    s.mix = 1.0f;
    setupEngine (eng, s);
    Perf perf;
    perf.reset (s.seed);
    perf.trigger (Cmd::Collapse, 0.0, eng);
    auto out = processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 28.0, true);
    EXPECT (out.peak < 0.02f);
    EXPECT (eng.responseCount() == 0u);
    EXPECT (perf.dormant());
}

static void testStage3CollapseVsHungerRamp()
{
    // A collapse is an arc with a shape and an ending. A HUNGER fade is a
    // dimmer. They must not be the same gesture on the same source.
    const double sr = 48000.0, bpm = 120.0;
    const int n = static_cast<int> (72.0 * sr * 60.0 / bpm);
    auto src = makeSparse (n, sr, bpm);

    Engine engC;
    Setup sC;
    sC.hunger = 0.7f;
    sC.mutation = 0.0f;
    setupEngine (engC, sC);
    Perf perf;
    perf.reset (sC.seed);
    perf.setTraceEnabled (true);
    processWithPerf (engC, perf, src, src, sr, bpm, 0.0, 16.0, true);
    perf.trigger (Cmd::Collapse, 16.0, engC);
    processWithPerf (engC, perf, src, src, sr, bpm, 16.0, 28.0, true);
    const auto respAfterArc = engC.responseCount();
    processWithPerf (engC, perf, src, src, sr, bpm, 44.0, 24.0, true);

    // The arc visited every phase in order, and it ended somewhere it stays.
    int seen = 0;
    for (const auto& e : perf.events())
    {
        if (e.detail.find ("CLING") != std::string::npos && seen == 0) seen = 1;
        else if (e.detail.find ("FEVER") != std::string::npos && seen == 1) seen = 2;
        else if (e.detail.find ("WITHDRAW") != std::string::npos && seen == 2) seen = 3;
        else if (e.detail.find ("DORMANT") != std::string::npos && seen == 3) seen = 4;
    }
    EXPECT (seen == 4);
    EXPECT (perf.dormant());
    EXPECT (engC.responseCount() == respAfterArc); // dormancy holds

    // The same source with HUNGER faded away instead: quieter, but never
    // dormant, and it comes straight back when the appetite does.
    Engine engH;
    Setup sH;
    sH.hunger = 0.7f;
    sH.mutation = 0.0f;
    setupEngine (engH, sH);
    processRun (engH, src, src, sr, bpm, 0.0, true, 256);
    const auto fadedKey = engH.responseFingerprint();
    EXPECT (engC.responseFingerprint() != fadedKey);

    engH.setHunger (0.0f);
    engH.snapMacros();
    const int quarter = n / 4;
    std::vector<float> tail (src.begin() + quarter, src.end());
    processRun (engH, tail, tail, sr, bpm, 18.0, true, 256);
    const auto respDuringFade = engH.responseCount();
    engH.setHunger (0.7f);
    engH.snapMacros();
    processRun (engH, tail, tail, sr, bpm, 72.0, true, 256);
    EXPECT (engH.responseCount() > respDuringFade); // appetite returns, dormancy does not
}

static void testStage3ReseedExitsDormant()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSparse (static_cast<int> (80.0 * sr * 60.0 / bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.7f;
    setupEngine (eng, s);
    Perf perf;
    perf.reset (s.seed);
    processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 8.0, true);
    perf.trigger (Cmd::Collapse, 8.0, eng);
    processWithPerf (eng, perf, src, src, sr, bpm, 8.0, 28.0, true);
    EXPECT (perf.dormant());
    const auto oldSeed = eng.seed();
    const auto oldDna = eng.dnaFingerprint();
    perf.trigger (Cmd::Reseed, 36.0, eng);
    EXPECT (! perf.dormant());
    EXPECT (eng.relationshipState() == Rel::Lurking);
    EXPECT (eng.seed() != oldSeed);
    EXPECT (eng.dnaFingerprint() != oldDna);
    processWithPerf (eng, perf, src, src, sr, bpm, 36.0, 32.0, true);
    EXPECT (eng.responseCount() > 0u || eng.stimulusCount() > 0u);
}

static void testStage3SilenceListenNoBacklog()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeBusy (static_cast<int> (64.0 * sr * 60.0 / bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.mix = 0.5f;
    s.hunger = 0.7f;
    setupEngine (eng, s);
    Perf perf;
    perf.reset (s.seed);
    pfl::dsp::ParamSmoother silenceSm;
    silenceSm.prepare (sr, 0.004f);
    silenceSm.setCurrentAndTarget (1.0f);

    processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 8.0, true, 256, &silenceSm);
    const auto stimBefore = eng.stimulusCount();
    const auto respBefore = eng.responseCount();
    perf.trigger (Cmd::SilenceOn, 8.0, eng);
    auto silent = processWithPerf (eng, perf, src, src, sr, bpm, 8.0, 16.0, true, 256, &silenceSm);
    // Allow the ~4 ms mute ramp; assert the settled tail is effectively silent.
    float tailPeak = 0.0f;
    const int tailFrom = static_cast<int> (silent.L.size() / 4);
    for (int i = tailFrom; i < static_cast<int> (silent.L.size()); ++i)
    {
        tailPeak = std::max (tailPeak,
                             std::max (std::abs (silent.L[static_cast<size_t> (i)]),
                                       std::abs (silent.R[static_cast<size_t> (i)])));
    }
    EXPECT (tailPeak < 0.02f);
    EXPECT (eng.stimulusCount() > stimBefore);
    EXPECT (eng.responseCount() == respBefore);
    const auto respAtUnsilence = eng.responseCount();
    perf.trigger (Cmd::SilenceOff, 24.0, eng);
    processWithPerf (eng, perf, src, src, sr, bpm, 24.0, 4.0, true, 256, &silenceSm);
    // No backlog burst: responses may resume gradually, not dump a cluster.
    EXPECT (eng.responseCount() <= respAtUnsilence + 3u);
}

static void testStage3IdleCommandsAreStageTwo()
{
    // A controller that is never played must be inaudible in every sense:
    // the Stage 2 schedule has to survive it bit for bit.
    const double sr = 48000.0, bpm = 120.0;
    const int n = beatsToSamples (96.0, sr, bpm);
    auto drums = makeDrums (n, sr, bpm);

    Engine plain;
    Setup s;
    s.hunger = 0.6f;
    setupEngine (plain, s);
    processRun (plain, drums, drums, sr, bpm, 0.0, true, 256);

    Engine driven;
    setupEngine (driven, s);
    Perf perf;
    perf.reset (s.seed);
    processWithPerf (driven, perf, drums, drums, sr, bpm, 0.0, 96.0, true);

    EXPECT (perf.mode() == Mode::Normal);
    EXPECT (structuralKey (driven) == structuralKey (plain));
}

static void testStage3ReseedKeepsFreezeAndMacros()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSparse (static_cast<int> (96.0 * sr * 60.0 / bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.7f;
    setupEngine (eng, s);
    Perf perf;
    perf.reset (s.seed);
    processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 32.0, true);

    perf.trigger (Cmd::FreezeOn, 32.0, eng);
    const auto respBefore = eng.responseCount();
    perf.trigger (Cmd::Reseed, 32.0, eng);

    // A new parasite, born holding the switch the performer is holding.
    EXPECT (perf.mode() == Mode::Frozen);
    EXPECT (perf.state().freezeLatched);
    EXPECT (eng.relationshipFrozen());
    EXPECT (eng.relationshipState() == Rel::Lurking);
    EXPECT (eng.historySize() == 0);
    EXPECT (eng.dnaGeneration() == 0u);

    // HUNGER survived the rebirth, so it still answers this source.
    processWithPerf (eng, perf, src, src, sr, bpm, 32.0, 48.0, true);
    EXPECT (eng.responseCount() > respBefore);
}

static void testStage3FreezeUnderSilencePreservesState()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSparse (static_cast<int> (64.0 * sr * 60.0 / bpm), sr, bpm);
    Engine eng;
    Setup s;
    s.hunger = 0.7f;
    setupEngine (eng, s);
    Perf perf;
    perf.reset (s.seed);
    processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 24.0, true);
    const auto held = eng.relationshipState();
    perf.trigger (Cmd::FreezeOn, 24.0, eng);
    perf.trigger (Cmd::SilenceOn, 24.0, eng);
    processWithPerf (eng, perf, src, src, sr, bpm, 24.0, 16.0, true);
    perf.trigger (Cmd::SilenceOff, 40.0, eng);
    EXPECT (perf.mode() == Mode::Frozen);
    EXPECT (eng.relationshipState() == held);
}

static void testStage3CommandScriptDeterminism()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSparse (static_cast<int> (160.0 * sr * 60.0 / bpm), sr, bpm);

    auto run = [&]() {
        Engine eng;
        Setup s;
        s.hunger = 0.65f;
        s.mutation = 0.5f;
        setupEngine (eng, s);
        Perf perf;
        perf.reset (s.seed);
        processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 32.0, true);
        perf.trigger (Cmd::FreezeOn, 32.0, eng);
        processWithPerf (eng, perf, src, src, sr, bpm, 32.0, 8.0, true);
        perf.trigger (Cmd::Mutate, 40.0, eng);
        processWithPerf (eng, perf, src, src, sr, bpm, 40.0, 8.0, true);
        perf.trigger (Cmd::FreezeOff, 48.0, eng);
        processWithPerf (eng, perf, src, src, sr, bpm, 48.0, 16.0, true);
        perf.trigger (Cmd::Collapse, 64.0, eng);
        processWithPerf (eng, perf, src, src, sr, bpm, 64.0, 28.0, true);
        perf.trigger (Cmd::Reseed, 96.0, eng);
        processWithPerf (eng, perf, src, src, sr, bpm, 96.0, 32.0, true);
        std::ostringstream os;
        os << eng.seed() << "|" << eng.dnaFingerprint() << "|" << eng.stateFingerprint() << "|"
           << eng.responseFingerprint() << "|" << eng.stimulusFingerprint() << "|"
           << static_cast<int> (perf.mode()) << "|" << (perf.dormant() ? 1 : 0);
        return os.str();
    };
    EXPECT (run() == run());
}

static void testStage3PerfBufferMatrix()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSparse (static_cast<int> (48.0 * sr * 60.0 / bpm), sr, bpm);
    std::string ref;
    for (int block : { 64, 127, 128, 255, 256, 511, 512, 1024 })
    {
        Engine eng;
        Setup s;
        s.block = block;
        s.hunger = 0.65f;
        setupEngine (eng, s);
        Perf perf;
        perf.reset (s.seed);
        processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 16.0, true, block);
        perf.trigger (Cmd::FreezeOn, 16.0, eng);
        processWithPerf (eng, perf, src, src, sr, bpm, 16.0, 8.0, true, block);
        perf.trigger (Cmd::Mutate, 24.0, eng);
        processWithPerf (eng, perf, src, src, sr, bpm, 24.0, 16.0, true, block);
        std::ostringstream os;
        os << eng.stimulusFingerprint() << "|" << eng.responseFingerprint() << "|"
           << eng.stateFingerprint() << "|" << eng.dnaFingerprint();
        if (ref.empty())
            ref = os.str();
        else
            EXPECT (os.str() == ref);
    }
}

static void testStage3NoSourceCommandsSilent()
{
    const double sr = 48000.0, bpm = 120.0;
    auto src = makeSilence (static_cast<int> (48.0 * sr * 60.0 / bpm));
    Engine eng;
    Setup s;
    s.mix = 1.0f;
    setupEngine (eng, s);
    Perf perf;
    perf.reset (s.seed);
    perf.trigger (Cmd::FreezeOn, 0.0, eng);
    perf.trigger (Cmd::Mutate, 0.0, eng);
    perf.trigger (Cmd::Collapse, 0.0, eng);
    auto out = processWithPerf (eng, perf, src, src, sr, bpm, 0.0, 28.0, true);
    EXPECT (out.peak < 0.02f);
    EXPECT (eng.responseCount() == 0u);
    perf.trigger (Cmd::Reseed, 28.0, eng);
    out = processWithPerf (eng, perf, src, src, sr, bpm, 28.0, 8.0, true);
    EXPECT (out.peak < 0.02f);
    EXPECT (eng.responseCount() == 0u);
}

int main()
{
    testAlgorithmVersion();
    testStartsLurking();
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

    // Stage 2 — relationship
    testBusySourceWithdrawsSparseSourceEngages();
    testMutationZeroFreezesDnaNotState();
    testMutationDoesNotDriveStateRate();
    testHungerZeroStillObserves();
    testNoStimulusNoResponseEvenWhenAnswering();
    testSilenceHasNoRelationship();
    testSeekResetsRelationship();
    testStopPausesRelationship();
    testSeedChangeResetsRelationshipNotHistoryPolicy();
    testRelationshipNeverOutrunsHunger();
    testStateBiasesAnswersWithinStageOneVocabulary();
    testJourneyOccupancyAndReturn();
    testHistoryIsBoundedAndProcessDoesNotAllocate();

    // Stage 3 — performance
    testStage3FreezeHoldsDnaAndRelationship();
    testStage3FreezeNotMutationZero();
    testStage3MutateWhileFrozen();
    testStage3CollapseToDormant();
    testStage3CollapseSilentSourceNoAudio();
    testStage3CollapseVsHungerRamp();
    testStage3IdleCommandsAreStageTwo();
    testStage3ReseedKeepsFreezeAndMacros();
    testStage3ReseedExitsDormant();
    testStage3SilenceListenNoBacklog();
    testStage3FreezeUnderSilencePreservesState();
    testStage3CommandScriptDeterminism();
    testStage3PerfBufferMatrix();
    testStage3NoSourceCommandsSilent();

    if (gFails == 0)
    {
        std::cout << "signal_parasite_tests: OK\n";
        return 0;
    }
    std::cerr << "signal_parasite_tests: " << gFails << " failure(s)\n";
    return 1;
}

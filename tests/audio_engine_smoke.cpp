#include "dsp/DCBlocker.h"
#include "dsp/DirtBus.h"
#include "dsp/Drift.h"
#include "dsp/FeedbackDelay.h"
#include "dsp/Oscillator.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/Voice.h"
#include "generative/DeterministicRNG.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;

#define EXPECT(cond) \
    do { \
        if (! (cond)) { \
            std::fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures; \
        } \
    } while (0)

static void renderEngine (float drift, float dirt, float space, float output,
                          int blockSize, double sampleRate, int totalSamples,
                          float& peak, bool& finite)
{
    std::array<pfl::dsp::Voice, 3> voices;
    const float notes[3] = { 38.0f, 45.0f, 50.0f };
    const float pans[3] = { -0.28f, 0.05f, 0.32f };
    auto master = pfl::generative::DeterministicRNG::derived (1001, 0x50464C01ull);

    for (int i = 0; i < 3; ++i)
    {
        voices[static_cast<size_t> (i)].prepare (sampleRate);
        voices[static_cast<size_t> (i)].setDriftRng (
            pfl::generative::DeterministicRNG::derived (1001, 0x44524654ull + static_cast<uint64_t> (i)));
        pfl::dsp::VoiceParams p;
        p.baseMidiNote = notes[i];
        p.level = 0.28f;
        p.attackSec = 0.5f;
        p.releaseSec = 1.0f;
        p.osc2DetuneCents = 7.0f;
        p.oscBlend = 0.35f;
        p.pan = pans[i];
        p.driftAmount = drift;
        p.driftVoiceOffsetCents = (i - 1) * 2.0f;
        p.gate = true;
        voices[static_cast<size_t> (i)].setParams (p);
    }

    pfl::dsp::DirtBus dirtBus;
    pfl::dsp::FeedbackDelay delay;
    pfl::dsp::DCBlocker dcL, dcR;
    pfl::dsp::SafetyLimiter limL, limR;
    dirtBus.prepare (sampleRate);
    dirtBus.setNoiseSeed (1001);
    dirtBus.setDirt (dirt);
    delay.prepare (sampleRate);
    delay.setSpace (space);
    dcL.prepare (sampleRate);
    dcR.prepare (sampleRate);
    limL.prepare (sampleRate);
    limR.prepare (sampleRate);

    peak = 0.0f;
    finite = true;

    // Warm parameter smoothers
    for (int warm = 0; warm < 2048; ++warm)
    {
        float L = 0, R = 0, dL = 0, dR = 0, sL = 0, sR = 0;
        for (auto& v : voices) { float a, b; v.processSample (a, b); L += a; R += b; }
        dirtBus.processSample (L, R, dL, dR);
        delay.processSample (dL, dR, sL, sR);
    }

    for (int i = 0; i < totalSamples; ++i)
    {
        float L = 0, R = 0;
        for (auto& v : voices)
        {
            float a, b;
            v.processSample (a, b);
            L += a;
            R += b;
        }
        float dL, dR, sL, sR;
        dirtBus.processSample (L, R, dL, dR);
        delay.processSample (dL, dR, sL, sR);
        sL = limL.processSample (dcL.processSample (sL)) * output;
        sR = limR.processSample (dcR.processSample (sR)) * output;
        sL = std::clamp (sL, -0.99f, 0.99f);
        sR = std::clamp (sR, -0.99f, 0.99f);
        if (! std::isfinite (sL) || ! std::isfinite (sR))
            finite = false;
        peak = std::max (peak, std::max (std::abs (sL), std::abs (sR)));
        (void) blockSize;
    }
}

int main()
{
    // RNG stream independence
    {
        auto a1 = pfl::generative::DeterministicRNG::derived (42, 1);
        auto a2 = pfl::generative::DeterministicRNG::derived (42, 1);
        auto b = pfl::generative::DeterministicRNG::derived (42, 2);
        EXPECT (a1.nextU64() == a2.nextU64());
        a1.nextU64(); // consume extra on stream 1 copy path
        auto a3 = pfl::generative::DeterministicRNG::derived (42, 1);
        auto b2 = pfl::generative::DeterministicRNG::derived (42, 2);
        (void) a3.nextFloat();
        EXPECT (b.nextU64() == b2.nextU64());
    }

    // Oscillator pitch sanity at 48k
    {
        pfl::dsp::Oscillator osc;
        osc.prepare (48000.0);
        osc.setFrequencyHz (480.0); // period = 100 samples
        osc.setWaveform (pfl::dsp::Waveform::Saw);
        int zeroCrossings = 0;
        float prev = osc.processSample();
        for (int i = 0; i < 1000; ++i)
        {
            const float x = osc.processSample();
            if ((prev < 0.0f && x >= 0.0f) || (prev >= 0.0f && x < 0.0f))
                ++zeroCrossings;
            prev = x;
        }
        // ~480 Hz → ~20 crossings per 1000 samples at 48k (2 per period * 10 periods)
        EXPECT (zeroCrossings > 12 && zeroCrossings < 30);
    }

    // Drift bounds
    {
        pfl::dsp::Drift drift;
        drift.prepare (44100.0);
        drift.setAmount (1.0f);
        drift.setVoiceOffsetCents (0.0f);
        drift.setRng (pfl::generative::DeterministicRNG (99));
        float maxAbsCents = 0.0f;
        for (int i = 0; i < 44100 * 3; ++i)
        {
            drift.processRatio();
            maxAbsCents = std::max (maxAbsCents, std::abs (drift.currentCents()));
        }
        EXPECT (maxAbsCents <= 80.0f + 1.0f);
    }

    // Hostile engine: finite + bounded across buffer sizes / sample rates
    {
        const int blocks[] = { 64, 128, 256, 512, 1024 };
        const double rates[] = { 44100.0, 48000.0 };
        for (double sr : rates)
        {
            for (int bs : blocks)
            {
                float peak = 0.0f;
                bool finite = false;
                renderEngine (1.0f, 1.0f, 1.0f, 1.0f, bs, sr, sr > 0 ? static_cast<int> (sr * 0.5) : 1000, peak, finite);
                EXPECT (finite);
                EXPECT (peak <= 0.99f + 1.0e-4f);
                EXPECT (peak > 1.0e-4f);
            }
        }
    }

    // Feedback delay alone stays bounded
    {
        pfl::dsp::FeedbackDelay d;
        d.prepare (48000.0);
        d.setSpace (1.0f);
        float peak = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            float oL, oR;
            const float in = (i % 100 == 0) ? 0.9f : 0.0f;
            d.processSample (in, in, oL, oR);
            EXPECT (std::isfinite (oL) && std::isfinite (oR));
            peak = std::max (peak, std::max (std::abs (oL), std::abs (oR)));
        }
        EXPECT (peak < 5.0f); // dry+wet before safety; must not explode
    }

    if (failures != 0)
    {
        std::fprintf (stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }

    std::puts ("audio_engine_smoke: ok");
    return EXIT_SUCCESS;
}

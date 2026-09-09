#include "dsp/DCBlocker.h"
#include "dsp/DirtBus.h"
#include "dsp/FeedbackDelay.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/Voice.h"
#include "generative/DeterministicRNG.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

struct RenderSettings
{
    float drift = 0.35f;
    float dirt = 0.45f;
    float space = 0.55f;
    float output = 0.65f;
    float density = 1.0f;
    uint64_t seed = 1001;
};

static bool writeWav (const juce::File& outFile, const juce::AudioBuffer<float>& master, double sampleRate)
{
    outFile.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    auto fileStream = std::make_unique<juce::FileOutputStream> (outFile);
    if (fileStream->failedToOpen())
        return false;

    std::unique_ptr<juce::OutputStream> stream (std::move (fileStream));
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (2)
                             .withBitsPerSample (24);

    auto writer = wav.createWriterFor (stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer (master, 0, master.getNumSamples());
}

static int renderOne (const juce::File& outFile, double durationSec, const RenderSettings& s)
{
    const double sampleRate = 48000.0;
    constexpr int kNumVoices = 3;
    const std::array<float, kNumVoices> notes { 38.0f, 45.0f, 50.0f };
    const std::array<float, kNumVoices> pans { -0.28f, 0.05f, 0.32f };
    const std::array<float, kNumVoices> driftOff { -3.5f, 2.0f, 1.5f };

    std::array<pfl::dsp::Voice, kNumVoices> voices;
    const int active = 1 + static_cast<int> (std::round (s.density * static_cast<float> (kNumVoices - 1)));

    for (int i = 0; i < kNumVoices; ++i)
    {
        voices[static_cast<size_t> (i)].prepare (sampleRate);
        voices[static_cast<size_t> (i)].setDriftRng (
            pfl::generative::DeterministicRNG::derived (s.seed, 0x44524654ull + static_cast<uint64_t> (i)));

        pfl::dsp::VoiceParams p;
        p.baseMidiNote = notes[static_cast<size_t> (i)];
        p.level = 0.28f;
        p.attackSec = 1.5f;
        p.releaseSec = 2.0f;
        p.osc2DetuneCents = 6.0f + 2.5f * static_cast<float> (i);
        p.oscBlend = 0.3f;
        p.pan = pans[static_cast<size_t> (i)];
        p.driftAmount = s.drift;
        p.driftVoiceOffsetCents = driftOff[static_cast<size_t> (i)] * (0.35f + s.drift);
        p.gate = i < active;
        voices[static_cast<size_t> (i)].setParams (p);
    }

    pfl::dsp::DirtBus dirtBus;
    pfl::dsp::FeedbackDelay space;
    pfl::dsp::DCBlocker dcL, dcR;
    pfl::dsp::SafetyLimiter limL, limR;
    dirtBus.prepare (sampleRate);
    dirtBus.setNoiseSeed (s.seed);
    dirtBus.setDirt (s.dirt);
    space.prepare (sampleRate);
    space.setSpace (s.space);
    dcL.prepare (sampleRate);
    dcR.prepare (sampleRate);
    limL.prepare (sampleRate);
    limR.prepare (sampleRate);

    const int totalSamples = static_cast<int> (durationSec * sampleRate);
    juce::AudioBuffer<float> master (2, totalSamples);

    float peak = 0.0f;
    for (int i = 0; i < totalSamples; ++i)
    {
        float mixL = 0.0f, mixR = 0.0f;
        for (auto& v : voices)
        {
            float a, b;
            v.processSample (a, b);
            mixL += a;
            mixR += b;
        }

        float dL, dR, sL, sR;
        dirtBus.processSample (mixL, mixR, dL, dR);
        space.processSample (dL, dR, sL, sR);

        float L = limL.processSample (dcL.processSample (sL)) * s.output;
        float R = limR.processSample (dcR.processSample (sR)) * s.output;
        L = std::clamp (L, -0.99f, 0.99f);
        R = std::clamp (R, -0.99f, 0.99f);

        if (! std::isfinite (L) || ! std::isfinite (R))
        {
            std::cerr << "non-finite at " << i << "\n";
            return EXIT_FAILURE;
        }

        master.setSample (0, i, L);
        master.setSample (1, i, R);
        peak = std::max (peak, std::max (std::abs (L), std::abs (R)));
    }

    if (peak < 1.0e-3f)
    {
        std::cerr << "near silence peak=" << peak << "\n";
        return EXIT_FAILURE;
    }

    if (! writeWav (outFile, master, sampleRate))
    {
        std::cerr << "WAV write failed: " << outFile.getFullPathName() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Wrote " << outFile.getFullPathName() << " peak=" << peak
              << " drift=" << s.drift << " dirt=" << s.dirt << " space=" << s.space << "\n";
    return EXIT_SUCCESS;
}

int main (int argc, char** argv)
{
    const double duration = (argc > 1) ? std::atof (argv[1]) : 90.0;
    const juce::File outDir = (argc > 2)
                                  ? juce::File (argv[2])
                                  : juce::File::getCurrentWorkingDirectory().getChildFile ("renders");

    RenderSettings nominal { 0.35f, 0.45f, 0.55f, 0.65f, 1.0f, 1001 };
    RenderSettings clean { 0.05f, 0.05f, 0.1f, 0.6f, 1.0f, 1001 };
    RenderSettings hostile { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1001 };

    int rc = 0;
    rc |= renderOne (outDir.getChildFile ("drone-organism-audio-engine-v0.1.wav"), duration, nominal);
    rc |= renderOne (outDir.getChildFile ("clean.wav"), std::min (duration, 30.0), clean);
    rc |= renderOne (outDir.getChildFile ("nominal.wav"), std::min (duration, 30.0), nominal);
    rc |= renderOne (outDir.getChildFile ("hostile.wav"), std::min (duration, 30.0), hostile);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

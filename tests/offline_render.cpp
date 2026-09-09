#include "dsp/DCBlocker.h"
#include "dsp/DirtBus.h"
#include "dsp/FeedbackDelay.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/Voice.h"
#include "generative/Composer.h"
#include "generative/DeterministicRNG.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

struct RenderJob
{
    std::string name;
    uint64_t seed = 1001;
    float density = 0.45f;
    float mutation = 0.35f;
    float drift = 0.35f;
    float dirt = 0.45f;
    float space = 0.55f;
    float output = 0.65f;
    double bpm = 72.0;
    double durationSec = 180.0;
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

static int renderJob (const juce::File& outDir, const RenderJob& job)
{
    const double sampleRate = 48000.0;
    constexpr int kBlock = 256;
    constexpr int kNumVoices = 3;
    const std::array<float, kNumVoices> pans { -0.28f, 0.05f, 0.32f };
    const std::array<float, kNumVoices> driftOff { -3.5f, 2.0f, 1.5f };

    pfl::generative::Composer composer;
    composer.setParams ({ job.density, job.mutation });
    composer.reseed (job.seed);

    std::array<pfl::dsp::Voice, kNumVoices> voices;
    for (int i = 0; i < kNumVoices; ++i)
    {
        voices[static_cast<size_t> (i)].prepare (sampleRate);
        voices[static_cast<size_t> (i)].setDriftRng (
            pfl::generative::DeterministicRNG::derived (job.seed, 0x44524654ull + static_cast<uint64_t> (i)));
    }

    pfl::dsp::DirtBus dirtBus;
    pfl::dsp::FeedbackDelay space;
    pfl::dsp::DCBlocker dcL, dcR;
    pfl::dsp::SafetyLimiter limL, limR;
    dirtBus.prepare (sampleRate);
    dirtBus.setNoiseSeed (job.seed);
    dirtBus.setDirt (job.dirt);
    space.prepare (sampleRate);
    space.setSpace (job.space);
    dcL.prepare (sampleRate);
    dcR.prepare (sampleRate);
    limL.prepare (sampleRate);
    limR.prepare (sampleRate);

    const int totalSamples = static_cast<int> (job.durationSec * sampleRate);
    juce::AudioBuffer<float> master (2, totalSamples);
    master.clear();

    double ppq = 0.0;
    const double beatsPerSec = job.bpm / 60.0;
    float peak = 0.0f;
    int written = 0;

    while (written < totalSamples)
    {
        const int n = std::min (kBlock, totalSamples - written);
        const double blockBeats = (static_cast<double> (n) / sampleRate) * beatsPerSec;
        const double ppqEnd = ppq + blockBeats;

        pfl::generative::ClockSnapshot snap;
        snap.playing = true;
        snap.ppq = ppq;
        snap.tempoBpm = job.bpm;
        composer.clock().advance (snap);
        composer.processTimeRange (ppq, ppqEnd, true);

        for (int i = 0; i < kNumVoices; ++i)
        {
            const auto& cv = composer.voice (i);
            pfl::dsp::VoiceParams p;
            p.baseMidiNote = static_cast<float> (cv.pitch.midiNote);
            p.level = 0.28f;
            p.attackSec = 1.5f;
            p.releaseSec = 2.0f;
            p.osc2DetuneCents = 6.0f + 2.5f * static_cast<float> (i);
            p.oscBlend = 0.3f;
            p.pan = pans[static_cast<size_t> (i)];
            p.driftAmount = job.drift;
            p.driftVoiceOffsetCents = driftOff[static_cast<size_t> (i)] * (0.35f + job.drift);
            p.gate = cv.active;
            voices[static_cast<size_t> (i)].setParams (p);
        }

        for (int i = 0; i < n; ++i)
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
            float L = limL.processSample (dcL.processSample (sL)) * job.output;
            float R = limR.processSample (dcR.processSample (sR)) * job.output;
            L = std::clamp (L, -0.99f, 0.99f);
            R = std::clamp (R, -0.99f, 0.99f);
            if (! std::isfinite (L) || ! std::isfinite (R))
            {
                std::cerr << "non-finite in " << job.name << "\n";
                return EXIT_FAILURE;
            }
            master.setSample (0, written + i, L);
            master.setSample (1, written + i, R);
            peak = std::max (peak, std::max (std::abs (L), std::abs (R)));
        }

        ppq = ppqEnd;
        written += n;
    }

    if (peak < 1.0e-4f)
    {
        std::cerr << "near silence: " << job.name << "\n";
        return EXIT_FAILURE;
    }

    const auto out = outDir.getChildFile (job.name);
    if (! writeWav (out, master, sampleRate))
    {
        std::cerr << "write failed: " << out.getFullPathName() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Wrote " << out.getFullPathName() << " peak=" << peak
              << " seed=" << job.seed << " dens=" << job.density
              << " mut=" << job.mutation << "\n";
    return EXIT_SUCCESS;
}

int main (int argc, char** argv)
{
    const juce::File outDir = (argc > 1) ? juce::File (argv[1])
                                         : juce::File::getCurrentWorkingDirectory().getChildFile ("renders/phase2");

    const float drift = 0.35f, dirt = 0.45f, space = 0.55f, output = 0.65f;
    const double bpm = 72.0;
    const double dur = 180.0;

    std::vector<RenderJob> jobs = {
        { "seed-1001.wav", 1001, 0.45f, 0.35f, drift, dirt, space, output, bpm, dur },
        { "seed-2002.wav", 2002, 0.45f, 0.35f, drift, dirt, space, output, bpm, dur },
        { "seed-3003.wav", 3003, 0.45f, 0.35f, drift, dirt, space, output, bpm, dur },
        { "seed-2002-density-20.wav", 2002, 0.20f, 0.35f, drift, dirt, space, output, bpm, 120.0 },
        { "seed-2002-density-50.wav", 2002, 0.50f, 0.35f, drift, dirt, space, output, bpm, 120.0 },
        { "seed-2002-density-80.wav", 2002, 0.80f, 0.35f, drift, dirt, space, output, bpm, 120.0 },
        { "seed-2002-mutation-10.wav", 2002, 0.45f, 0.10f, drift, dirt, space, output, bpm, 120.0 },
        { "seed-2002-mutation-40.wav", 2002, 0.45f, 0.40f, drift, dirt, space, output, bpm, 120.0 },
        { "seed-2002-mutation-80.wav", 2002, 0.45f, 0.80f, drift, dirt, space, output, bpm, 120.0 },
    };

    int rc = 0;
    for (auto& j : jobs)
        rc |= renderJob (outDir, j);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

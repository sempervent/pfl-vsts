#include "dsp/DCBlocker.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/Voice.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

int main (int argc, char** argv)
{
    const double sampleRate = 48000.0;
    const double durationSec = (argc > 1) ? std::atof (argv[1]) : 3.0;
    const juce::File outFile = (argc > 2)
                                   ? juce::File (argv[2])
                                   : juce::File::getCurrentWorkingDirectory().getChildFile ("renders/phase1-drone.wav");

    outFile.getParentDirectory().createDirectory();

    constexpr int kNumVoices = 4;
    const std::array<float, kNumVoices> notes { 38.0f, 45.0f, 53.0f, 60.0f };
    const std::array<float, kNumVoices> driftOffsets { -4.0f, 2.5f, -1.5f, 3.0f };

    std::array<pfl::dsp::Voice, kNumVoices> voices;
    for (int i = 0; i < kNumVoices; ++i)
    {
        voices[static_cast<size_t> (i)].prepare (sampleRate);
        pfl::dsp::VoiceParams p;
        p.baseMidiNote = notes[static_cast<size_t> (i)];
        p.level = 0.22f;
        p.attackSec = 1.5f;
        p.releaseSec = 2.0f;
        p.osc2DetuneCents = 6.0f + static_cast<float> (i);
        p.oscBlend = 0.35f;
        p.filterCutoffHz = 800.0f + 200.0f * static_cast<float> (i);
        p.filterRes = 0.15f;
        p.satDrive = 0.3f;
        p.driftAmount = 0.4f;
        p.driftVoiceOffsetCents = driftOffsets[static_cast<size_t> (i)];
        p.gate = true;
        voices[static_cast<size_t> (i)].setParams (p);
    }

    pfl::dsp::DCBlocker dcL, dcR;
    pfl::dsp::SafetyLimiter limL, limR;
    dcL.prepare (sampleRate);
    dcR.prepare (sampleRate);
    limL.prepare (sampleRate);
    limR.prepare (sampleRate);

    const int totalSamples = static_cast<int> (durationSec * sampleRate);
    juce::AudioBuffer<float> master (2, totalSamples);

    float peak = 0.0f;
    for (int i = 0; i < totalSamples; ++i)
    {
        float mix = 0.0f;
        for (auto& v : voices)
            mix += v.processSample();

        mix *= 0.75f;
        float L = limL.processSample (dcL.processSample (mix));
        float R = limR.processSample (dcR.processSample (mix));
        master.setSample (0, i, L);
        master.setSample (1, i, R);
        peak = std::max (peak, std::max (std::abs (L), std::abs (R)));

        if (! std::isfinite (L) || ! std::isfinite (R))
        {
            std::cerr << "non-finite sample at " << i << "\n";
            return EXIT_FAILURE;
        }
    }

    if (peak < 1.0e-3f)
    {
        std::cerr << "render near silence, peak=" << peak << "\n";
        return EXIT_FAILURE;
    }

    juce::WavAudioFormat wav;
    auto fileStream = std::make_unique<juce::FileOutputStream> (outFile);
    if (fileStream->failedToOpen())
    {
        std::cerr << "cannot open " << outFile.getFullPathName() << "\n";
        return EXIT_FAILURE;
    }

    std::unique_ptr<juce::OutputStream> stream (std::move (fileStream));
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (2)
                             .withBitsPerSample (24);

    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (master, 0, totalSamples))
    {
        std::cerr << "WAV write failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Wrote " << outFile.getFullPathName() << " peak=" << peak << "\n";
    return EXIT_SUCCESS;
}

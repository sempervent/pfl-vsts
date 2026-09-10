#include "dsp/RuinEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void writeWav (const fs::path& path, const std::vector<float>& L, const std::vector<float>& R, double sr)
{
    juce::AudioBuffer<float> buf (2, static_cast<int> (L.size()));
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        buf.setSample (0, i, L[static_cast<size_t> (i)]);
        buf.setSample (1, i, R[static_cast<size_t> (i)]);
    }

    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> stream (new juce::FileOutputStream (juce::File (path.string())));
    if (stream->failedToOpen())
    {
        std::cerr << "cannot write " << path << "\n";
        return;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (stream.release(), sr, 2, 24, {}, 0));
    if (writer)
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

static void renderFile (const fs::path& out,
                        uint64_t seed, float mix, float age, float inst, float output,
                        double sr, double seconds, double bpm)
{
    const int n = static_cast<int> (sr * seconds);
    std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        // Synthetic "instrument" bed: two partials + slow amp motion
        float s = 0.35f * std::sin (2.0 * 3.141592653589793 * 110.0 * t)
                + 0.18f * std::sin (2.0 * 3.141592653589793 * 165.0 * t)
                + 0.12f * std::sin (2.0 * 3.141592653589793 * 220.0 * t);
        s *= 0.75f + 0.25f * std::sin (2.0 * 3.141592653589793 * t / 7.0);
        L[static_cast<size_t> (i)] = s;
        R[static_cast<size_t> (i)] = s * 0.97f;
    }

    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (seed);
    eng.setMix (mix);
    eng.setAge (age);
    eng.setInstability (inst);
    eng.setOutput (output);
    eng.snapMacros();

    const int block = 256;
    const double beatsPerSample = (bpm / 60.0) / sr;
    for (int done = 0; done < n; done += block)
    {
        const int m = std::min (block, n - done);
        eng.process (L.data() + done, R.data() + done, m, true,
                     static_cast<double> (done) * beatsPerSample, bpm);
    }

    writeWav (out, L, R, sr);
    std::cout << "wrote " << out << "\n";
}

int main (int argc, char** argv)
{
    juce::ignoreUnused (argc, argv);
    const fs::path dir = "renders/ruin-engine/stage1";
    fs::create_directories (dir);

    const double sr = 48000.0;
    const double bpm = 72.0;
    const uint64_t seed = 2002;

    renderFile (dir / "clean.wav", seed, 0.0f, 0.0f, 0.0f, 0.9f, sr, 8.0, bpm);
    renderFile (dir / "age-025.wav", seed, 0.7f, 0.25f, 0.35f, 0.9f, sr, 12.0, bpm);
    renderFile (dir / "age-050.wav", seed, 0.7f, 0.50f, 0.35f, 0.9f, sr, 12.0, bpm);
    renderFile (dir / "age-075.wav", seed, 0.7f, 0.75f, 0.35f, 0.9f, sr, 12.0, bpm);
    renderFile (dir / "age-100.wav", seed, 0.85f, 1.00f, 0.35f, 0.9f, sr, 12.0, bpm);

    renderFile (dir / "instability-000.wav", seed, 0.7f, 0.5f, 0.00f, 0.9f, sr, 20.0, bpm);
    renderFile (dir / "instability-050.wav", seed, 0.7f, 0.5f, 0.50f, 0.9f, sr, 20.0, bpm);
    renderFile (dir / "instability-100.wav", seed, 0.7f, 0.5f, 1.00f, 0.9f, sr, 20.0, bpm);

    renderFile (dir / "stage1-evolution.wav", seed, 0.65f, 0.45f, 0.45f, 0.9f, sr, 180.0, bpm);

    return 0;
}

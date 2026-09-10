#include "dsp/RuinEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <filesystem>
#include <fstream>
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
        return;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (stream.release(), sr, 2, 24, {}, 0));
    if (writer)
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    std::cout << "wrote " << path << "\n";
}

static void fillSource (std::vector<float>& L, std::vector<float>& R, double sr)
{
    for (size_t i = 0; i < L.size(); ++i)
    {
        const double t = static_cast<double> (i) / sr;
        float s = 0.35f * std::sin (2.0 * 3.141592653589793 * 110.0 * t)
                + 0.18f * std::sin (2.0 * 3.141592653589793 * 165.0 * t)
                + 0.12f * std::sin (2.0 * 3.141592653589793 * 220.0 * t);
        s *= 0.75f + 0.25f * std::sin (2.0 * 3.141592653589793 * t / 7.0);
        L[i] = s;
        R[i] = s * 0.97f;
    }
}

static void renderJourney (const fs::path& wav, const fs::path& trace,
                           uint64_t seed, float mix, float age, float inst, float output,
                           double sr, double seconds, double bpm)
{
    const int n = static_cast<int> (sr * seconds);
    std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
    fillSource (L, R, sr);

    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (seed);
    eng.setMix (mix);
    eng.setAge (age);
    eng.setInstability (inst);
    eng.setOutput (output);
    eng.snapMacros();

    std::ofstream tr (trace);
    auto prev = eng.processingState();
    tr << "beat 0\nSTATE " << pfl::dsp::ruinStateName (prev) << "\n";

    const int block = 256;
    const double beatsPerSample = (bpm / 60.0) / sr;
    for (int done = 0; done < n; done += block)
    {
        const int m = std::min (block, n - done);
        const double ppq = static_cast<double> (done) * beatsPerSample;
        eng.process (L.data() + done, R.data() + done, m, true, ppq, bpm);
        const auto st = eng.processingState();
        if (st != prev)
        {
            tr << "\nbeat " << ppq << "\nTRANSITION " << pfl::dsp::ruinStateName (prev)
               << " → " << pfl::dsp::ruinStateName (st) << "\n";
            prev = st;
        }
    }
    writeWav (wav, L, R, sr);
}

static void renderForced (const fs::path& out, pfl::dsp::RuinProcessingState state,
                          double sr, double seconds, double bpm)
{
    const int n = static_cast<int> (sr * seconds);
    std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
    fillSource (L, R, sr);
    pfl::dsp::RuinEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (0.7f);
    eng.setAge (0.65f);
    eng.setInstability (0.45f);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.forceProcessingState (true, state);
    const int block = 256;
    const double beatsPerSample = (bpm / 60.0) / sr;
    for (int done = 0; done < n; done += block)
    {
        const int m = std::min (block, n - done);
        eng.process (L.data() + done, R.data() + done, m, true,
                     static_cast<double> (done) * beatsPerSample, bpm);
    }
    writeWav (out, L, R, sr);
}

int main (int argc, char** argv)
{
    const bool stage2 = (argc > 1 && std::string (argv[1]) == "--stage2");
    const double sr = 48000.0;
    const double bpm = 72.0;

    if (! stage2)
    {
        const fs::path dir = "renders/ruin-engine/stage1";
        fs::create_directories (dir);
        renderJourney (dir / "stage1-evolution.wav", dir / "stage1-trace.txt",
                       2002, 0.65f, 0.45f, 0.45f, 0.9f, sr, 60.0, bpm);
        return 0;
    }

    const fs::path dir = "renders/ruin-engine/stage2";
    fs::create_directories (dir);

    using S = pfl::dsp::RuinProcessingState;
    renderForced (dir / "state-intact.wav", S::Intact, sr, 12.0, bpm);
    renderForced (dir / "state-weathered.wav", S::Weathered, sr, 12.0, bpm);
    renderForced (dir / "state-fractured.wav", S::Fractured, sr, 12.0, bpm);
    renderForced (dir / "state-ruined.wav", S::Ruined, sr, 12.0, bpm);
    renderForced (dir / "state-recovering.wav", S::Recovering, sr, 12.0, bpm);

    renderJourney (dir / "journey-seed-1001.wav", dir / "journey-seed-1001-trace.txt",
                   1001, 0.70f, 0.50f, 0.50f, 0.9f, sr, 180.0, bpm);
    renderJourney (dir / "journey-seed-2002.wav", dir / "journey-seed-2002-trace.txt",
                   2002, 0.70f, 0.50f, 0.50f, 0.9f, sr, 180.0, bpm);
    renderJourney (dir / "journey-seed-3003.wav", dir / "journey-seed-3003-trace.txt",
                   3003, 0.70f, 0.50f, 0.50f, 0.9f, sr, 180.0, bpm);

    renderJourney (dir / "age-020-instability-050.wav", dir / "age-020-instability-050-trace.txt",
                   2002, 0.70f, 0.20f, 0.50f, 0.9f, sr, 90.0, bpm);
    renderJourney (dir / "age-050-instability-050.wav", dir / "age-050-instability-050-trace.txt",
                   2002, 0.70f, 0.50f, 0.50f, 0.9f, sr, 90.0, bpm);
    renderJourney (dir / "age-100-instability-050.wav", dir / "age-100-instability-050-trace.txt",
                   2002, 0.70f, 1.00f, 0.50f, 0.9f, sr, 90.0, bpm);
    renderJourney (dir / "age-050-instability-010.wav", dir / "age-050-instability-010-trace.txt",
                   2002, 0.70f, 0.50f, 0.10f, 0.9f, sr, 90.0, bpm);
    renderJourney (dir / "age-050-instability-100.wav", dir / "age-050-instability-100-trace.txt",
                   2002, 0.70f, 0.50f, 1.00f, 0.9f, sr, 90.0, bpm);

    return 0;
}

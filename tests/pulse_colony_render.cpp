#include "dsp/PulseColonyEngine.h"

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

static void fillTone (std::vector<float>& L, std::vector<float>& R, double sr)
{
    for (size_t i = 0; i < L.size(); ++i)
    {
        const double t = static_cast<double> (i) / sr;
        L[i] = 0.35f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * 220.0 * t));
        R[i] = 0.35f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * 277.0 * t));
    }
}

static void renderVariant (const fs::path& wav, const fs::path& trace,
                           float mix, float dens, float mut, float motion,
                           double sr, double bpm, double beats)
{
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (beats / bps);
    std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
    fillTone (L, R, sr);

    pfl::dsp::PulseColonyEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (mix);
    eng.setDensity (dens);
    eng.setMutation (mut);
    eng.setMotion (motion);
    eng.setOutput (0.85f);
    eng.snapMacros();
    eng.forceRebuild (0);
    eng.setTraceEnabled (true);

    for (int done = 0; done < n; done += 256)
    {
        const int m = std::min (256, n - done);
        eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
    }

    std::ofstream tr (trace);
    tr << "Pulse Colony Stage 1\n";
    tr << "dens=" << dens << " mut=" << mut << " motion=" << motion
       << " gen=" << eng.generation()
       << " occ=" << eng.dna().occupancy()
       << " bars=" << eng.dna().lengthBars
       << " windows=" << eng.dna().windowCount
       << " opens=" << eng.opens().size() << "\n";
    for (const auto& e : eng.traces())
        tr << "beat " << e.beat << " " << e.detail << "\n";
    for (const auto& e : eng.opens())
        tr << "open beat=" << e.beat << " dur=" << e.durationBeats
           << " pan=" << e.pan << "\n";

    writeWav (wav, L, R, sr);
}

int main()
{
    const double sr = 48000.0, bpm = 120.0;
    const fs::path dir = "renders/pulse-colony/stage1";
    fs::create_directories (dir);

    {
        const int n = static_cast<int> (sr * 20.0);
        std::vector<float> L (n), R (n);
        fillTone (L, R, sr);
        writeWav (dir / "dry-source.wav", L, R, sr);
    }

    renderVariant (dir / "density020.wav", dir / "density020-trace.txt", 1.0f, 0.20f, 0.35f, 0.0f, sr, bpm, 64.0);
    renderVariant (dir / "density050.wav", dir / "density050-trace.txt", 1.0f, 0.50f, 0.35f, 0.0f, sr, bpm, 64.0);
    renderVariant (dir / "density100.wav", dir / "density100-trace.txt", 1.0f, 1.00f, 0.35f, 0.0f, sr, bpm, 64.0);

    renderVariant (dir / "mutation000.wav", dir / "mutation000-trace.txt", 1.0f, 0.50f, 0.00f, 0.0f, sr, bpm, 96.0);
    renderVariant (dir / "mutation050.wav", dir / "mutation050-trace.txt", 1.0f, 0.50f, 0.50f, 0.0f, sr, bpm, 96.0);
    renderVariant (dir / "mutation100.wav", dir / "mutation100-trace.txt", 1.0f, 0.50f, 1.00f, 0.0f, sr, bpm, 96.0);

    renderVariant (dir / "motion000.wav", dir / "motion000-trace.txt", 1.0f, 0.50f, 0.0f, 0.00f, sr, bpm, 48.0);
    renderVariant (dir / "motion050.wav", dir / "motion050-trace.txt", 1.0f, 0.50f, 0.0f, 0.50f, sr, bpm, 48.0);
    renderVariant (dir / "motion100.wav", dir / "motion100-trace.txt", 1.0f, 0.50f, 0.0f, 1.00f, sr, bpm, 48.0);

    renderVariant (dir / "stage1-journey-wet.wav", dir / "stage1-journey-trace.txt",
                   1.0f, 0.50f, 0.35f, 0.35f, sr, bpm, 128.0);

    {
        const double beats = 128.0;
        const double bps = (bpm / 60.0) / sr;
        const int n = static_cast<int> (beats / bps);
        std::vector<float> dryL (n), dryR (n), wetL (n), wetR (n);
        fillTone (dryL, dryR, sr);
        wetL = dryL;
        wetR = dryR;
        pfl::dsp::PulseColonyEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (0.70f);
        eng.setDensity (0.50f);
        eng.setMutation (0.35f);
        eng.setMotion (0.35f);
        eng.setOutput (0.85f);
        eng.snapMacros();
        eng.forceRebuild (0);
        for (int done = 0; done < n; done += 256)
        {
            const int m = std::min (256, n - done);
            eng.process (wetL.data() + done, wetR.data() + done, m, true, done * bps, bpm);
        }
        writeWav (dir / "stage1-journey-mix.wav", wetL, wetR, sr);
    }

    return 0;
}

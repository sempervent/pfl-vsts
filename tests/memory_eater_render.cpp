#include "dsp/MemoryEaterEngine.h"

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

static void fillIdent (std::vector<float>& L, std::vector<float>& R, double sr, double bpm)
{
    const double bps = (bpm / 60.0) / sr;
    for (size_t i = 0; i < L.size(); ++i)
    {
        const double beat = static_cast<double> (i) * bps;
        const int bi = static_cast<int> (std::floor (beat)) % 8;
        const double freq = 110.0 * (1 + bi);
        const double t = static_cast<double> (i) / sr;
        float s = 0.32f * std::sin (2.0 * 3.141592653589793 * freq * t);
        const double frac = beat - std::floor (beat);
        if (frac < 0.015)
            s += 0.45f * (1.0f - static_cast<float> (frac / 0.015));
        L[i] = s;
        R[i] = s * 0.97f;
    }
}

static void renderVariant (const fs::path& wav, const fs::path& trace,
                           float mix, float hunger, float memory,
                           double sr, double bpm, double beats)
{
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (beats / bps);
    std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
    fillIdent (L, R, sr, bpm);

    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (3003);
    eng.setMix (mix);
    eng.setHunger (hunger);
    eng.setMemory (memory);
    eng.setOutput (0.9f);
    eng.snapMacros();
    eng.setTraceEnabled (true);

    const int block = 256;
    for (int done = 0; done < n; done += block)
    {
        const int m = std::min (block, n - done);
        eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
    }

    std::ofstream tr (trace);
    tr << "Memory Eater Stage 1 recall trace\n";
    tr << "mix=" << mix << " hunger=" << hunger << " memory=" << memory << "\n";
    tr << "recalls=" << eng.events().size() << "\n";
    for (const auto& e : eng.events())
    {
        tr << "beat " << e.eventBeat
           << " RECALL sourceBeat " << e.sourceBeat
           << " lookback " << e.lookbackBeats
           << " fragment " << e.fragmentBeats
           << " duration " << e.durationBeats
           << " loops " << e.loops << "\n";
    }
    writeWav (wav, L, R, sr);
    std::cout << "  recalls=" << eng.events().size() << " ramMiB="
              << (eng.historyRamBytes() / (1024.0 * 1024.0)) << "\n";
}

int main()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    const fs::path dir = "renders/memory-eater/stage1";
    fs::create_directories (dir);

    // Dry source reference
    {
        const int n = static_cast<int> (sr * 30.0);
        std::vector<float> L (n), R (n);
        fillIdent (L, R, sr, bpm);
        writeWav (dir / "dry-source.wav", L, R, sr);
    }

    renderVariant (dir / "memory-hunger025.wav", dir / "memory-hunger025-trace.txt",
                   0.55f, 0.25f, 0.55f, sr, bpm, 96.0);
    renderVariant (dir / "memory-hunger050.wav", dir / "memory-hunger050-trace.txt",
                   0.55f, 0.50f, 0.55f, sr, bpm, 96.0);
    renderVariant (dir / "memory-hunger100.wav", dir / "memory-hunger100-trace.txt",
                   0.55f, 1.00f, 0.55f, sr, bpm, 96.0);

    renderVariant (dir / "memory-short.wav", dir / "memory-short-trace.txt",
                   0.55f, 0.55f, 0.20f, sr, bpm, 96.0);
    renderVariant (dir / "memory-medium.wav", dir / "memory-medium-trace.txt",
                   0.55f, 0.55f, 0.55f, sr, bpm, 96.0);
    renderVariant (dir / "memory-deep.wav", dir / "memory-deep-trace.txt",
                   0.55f, 0.55f, 0.95f, sr, bpm, 96.0);

    renderVariant (dir / "stage1-journey.wav", dir / "stage1-journey-trace.txt",
                   0.50f, 0.50f, 0.65f, sr, bpm, 192.0);
    renderVariant (dir / "wet-only.wav", dir / "wet-only-trace.txt",
                   1.00f, 0.55f, 0.65f, sr, bpm, 128.0);

    return 0;
}

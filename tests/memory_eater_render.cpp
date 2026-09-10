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

static const char* kindName (pfl::dsp::EcologyTraceEvent::Kind k)
{
    using K = pfl::dsp::EcologyTraceEvent::Kind;
    switch (k)
    {
        case K::Promote: return "PROMOTE";
        case K::Recall: return "RECALL";
        case K::Decay: return "DECAY";
        case K::Forget: return "FORGET";
        case K::Replace: return "REPLACE";
    }
    return "?";
}

static void writeLifecycle (const fs::path& path, const pfl::dsp::MemoryEaterEngine& eng)
{
    std::ofstream tr (path);
    tr << "Memory Eater Stage 2 ecology lifecycle\n";
    tr << "promotions=" << eng.ecology().promotions()
       << " storedRecalls=" << eng.ecology().recallsStored()
       << " forgotten=" << eng.ecology().forgotten()
       << " replacements=" << eng.ecology().replacements()
       << " occupied=" << eng.ecology().occupiedCount() << "\n";
    for (const auto& e : eng.ecology().traces())
    {
        tr << "beat " << e.beat << " " << kindName (e.kind)
           << " memory=" << e.memoryId
           << " slot=" << e.slot
           << " strength=" << e.strength
           << " fatigue=" << e.fatigue
           << " sourceBeat=" << e.sourceBeat
           << " fragment=" << e.fragmentBeats << "\n";
    }
    int stored = 0, recent = 0;
    for (const auto& e : eng.events())
        (e.fromStored ? stored : recent)++;
    tr << "recallEvents recent=" << recent << " stored=" << stored << "\n";
    for (const auto& e : eng.events())
    {
        tr << "beat " << e.eventBeat
           << (e.fromStored ? " STORED" : " RECENT")
           << " memory=" << e.memoryId
           << " sourceBeat=" << e.sourceBeat
           << " lookback=" << e.lookbackBeats
           << " fragment=" << e.fragmentBeats
           << " duration=" << e.durationBeats << "\n";
    }
}

static pfl::dsp::MemoryEaterEngine renderVariant (const fs::path& wav, const fs::path& lifecycle,
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
    eng.setOutput (0.85f);
    eng.snapMacros();
    eng.setTraceEnabled (true);

    const int block = 256;
    for (int done = 0; done < n; done += block)
    {
        const int m = std::min (block, n - done);
        eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
    }

    writeLifecycle (lifecycle, eng);
    writeWav (wav, L, R, sr);

    int stored = 0;
    for (const auto& e : eng.events())
        if (e.fromStored)
            ++stored;
    std::cout << "  recalls=" << eng.events().size() << " stored=" << stored
              << " promotions=" << eng.ecology().promotions()
              << " totalRamMiB=" << (eng.totalRamBytes() / (1024.0 * 1024.0)) << "\n";
    return eng;
}

int main()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    const fs::path dir = "renders/memory-eater/stage2";
    fs::create_directories (dir);

    // Dry source (send simulation)
    {
        const int n = static_cast<int> (sr * 30.0);
        std::vector<float> L (n), R (n);
        fillIdent (L, R, sr, bpm);
        writeWav (dir / "dry-source.wav", L, R, sr);
    }

    // Send-style: MIX=1 wet-only return
    renderVariant (dir / "wet-only-journey.wav", dir / "wet-only-journey-lifecycle.txt",
                   1.00f, 0.50f, 0.65f, sr, bpm, 192.0);
    renderVariant (dir / "send-journey.wav", dir / "send-journey-lifecycle.txt",
                   1.00f, 0.50f, 0.65f, sr, bpm, 192.0);

    renderVariant (dir / "low-hunger.wav", dir / "low-hunger-lifecycle.txt",
                   1.00f, 0.20f, 0.60f, sr, bpm, 128.0);
    renderVariant (dir / "high-hunger.wav", dir / "high-hunger-lifecycle.txt",
                   1.00f, 1.00f, 0.60f, sr, bpm, 128.0);

    renderVariant (dir / "low-memory.wav", dir / "low-memory-lifecycle.txt",
                   1.00f, 0.50f, 0.20f, sr, bpm, 160.0);
    renderVariant (dir / "high-memory.wav", dir / "high-memory-lifecycle.txt",
                   1.00f, 0.50f, 1.00f, sr, bpm, 160.0);

    renderVariant (dir / "reinforced-memory.wav", dir / "reinforced-memory-lifecycle.txt",
                   1.00f, 0.75f, 0.70f, sr, bpm, 192.0);
    renderVariant (dir / "forgetting-memory.wav", dir / "forgetting-memory-lifecycle.txt",
                   1.00f, 0.30f, 0.25f, sr, bpm, 220.0);

    // Deep callback: long run high MEMORY
    renderVariant (dir / "deep-callback.wav", dir / "deep-callback-lifecycle.txt",
                   1.00f, 0.65f, 0.95f, sr, bpm, 256.0);

    return 0;
}

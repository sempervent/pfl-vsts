#include "dsp/MemoryEaterEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
        case K::DescendantCapture: return "DESCENDANT_CAPTURE";
        case K::DescendantPromote: return "DESCENDANT_PROMOTE";
    }
    return "?";
}

static void writeLifecycle (const fs::path& path, const pfl::dsp::MemoryEaterEngine& eng)
{
    std::ofstream tr (path);
    tr << "Memory Eater Stage 3 family / ecology lifecycle\n";
    tr << "promotions=" << eng.ecology().promotions()
       << " descendants=" << eng.ecology().descendants()
       << " storedRecalls=" << eng.ecology().recallsStored()
       << " forgotten=" << eng.ecology().forgotten()
       << " maxLineageOcc=" << eng.ecology().maxLineageOccupancy() << "\n";
    for (int g = 0; g <= pfl::dsp::MemoryEcology::kMaxGeneration; ++g)
        tr << "occupiedGen" << g << "=" << eng.ecology().countByGeneration (g) << "\n";

    for (const auto& e : eng.ecology().traces())
    {
        tr << "beat " << e.beat << " " << kindName (e.kind)
           << " memory=" << e.memoryId
           << " gen=" << e.generation
           << " parent=" << e.parentMemoryId
           << " root=" << e.rootMemoryId
           << " slot=" << e.slot
           << " strength=" << e.strength
           << " fatigue=" << e.fatigue
           << " sourceBeat=" << e.sourceBeat
           << " fragment=" << e.fragmentBeats << "\n";
    }

    // Family tree artifact from currently living slots
    tr << "\n--- living family tree ---\n";
    std::map<int, std::vector<const pfl::dsp::MemorySlot*>> byRoot;
    for (int i = 0; i < eng.ecology().numSlots(); ++i)
    {
        const auto& s = eng.ecology().slot (i);
        if (s.valid)
            byRoot[s.rootMemoryId].push_back (&s);
    }
    for (const auto& [root, members] : byRoot)
    {
        tr << "root " << root << "\n";
        for (const auto* s : members)
        {
            tr << "  memory " << s->memoryId
               << " gen" << s->generation
               << " parent=" << s->parentMemoryId
               << " strength=" << s->strength
               << " recalls=" << s->recallCount
               << " promoteBeat=" << s->promoteBeat << "\n";
        }
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
           << " gen=" << e.generation
           << " sourceBeat=" << e.sourceBeat
           << " lookback=" << e.lookbackBeats
           << " fragment=" << e.fragmentBeats << "\n";
    }
}

static pfl::dsp::MemoryEaterEngine runSession (float mix, float hunger, float memory,
                                               double sr, double bpm, double beats, uint64_t seed = 3003)
{
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (beats / bps);
    std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
    fillIdent (L, R, sr, bpm);

    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (seed);
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
    // stash audio on engine via return + separate write in caller using L,R
    return eng;
}

static void renderVariant (const fs::path& wav, const fs::path& lifecycle,
                           float mix, float hunger, float memory,
                           double sr, double bpm, double beats, uint64_t seed = 3003)
{
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (beats / bps);
    std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
    fillIdent (L, R, sr, bpm);

    pfl::dsp::MemoryEaterEngine eng;
    eng.prepare (sr);
    eng.setSeed (seed);
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
    std::cout << "  recalls=" << eng.events().size()
              << " descendants=" << eng.ecology().descendants()
              << " maxGenOcc=";
    for (int g = 0; g <= pfl::dsp::MemoryEcology::kMaxGeneration; ++g)
        std::cout << eng.ecology().countByGeneration (g) << (g < 3 ? "/" : "");
    std::cout << " ramMiB=" << (eng.totalRamBytes() / (1024.0 * 1024.0)) << "\n";
}

int main()
{
    const double sr = 48000.0;
    const double bpm = 72.0;
    const fs::path dir = "renders/memory-eater/stage3";
    fs::create_directories (dir);

    {
        const int n = static_cast<int> (sr * 30.0);
        std::vector<float> L (n), R (n);
        fillIdent (L, R, sr, bpm);
        writeWav (dir / "source-dry.wav", L, R, sr);
    }

    // Canonical send-first wet return
    renderVariant (dir / "memory-return-wet.wav", dir / "memory-return-wet-lifecycle.txt",
                   1.00f, 0.50f, 0.70f, sr, bpm, 256.0);
    renderVariant (dir / "family-journey-wet.wav", dir / "family-journey-lifecycle.txt",
                   1.00f, 0.50f, 0.70f, sr, bpm, 384.0);
    renderVariant (dir / "deep-lineage.wav", dir / "deep-lineage-lifecycle.txt",
                   1.00f, 0.75f, 0.95f, sr, bpm, 480.0, 9001);
    renderVariant (dir / "lineage-extinction.wav", dir / "lineage-extinction-lifecycle.txt",
                   1.00f, 0.30f, 0.25f, sr, bpm, 280.0);

    renderVariant (dir / "low-hunger.wav", dir / "low-hunger-lifecycle.txt",
                   1.00f, 0.20f, 0.70f, sr, bpm, 192.0);
    renderVariant (dir / "high-hunger.wav", dir / "high-hunger-lifecycle.txt",
                   1.00f, 1.00f, 0.70f, sr, bpm, 192.0);
    renderVariant (dir / "low-memory.wav", dir / "low-memory-lifecycle.txt",
                   1.00f, 0.50f, 0.20f, sr, bpm, 220.0);
    renderVariant (dir / "high-memory.wav", dir / "high-memory-lifecycle.txt",
                   1.00f, 0.50f, 1.00f, sr, bpm, 220.0);

    // Monitoring mix: dry + wet return (simulate send)
    {
        const double beats = 192.0;
        const double bps = (bpm / 60.0) / sr;
        const int n = static_cast<int> (beats / bps);
        std::vector<float> dryL (n), dryR (n), wetL (n), wetR (n);
        fillIdent (dryL, dryR, sr, bpm);
        wetL = dryL;
        wetR = dryR;
        pfl::dsp::MemoryEaterEngine eng;
        eng.prepare (sr);
        eng.setSeed (3003);
        eng.setMix (1.0f);
        eng.setHunger (0.50f);
        eng.setMemory (0.70f);
        eng.setOutput (0.85f);
        eng.snapMacros();
        for (int done = 0; done < n; done += 256)
        {
            const int m = std::min (256, n - done);
            eng.process (wetL.data() + done, wetR.data() + done, m, true, done * bps, bpm);
        }
        std::vector<float> mixL (n), mixR (n);
        for (int i = 0; i < n; ++i)
        {
            mixL[static_cast<size_t> (i)] = std::clamp (dryL[static_cast<size_t> (i)] * 0.85f
                                                        + wetL[static_cast<size_t> (i)] * 0.55f, -0.99f, 0.99f);
            mixR[static_cast<size_t> (i)] = std::clamp (dryR[static_cast<size_t> (i)] * 0.85f
                                                        + wetR[static_cast<size_t> (i)] * 0.55f, -0.99f, 0.99f);
        }
        writeWav (dir / "monitoring-mix.wav", mixL, mixR, sr);
        writeLifecycle (dir / "monitoring-mix-lifecycle.txt", eng);
    }

    return 0;
}

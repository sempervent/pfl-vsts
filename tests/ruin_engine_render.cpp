#include "dsp/RuinEngine.h"
#include "performance/RuinEnginePerformanceController.h"

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

static void writeWearLine (std::ofstream& tr, double beat, const pfl::dsp::RuinEngine& eng)
{
    const auto w = eng.wearState();
    tr << "beat " << beat
       << " state " << pfl::dsp::ruinStateName (eng.processingState())
       << " wear spectral " << w.spectral
       << " nonlinear " << w.nonlinear
       << " temporal " << w.temporal
       << " mean " << w.mean() << "\n";
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
    writeWearLine (tr, 0.0, eng);

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
            writeWearLine (tr, ppq, eng);
            tr << "TRANSITION " << pfl::dsp::ruinStateName (prev)
               << " → " << pfl::dsp::ruinStateName (st) << "\n";
            prev = st;
        }
    }
    writeWearLine (tr, static_cast<double> (n) * beatsPerSample, eng);
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

static void renderStage3 (const fs::path& dir, double sr, double bpm)
{
    fs::create_directories (dir);
    using S = pfl::dsp::RuinProcessingState;

    auto renderPair = [&] (const char* freshName, const char* agedName, S state)
    {
        // Fresh
        {
            const int n = static_cast<int> (sr * 16.0);
            std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
            fillSource (L, R, sr);
            pfl::dsp::RuinEngine eng;
            eng.prepare (sr);
            eng.setSeed (2002);
            eng.setMix (0.70f);
            eng.setAge (0.45f);
            eng.setInstability (0.35f);
            eng.setOutput (0.90f);
            eng.snapMacros();
            eng.forceProcessingState (true, state);
            eng.resetWearFresh();
            const int block = 256;
            const double bps = (bpm / 60.0) / sr;
            for (int done = 0; done < n; done += block)
            {
                const int m = std::min (block, n - done);
                eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
            }
            writeWav (dir / freshName, L, R, sr);
            std::cout << "  " << freshName << " wear=" << eng.wearState().mean() << "\n";
        }
        // Aged: abuse then settle into same state/macros
        {
            const double bps = (bpm / 60.0) / sr;
            const int abuseN = static_cast<int> (192.0 / bps);
            const int listenN = static_cast<int> (sr * 16.0);
            const int n = abuseN + listenN;
            std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
            fillSource (L, R, sr);
            pfl::dsp::RuinEngine eng;
            eng.prepare (sr);
            eng.setSeed (2002);
            eng.setMix (0.70f);
            eng.setAge (0.95f);
            eng.setInstability (0.55f);
            eng.setOutput (0.90f);
            eng.snapMacros();
            eng.forceProcessingState (true, S::Ruined);
            const int block = 256;
            int done = 0;
            for (; done < abuseN; done += block)
            {
                const int m = std::min (block, abuseN - done);
                eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
            }
            eng.setAge (0.45f);
            eng.setInstability (0.35f);
            eng.snapMacros();
            eng.forceProcessingState (true, state);
            for (; done < n; done += block)
            {
                const int m = std::min (block, n - done);
                eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
            }
            // Export only the listen segment (matched length to fresh)
            std::vector<float> oL (L.begin() + abuseN, L.end());
            std::vector<float> oR (R.begin() + abuseN, R.end());
            writeWav (dir / agedName, oL, oR, sr);
            std::cout << "  " << agedName << " wear=" << eng.wearState().mean() << "\n";
        }
    };

    renderPair ("fresh-intact.wav", "scarred-intact.wav", S::Intact);
    renderPair ("fresh-weathered.wav", "aged-weathered.wav", S::Weathered);
    renderPair ("fresh-ruined.wav", "aged-ruined.wav", S::Ruined);

    // Recovery journey + aging journey
    {
        const double bps = (bpm / 60.0) / sr;
        const int n = static_cast<int> (512.0 / bps);
        std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
        fillSource (L, R, sr);
        pfl::dsp::RuinEngine eng;
        eng.prepare (sr);
        eng.setSeed (2002);
        eng.setMix (0.70f);
        eng.setOutput (0.90f);
        std::ofstream tr (dir / "stage3-wear-trace.txt");
        tr << "stage3 wear trace\n";
        writeWearLine (tr, 0.0, eng);

        const int block = 256;
        double lastLogged = -1.0e9;
        for (int done = 0; done < n; done += block)
        {
            const double beat = static_cast<double> (done) * bps;
            float age = 0.20f, inst = 0.35f;
            if (beat < 64.0)
            {
                age = 0.20f;
                inst = 0.35f;
            }
            else if (beat < 256.0)
            {
                age = 0.85f;
                inst = 0.60f;
            }
            else
            {
                age = 0.15f;
                inst = 0.25f;
            }
            eng.setAge (age);
            eng.setInstability (inst);
            const int m = std::min (block, n - done);
            eng.process (L.data() + done, R.data() + done, m, true, beat, bpm);

            if (beat - lastLogged >= 32.0 || done + block >= n)
            {
                writeWearLine (tr, beat, eng);
                lastLogged = beat;
            }
        }
        writeWearLine (tr, 512.0, eng);
        writeWav (dir / "stage3-aging-journey.wav", L, R, sr);
        writeWav (dir / "recovery-journey.wav", L, R, sr);
        std::cout << "wrote aging/recovery journey wear_final=" << eng.wearState().mean() << "\n";
    }

    // Metrics summary
    {
        std::ofstream m (dir / "stage3-metrics.txt");
        m << "Ruin Engine Stage 3 metrics (offline)\n";
        m << "Wear dimensions: spectralWear, nonlinearWear, temporalWear\n";
        m << "See stage3-wear-trace.txt for beat timeline.\n";
        m << "Fresh vs aged WAVs share macros/state after scarring segment.\n";
    }
}

int main (int argc, char** argv)
{
    const std::string arg = argc > 1 ? argv[1] : "";
    const double sr = 48000.0;
    const double bpm = 72.0;

    if (arg == "--stage4")
    {
        const fs::path dir = "renders/ruin-engine/stage4";
        fs::create_directories (dir);
        using Cmd = pfl::ruin_perf::Command;
        auto renderPerf = [&] (const fs::path& wav, const fs::path& trace,
                               auto script)
        {
            const double seconds = 90.0;
            const int n = static_cast<int> (sr * seconds);
            std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
            fillSource (L, R, sr);
            pfl::dsp::RuinEngine eng;
            pfl::ruin_perf::RuinEnginePerformanceController perf;
            eng.prepare (sr);
            eng.setSeed (2002);
            eng.setMix (0.70f);
            eng.setAge (0.55f);
            eng.setInstability (0.50f);
            eng.setOutput (0.90f);
            eng.snapMacros();
            perf.reset (2002);
            perf.setTraceEnabled (true);
            std::ofstream tr (trace);
            const int block = 256;
            const double bps = (bpm / 60.0) / sr;
            for (int done = 0; done < n; done += block)
            {
                const double beat = static_cast<double> (done) * bps;
                script (perf, eng, beat);
                perf.tick (beat, true, eng);
                const int m = std::min (block, n - done);
                eng.process (L.data() + done, R.data() + done, m, true, beat, bpm);
            }
            for (const auto& e : perf.events())
                tr << "beat " << e.ppq << " " << pfl::ruin_perf::commandName (e.command)
                   << " " << e.detail << " mode=" << pfl::ruin_perf::modeName (perf.mode()) << "\n";
            writeWearLine (tr, static_cast<double> (n) * bps, eng);
            writeWav (wav, L, R, sr);
        };

        renderPerf (dir / "performance-journey.wav", dir / "performance-journey-trace.txt",
                    [] (auto& perf, auto& eng, double beat)
                    {
                        using Cmd = pfl::ruin_perf::Command;
                        if (std::abs (beat - 32.0) < 0.02) perf.trigger (Cmd::FreezeOn, beat, eng);
                        if (std::abs (beat - 48.0) < 0.02) perf.trigger (Cmd::Mutate, beat, eng);
                        if (std::abs (beat - 64.0) < 0.02) perf.trigger (Cmd::Mutate, beat, eng);
                        if (std::abs (beat - 80.0) < 0.02) perf.trigger (Cmd::FreezeOff, beat, eng);
                        if (std::abs (beat - 112.0) < 0.02) perf.trigger (Cmd::Collapse, beat, eng);
                        if (std::abs (beat - 136.0) < 0.02) perf.trigger (Cmd::SilenceOn, beat, eng);
                        if (std::abs (beat - 144.0) < 0.02) perf.trigger (Cmd::SilenceOff, beat, eng);
                        if (std::abs (beat - 176.0) < 0.02) perf.trigger (Cmd::Reseed, beat, eng);
                    });

        // freeze-weathered
        {
            const int n = static_cast<int> (sr * 20.0);
            std::vector<float> L (n), R (n); fillSource (L, R, sr);
            pfl::dsp::RuinEngine eng; pfl::ruin_perf::RuinEnginePerformanceController perf;
            eng.prepare (sr); eng.setSeed (2002); eng.setMix (0.7f); eng.setAge (0.55f); eng.setInstability (0.45f); eng.setOutput (0.9f); eng.snapMacros();
            eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Weathered);
            perf.reset (2002);
            const double bps = (bpm / 60.0) / sr;
            for (int done = 0; done < n; done += 256) {
                const double beat = done * bps;
                if (beat >= 8.0 && beat < 8.0 + bps * 256) perf.trigger (Cmd::FreezeOn, beat, eng);
                perf.tick (beat, true, eng);
                eng.process (L.data()+done, R.data()+done, std::min(256, n-done), true, beat, bpm);
            }
            writeWav (dir / "freeze-weathered.wav", L, R, sr);
        }
        {
            const int n = static_cast<int> (sr * 30.0);
            std::vector<float> L (n), R (n); fillSource (L, R, sr);
            pfl::dsp::RuinEngine eng; pfl::ruin_perf::RuinEnginePerformanceController perf;
            eng.prepare (sr); eng.setSeed (2002); eng.setMix (0.7f); eng.setAge (0.45f); eng.setInstability (0.45f); eng.setOutput (0.9f); eng.snapMacros();
            eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Intact);
            perf.reset (2002);
            const double bps = (bpm / 60.0) / sr;
            for (int done = 0; done < n; done += 256) {
                const double beat = done * bps;
                if (beat >= 4.0 && beat < 4.0 + bps * 256) perf.trigger (Cmd::Collapse, beat, eng);
                perf.tick (beat, true, eng);
                eng.process (L.data()+done, R.data()+done, std::min(256, n-done), true, beat, bpm);
            }
            writeWav (dir / "collapse-from-intact.wav", L, R, sr);
        }
        {
            // reseed-scarred
            const double bps = (bpm / 60.0) / sr;
            const int abuse = (int)(128.0 / bps), listen = (int)(sr * 12.0), n = abuse + listen;
            std::vector<float> L (n), R (n); fillSource (L, R, sr);
            pfl::dsp::RuinEngine eng; pfl::ruin_perf::RuinEnginePerformanceController perf;
            eng.prepare (sr); eng.setSeed (2002); eng.setMix (0.7f); eng.setAge (0.95f); eng.setInstability (0.55f); eng.setOutput (0.9f); eng.snapMacros();
            eng.forceProcessingState (true, pfl::dsp::RuinProcessingState::Ruined);
            perf.reset (2002);
            int done = 0;
            for (; done < abuse; done += 256) {
                const double beat = done * bps;
                perf.tick (beat, true, eng);
                eng.process (L.data()+done, R.data()+done, std::min(256, abuse-done), true, beat, bpm);
            }
            perf.trigger (Cmd::Reseed, done * bps, eng);
            for (; done < n; done += 256) {
                const double beat = done * bps;
                perf.tick (beat, true, eng);
                eng.process (L.data()+done, R.data()+done, std::min(256, n-done), true, beat, bpm);
            }
            writeWav (dir / "reseed-scarred.wav", L, R, sr);
            std::cout << "reseed-scarred wear=" << eng.wearState().mean() << " seed=" << eng.seed() << "\n";
        }
        std::cout << "stage4 renders done\n";
        return 0;
    }

    if (arg == "--stage3")
    {
        renderStage3 ("renders/ruin-engine/stage3", sr, bpm);
        return 0;
    }

    if (arg == "--stage2")
    {
        const fs::path dir = "renders/ruin-engine/stage2";
        fs::create_directories (dir);
        using S = pfl::dsp::RuinProcessingState;
        renderForced (dir / "state-intact.wav", S::Intact, sr, 12.0, bpm);
        renderForced (dir / "state-weathered.wav", S::Weathered, sr, 12.0, bpm);
        renderForced (dir / "state-fractured.wav", S::Fractured, sr, 12.0, bpm);
        renderForced (dir / "state-ruined.wav", S::Ruined, sr, 12.0, bpm);
        renderForced (dir / "state-recovering.wav", S::Recovering, sr, 12.0, bpm);
        renderJourney (dir / "journey-seed-2002.wav", dir / "journey-seed-2002-trace.txt",
                       2002, 0.70f, 0.50f, 0.50f, 0.9f, sr, 180.0, bpm);
        return 0;
    }

    const fs::path dir = "renders/ruin-engine/stage1";
    fs::create_directories (dir);
    renderJourney (dir / "stage1-evolution.wav", dir / "stage1-trace.txt",
                   2002, 0.65f, 0.45f, 0.45f, 0.9f, sr, 60.0, bpm);
    return 0;
}

#include "dsp/PulseColonyEngine.h"
#include "dsp/ParamSmoother.h"
#include "performance/PulseColonyPerformanceController.h"

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

struct RenderOpts
{
    float mix = 1.0f;
    float dens = 0.5f;
    float mut = 0.35f;
    float motion = 0.0f;
    bool interaction = true;
    int solo = -1;
};

static void renderVariant (const fs::path& wav, const fs::path& trace,
                           const RenderOpts& opt, double sr, double bpm, double beats)
{
    const double bps = (bpm / 60.0) / sr;
    const int n = static_cast<int> (beats / bps);
    std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
    fillTone (L, R, sr);

    pfl::dsp::PulseColonyEngine eng;
    eng.prepare (sr);
    eng.setSeed (2002);
    eng.setMix (opt.mix);
    eng.setDensity (opt.dens);
    eng.setMutation (opt.mut);
    eng.setMotion (opt.motion);
    eng.setOutput (0.85f);
    eng.setInteractionEnabled (opt.interaction);
    eng.setSoloRole (opt.solo);
    eng.snapMacros();
    eng.forceRebuild (0);
    eng.setTraceEnabled (true);

    for (int done = 0; done < n; done += 256)
    {
        const int m = std::min (256, n - done);
        eng.process (L.data() + done, R.data() + done, m, true, done * bps, bpm);
    }

    std::ofstream tr (trace);
    tr << "Pulse Colony Stage 2\n";
    tr << "dens=" << opt.dens << " mut=" << opt.mut << " motion=" << opt.motion
       << " interaction=" << (opt.interaction ? 1 : 0)
       << " solo=" << opt.solo
       << " gen=" << eng.generation()
       << " colonyOcc=" << eng.colonyOpenOccupancy()
       << " acceptA=" << eng.roleAcceptCount (0)
       << " acceptS=" << eng.roleAcceptCount (1)
       << " acceptG=" << eng.roleAcceptCount (2)
       << " opens=" << eng.opens().size() << "\n";
    for (const auto& e : eng.traces())
        tr << "beat " << e.beat << " " << e.detail << "\n";
    for (const auto& e : eng.opens())
        tr << "open beat=" << e.beat << " dur=" << e.durationBeats
           << " role=" << static_cast<int> (e.role)
           << " pan=" << e.pan << "\n";

    writeWav (wav, L, R, sr);
}

int main()
{
    const double sr = 48000.0, bpm = 120.0;
    const fs::path dir = "renders/pulse-colony/stage2";
    fs::create_directories (dir);

    {
        const int n = static_cast<int> (sr * 20.0);
        std::vector<float> L (n), R (n);
        fillTone (L, R, sr);
        writeWav (dir / "dry-source.wav", L, R, sr);
    }

    auto R = [] (float dens, float mut, float motion) {
        RenderOpts o;
        o.dens = dens; o.mut = mut; o.motion = motion;
        return o;
    };

    renderVariant (dir / "density020.wav", dir / "density020-trace.txt", R (0.20f, 0.35f, 0.0f), sr, bpm, 64.0);
    renderVariant (dir / "density050.wav", dir / "density050-trace.txt", R (0.50f, 0.35f, 0.0f), sr, bpm, 64.0);
    renderVariant (dir / "density100.wav", dir / "density100-trace.txt", R (1.00f, 0.35f, 0.0f), sr, bpm, 64.0);

    renderVariant (dir / "mutation000.wav", dir / "mutation000-trace.txt", R (0.50f, 0.00f, 0.0f), sr, bpm, 96.0);
    renderVariant (dir / "mutation050.wav", dir / "mutation050-trace.txt", R (0.50f, 0.50f, 0.0f), sr, bpm, 96.0);
    renderVariant (dir / "mutation100.wav", dir / "mutation100-trace.txt", R (0.50f, 1.00f, 0.0f), sr, bpm, 96.0);

    renderVariant (dir / "motion000.wav", dir / "motion000-trace.txt", R (0.50f, 0.0f, 0.00f), sr, bpm, 48.0);
    renderVariant (dir / "motion050.wav", dir / "motion050-trace.txt", R (0.50f, 0.0f, 0.50f), sr, bpm, 48.0);
    renderVariant (dir / "motion100.wav", dir / "motion100-trace.txt", R (0.50f, 0.0f, 1.00f), sr, bpm, 48.0);

    {
        RenderOpts o = R (0.50f, 0.35f, 0.35f);
        renderVariant (dir / "colony-default.wav", dir / "colony-default-trace.txt", o, sr, bpm, 128.0);
        renderVariant (dir / "colony.wav", dir / "colony-trace.txt", o, sr, bpm, 128.0); // alias
    }

    {
        RenderOpts o = R (0.50f, 0.35f, 0.35f);
        o.solo = 0;
        renderVariant (dir / "anchor-only.wav", dir / "anchor-only-trace.txt", o, sr, bpm, 96.0);
        o.solo = 1;
        renderVariant (dir / "skitter-only.wav", dir / "skitter-only-trace.txt", o, sr, bpm, 96.0);
        o.solo = 2;
        renderVariant (dir / "ghost-only.wav", dir / "ghost-only-trace.txt", o, sr, bpm, 96.0);
    }

    {
        RenderOpts o = R (0.85f, 0.35f, 0.35f);
        o.interaction = false;
        renderVariant (dir / "interaction-disabled.wav", dir / "interaction-disabled-trace.txt",
                       o, sr, bpm, 96.0);
        o.interaction = true;
        renderVariant (dir / "interaction-enabled.wav", dir / "interaction-enabled-trace.txt",
                       o, sr, bpm, 96.0);
    }

    renderVariant (dir / "stage2-journey-wet.wav", dir / "stage2-journey-trace.txt",
                   R (0.50f, 0.35f, 0.35f), sr, bpm, 128.0);

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
        writeWav (dir / "stage2-journey-mix.wav", wetL, wetR, sr);
    }

    // ---- Stage 3 performance renders ----
    {
        const fs::path d3 = "renders/pulse-colony/stage3";
        fs::create_directories (d3);
        auto writePerfTrace = [&] (const fs::path& path,
                                   const pfl::pulse_perf::PulseColonyPerformanceController& perf,
                                   const pfl::dsp::PulseColonyEngine& eng)
        {
            std::ofstream tr (path);
            tr << "Pulse Colony Stage 3 performance trace\n";
            tr << "algorithm=" << pfl::dsp::PulseColonyEngine::kAlgorithmVersion
               << " perfEngine=" << pfl::pulse_perf::kPerformanceEngineVersion << "\n";
            tr << "seed=" << eng.seed()
               << " gen=" << eng.generation()
               << " mode=" << pfl::pulse_perf::modeName (perf.mode())
               << " residue=" << perf.state().residueRole << "\n";
            for (const auto& e : perf.events())
                tr << "ppq " << e.ppq << " " << e.detail << "\n";
            for (const auto& e : eng.traces())
                tr << "eng beat " << e.beat << " " << e.detail << "\n";
        };

        auto processSeg = [&] (pfl::dsp::PulseColonyEngine& eng,
                               pfl::pulse_perf::PulseColonyPerformanceController& perf,
                               std::vector<float>& L, std::vector<float>& R,
                               double startBeat, double numBeats,
                               pfl::dsp::ParamSmoother* silenceSm = nullptr)
        {
            const double bps = (bpm / 60.0) / sr;
            const int n = static_cast<int> (numBeats / bps);
            const int offset = static_cast<int> (startBeat / bps);
            for (int done = 0; done < n; done += 256)
            {
                const int m = std::min (256, n - done);
                const int idx = offset + done;
                if (idx + m > static_cast<int> (L.size())) break;
                const double ppq = startBeat + done * bps;
                perf.tick (ppq, true, eng);
                eng.process (L.data() + idx, R.data() + idx, m, true, ppq, bpm);
                if (silenceSm != nullptr)
                {
                    silenceSm->setTarget (perf.mode() == pfl::pulse_perf::Mode::Silenced ? 0.0f : 1.0f);
                    for (int i = 0; i < m; ++i)
                    {
                        const float g = silenceSm->getNext();
                        L[static_cast<size_t> (idx + i)] *= g;
                        R[static_cast<size_t> (idx + i)] *= g;
                    }
                }
            }
        };

        auto makeBuf = [&] (double beats)
        {
            const double bps = (bpm / 60.0) / sr;
            const int n = static_cast<int> (beats / bps);
            std::vector<float> L (static_cast<size_t> (n)), R (static_cast<size_t> (n));
            fillTone (L, R, sr);
            return std::make_pair (L, R);
        };

        auto setup = [&] (pfl::dsp::PulseColonyEngine& eng,
                          pfl::pulse_perf::PulseColonyPerformanceController& perf)
        {
            eng.prepare (sr);
            eng.setSeed (2002);
            eng.setMix (1.0f);
            eng.setDensity (0.55f);
            eng.setMutation (0.40f);
            eng.setMotion (0.35f);
            eng.setOutput (0.85f);
            eng.snapMacros();
            eng.forceRebuild (0);
            eng.setTraceEnabled (true);
            perf.reset (2002);
            perf.setTraceEnabled (true);
        };

        // freeze-density-sweep.wav
        {
            auto [L, R] = makeBuf (192.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            setup (eng, perf);
            processSeg (eng, perf, L, R, 0.0, 64.0);
            perf.trigger (pfl::pulse_perf::Command::FreezeOn, 64.0, eng);
            processSeg (eng, perf, L, R, 64.0, 32.0);
            eng.setDensity (0.20f);
            processSeg (eng, perf, L, R, 96.0, 32.0);
            eng.setDensity (0.80f);
            processSeg (eng, perf, L, R, 128.0, 32.0);
            eng.setDensity (0.40f);
            processSeg (eng, perf, L, R, 160.0, 32.0);
            writeWav (d3 / "freeze-density-sweep.wav", L, R, sr);
            writePerfTrace (d3 / "freeze-density-sweep-trace.txt", perf, eng);
        }
        // mutate forced roles (diagnostic solos)
        for (int role = 0; role < 3; ++role)
        {
            const char* names[] = { "mutate-anchor.wav", "mutate-skitter.wav", "mutate-ghost.wav" };
            const char* traces[] = { "mutate-anchor-trace.txt", "mutate-skitter-trace.txt", "mutate-ghost-trace.txt" };
            auto [L, R] = makeBuf (128.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            setup (eng, perf);
            processSeg (eng, perf, L, R, 0.0, 64.0);
            perf.trigger (pfl::pulse_perf::Command::FreezeOn, 64.0, eng);
            eng.manualMutate (role);
            processSeg (eng, perf, L, R, 64.0, 64.0);
            writeWav (d3 / names[role], L, R, sr);
            writePerfTrace (d3 / traces[role], perf, eng);
        }
        // collapse-vs-density-ramp.wav (B path: dens automation only)
        {
            auto [L, R] = makeBuf (220.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            setup (eng, perf);
            processSeg (eng, perf, L, R, 0.0, 180.0);
            for (int i = 0; i < 40; ++i)
            {
                const float t = static_cast<float> (i) / 39.0f;
                eng.setDensity (0.50f * (1.0f - t) + 0.08f * t);
                processSeg (eng, perf, L, R, 180.0 + i, 1.0);
            }
            writeWav (d3 / "collapse-vs-density-ramp.wav", L, R, sr);
            writePerfTrace (d3 / "collapse-vs-density-ramp-trace.txt", perf, eng);
        }
        // collapsed-residue.wav + residue-mutate.wav
        {
            auto [L, R] = makeBuf (256.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            setup (eng, perf);
            processSeg (eng, perf, L, R, 0.0, 64.0);
            perf.trigger (pfl::pulse_perf::Command::Collapse, 64.0, eng);
            processSeg (eng, perf, L, R, 64.0, 96.0); // through residue
            writeWav (d3 / "collapsed-residue.wav", L, R, sr);
            writePerfTrace (d3 / "collapsed-residue-trace.txt", perf, eng);
            perf.trigger (pfl::pulse_perf::Command::Mutate, 160.0, eng);
            processSeg (eng, perf, L, R, 160.0, 96.0);
            writeWav (d3 / "residue-mutate.wav", L, R, sr);
            writePerfTrace (d3 / "residue-mutate-trace.txt", perf, eng);
        }
        // silence.wav alias of silence-recover core mute window
        {
            auto [L, R] = makeBuf (128.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            pfl::dsp::ParamSmoother silenceSm;
            setup (eng, perf);
            silenceSm.prepare (sr, 0.004f);
            silenceSm.setCurrentAndTarget (1.0f);
            processSeg (eng, perf, L, R, 0.0, 32.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::SilenceOn, 32.0, eng);
            processSeg (eng, perf, L, R, 32.0, 64.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::SilenceOff, 96.0, eng);
            processSeg (eng, perf, L, R, 96.0, 32.0, &silenceSm);
            writeWav (d3 / "silence.wav", L, R, sr);
            writePerfTrace (d3 / "silence-trace.txt", perf, eng);
        }
        // freeze.wav
        {
            auto [L, R] = makeBuf (160.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            setup (eng, perf);
            processSeg (eng, perf, L, R, 0.0, 64.0);
            perf.trigger (pfl::pulse_perf::Command::FreezeOn, 64.0, eng);
            processSeg (eng, perf, L, R, 64.0, 96.0);
            writeWav (d3 / "freeze.wav", L, R, sr);
            writePerfTrace (d3 / "freeze-trace.txt", perf, eng);
        }
        // freeze-mutate.wav
        {
            auto [L, R] = makeBuf (192.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            setup (eng, perf);
            processSeg (eng, perf, L, R, 0.0, 64.0);
            perf.trigger (pfl::pulse_perf::Command::FreezeOn, 64.0, eng);
            processSeg (eng, perf, L, R, 64.0, 16.0);
            perf.trigger (pfl::pulse_perf::Command::Mutate, 80.0, eng);
            processSeg (eng, perf, L, R, 80.0, 16.0);
            perf.trigger (pfl::pulse_perf::Command::Mutate, 96.0, eng);
            processSeg (eng, perf, L, R, 96.0, 16.0);
            perf.trigger (pfl::pulse_perf::Command::FreezeOff, 112.0, eng);
            processSeg (eng, perf, L, R, 112.0, 80.0);
            writeWav (d3 / "freeze-mutate.wav", L, R, sr);
            writePerfTrace (d3 / "freeze-mutate-trace.txt", perf, eng);
        }
        // collapse.wav
        {
            auto [L, R] = makeBuf (220.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            setup (eng, perf);
            processSeg (eng, perf, L, R, 0.0, 180.0);
            perf.trigger (pfl::pulse_perf::Command::Collapse, 180.0, eng);
            processSeg (eng, perf, L, R, 180.0, 40.0);
            writeWav (d3 / "collapse.wav", L, R, sr);
            writePerfTrace (d3 / "collapse-trace.txt", perf, eng);
        }
        // reseed.wav
        {
            auto [L, R] = makeBuf (256.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            setup (eng, perf);
            processSeg (eng, perf, L, R, 0.0, 128.0);
            perf.trigger (pfl::pulse_perf::Command::Reseed, 128.0, eng);
            processSeg (eng, perf, L, R, 128.0, 128.0);
            writeWav (d3 / "reseed.wav", L, R, sr);
            writePerfTrace (d3 / "reseed-trace.txt", perf, eng);
        }
        // silence-recover.wav
        {
            auto [L, R] = makeBuf (192.0);
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            pfl::dsp::ParamSmoother silenceSm;
            setup (eng, perf);
            silenceSm.prepare (sr, 0.004f);
            silenceSm.setCurrentAndTarget (1.0f);
            processSeg (eng, perf, L, R, 0.0, 64.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::SilenceOn, 64.0, eng);
            processSeg (eng, perf, L, R, 64.0, 32.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::SilenceOff, 96.0, eng);
            processSeg (eng, perf, L, R, 96.0, 96.0, &silenceSm);
            writeWav (d3 / "silence-recover.wav", L, R, sr);
            writePerfTrace (d3 / "silence-recover-trace.txt", perf, eng);
        }
        // performance-journey-wet.wav + mix
        {
            auto [L, R] = makeBuf (256.0);
            auto dryL = L, dryR = R;
            pfl::dsp::PulseColonyEngine eng;
            pfl::pulse_perf::PulseColonyPerformanceController perf;
            pfl::dsp::ParamSmoother silenceSm;
            setup (eng, perf);
            silenceSm.prepare (sr, 0.004f);
            silenceSm.setCurrentAndTarget (1.0f);
            processSeg (eng, perf, L, R, 0.0, 64.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::FreezeOn, 64.0, eng);
            processSeg (eng, perf, L, R, 64.0, 16.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::Mutate, 80.0, eng);
            processSeg (eng, perf, L, R, 80.0, 16.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::Mutate, 96.0, eng);
            processSeg (eng, perf, L, R, 96.0, 16.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::FreezeOff, 112.0, eng);
            processSeg (eng, perf, L, R, 112.0, 32.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::Collapse, 144.0, eng);
            processSeg (eng, perf, L, R, 144.0, 12.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::SilenceOn, 156.0, eng);
            processSeg (eng, perf, L, R, 156.0, 8.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::SilenceOff, 164.0, eng);
            processSeg (eng, perf, L, R, 164.0, 28.0, &silenceSm);
            perf.trigger (pfl::pulse_perf::Command::Reseed, 192.0, eng);
            processSeg (eng, perf, L, R, 192.0, 64.0, &silenceSm);
            writeWav (d3 / "performance-journey-wet.wav", L, R, sr);
            writePerfTrace (d3 / "performance-journey-trace.txt", perf, eng);
            std::vector<float> mixL (L.size()), mixR (R.size());
            for (size_t i = 0; i < L.size(); ++i)
            {
                mixL[i] = std::clamp (dryL[i] * 0.85f + L[i] * 0.55f, -0.99f, 0.99f);
                mixR[i] = std::clamp (dryR[i] * 0.85f + R[i] * 0.55f, -0.99f, 0.99f);
            }
            writeWav (d3 / "performance-journey-mix.wav", mixL, mixR, sr);
        }
        std::cout << "stage3 renders written under " << d3 << "\n";
    }

    return 0;
}

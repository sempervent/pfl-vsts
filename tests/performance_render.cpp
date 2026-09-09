#include "dsp/DCBlocker.h"
#include "dsp/DirtBus.h"
#include "dsp/FeedbackDelay.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/Voice.h"
#include "generative/Composer.h"
#include "generative/DeterministicRNG.h"
#include "performance/PerformanceController.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct ScriptEvent
{
    double bar = 0.0;
    pfl::performance::Command cmd = pfl::performance::Command::FreezeOn;
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

static int renderPerformance (const juce::File& outDir)
{
    constexpr double sampleRate = 48000.0;
    constexpr int kBlock = 256;
    constexpr int kNumVoices = 3;
    constexpr double bpm = 72.0;
    constexpr uint64_t seed = 2002;
    constexpr double durationSec = 7.0 * 60.0; // ~7 minutes demonstration

    const std::array<float, kNumVoices> pans { -0.28f, 0.05f, 0.32f };
    const std::array<float, kNumVoices> driftOff { -3.5f, 2.0f, 1.5f };

    // Charter example adapted to implemented semantics (bars are 0-indexed downbeat indices)
    const std::vector<ScriptEvent> script {
        { 16.0, pfl::performance::Command::FreezeOn },
        { 20.0, pfl::performance::Command::Mutate },
        { 22.0, pfl::performance::Command::Mutate },
        { 24.0, pfl::performance::Command::FreezeOff },
        { 40.0, pfl::performance::Command::Collapse },
        // collapse 8 bars → residue from ~48
        { 56.0, pfl::performance::Command::Reseed },
        { 72.0, pfl::performance::Command::SilenceOn },
        { 73.0, pfl::performance::Command::SilenceOff },
        { 88.0, pfl::performance::Command::Mutate },
        { 104.0, pfl::performance::Command::FreezeOn },
        { 108.0, pfl::performance::Command::Mutate },
        { 112.0, pfl::performance::Command::FreezeOff },
    };

    pfl::generative::Composer composer;
    composer.setParams ({ 0.45f, 0.35f });
    composer.reseed (seed);

    pfl::performance::PerformanceController perf;
    perf.reset (seed);
    perf.setTraceEnabled (true);

    std::array<pfl::dsp::Voice, kNumVoices> voices;
    for (int i = 0; i < kNumVoices; ++i)
    {
        voices[static_cast<size_t> (i)].prepare (sampleRate);
        voices[static_cast<size_t> (i)].setDriftRng (
            pfl::generative::DeterministicRNG::derived (seed, 0x44524654ull + static_cast<uint64_t> (i)));
    }

    pfl::dsp::DirtBus dirtBus;
    pfl::dsp::FeedbackDelay space;
    pfl::dsp::DCBlocker dcL, dcR;
    pfl::dsp::SafetyLimiter limL, limR;
    dirtBus.prepare (sampleRate);
    dirtBus.setNoiseSeed (seed);
    dirtBus.setDirt (0.45f);
    space.prepare (sampleRate);
    space.setSpace (0.55f);
    dcL.prepare (sampleRate);
    dcR.prepare (sampleRate);
    limL.prepare (sampleRate);
    limR.prepare (sampleRate);

    const int totalSamples = static_cast<int> (durationSec * sampleRate);
    juce::AudioBuffer<float> master (2, totalSamples);
    master.clear();

    double ppq = 0.0;
    const double beatsPerSec = bpm / 60.0;
    const double bpb = 4.0;
    float peak = 0.0f;
    int written = 0;
    size_t scriptIdx = 0;
    pfl::performance::PerformanceOutputs lastPerf {};

    auto fireDue = [&] (double atPpq)
    {
        while (scriptIdx < script.size())
        {
            const double target = script[scriptIdx].bar * bpb;
            if (atPpq + 1.0e-9 < target)
                break;
            if (script[scriptIdx].cmd == pfl::performance::Command::Reseed)
            {
                const int fade = static_cast<int> (sampleRate * (1.0 / beatsPerSec)); // ~1 beat
                perf.armReseedFade (std::max (1, fade));
            }
            perf.trigger (script[scriptIdx].cmd, target, composer);
            uint64_t newSeed = 0;
            if (perf.takeSeedDirty (newSeed))
            {
                dirtBus.setNoiseSeed (newSeed);
                for (int i = 0; i < kNumVoices; ++i)
                    voices[static_cast<size_t> (i)].setDriftRng (
                        pfl::generative::DeterministicRNG::derived (
                            newSeed, 0x44524654ull + static_cast<uint64_t> (i)));
            }
            ++scriptIdx;
        }
    };

    while (written < totalSamples)
    {
        const int n = std::min (kBlock, totalSamples - written);
        const double blockBeats = (static_cast<double> (n) / sampleRate) * beatsPerSec;
        const double ppqEnd = ppq + blockBeats;

        fireDue (ppq);

        pfl::generative::ClockSnapshot snap;
        snap.playing = true;
        snap.ppq = ppq;
        snap.tempoBpm = bpm;
        composer.clock().advance (snap);

        const int first = static_cast<int> (std::floor (ppq / bpb + 1.0e-9)) + 1;
        const int last = static_cast<int> (std::floor (ppqEnd / bpb + 1.0e-9));
        for (int b = first; b <= last; ++b)
            perf.onBar (b, static_cast<double> (b) * bpb, composer);

        perf.syncComposerLock (composer);
        composer.processTimeRange (ppq, ppqEnd, true);

        float drift = std::clamp (0.35f + lastPerf.driftBoost, 0.0f, 1.0f);
        float dirt = std::clamp (0.45f + lastPerf.dirtBoost, 0.0f, 1.0f);
        float spaceAmt = std::clamp (0.55f + lastPerf.spaceBoost, 0.0f, 1.0f);
        dirtBus.setDirt (dirt);
        space.setSpace (spaceAmt);

        for (int i = 0; i < kNumVoices; ++i)
        {
            const auto& cv = composer.voice (i);
            pfl::dsp::VoiceParams p;
            p.baseMidiNote = static_cast<float> (cv.pitch.midiNote);
            p.level = 0.28f;
            p.attackSec = 1.2f + 0.5f * static_cast<float> (i);
            p.releaseSec = 2.5f + 0.8f * static_cast<float> (i);
            p.osc2DetuneCents = 6.0f + 2.5f * static_cast<float> (i);
            p.oscBlend = 0.28f + 0.1f * static_cast<float> (i);
            p.pan = pans[static_cast<size_t> (i)];
            p.driftAmount = drift;
            p.driftVoiceOffsetCents = driftOff[static_cast<size_t> (i)] * (0.35f + drift);
            bool active = cv.active;
            if (lastPerf.maxActiveVoices >= 0 && i >= lastPerf.maxActiveVoices)
                active = false;
            p.gate = active;
            voices[static_cast<size_t> (i)].setParams (p);
        }

        auto* L = master.getWritePointer (0, written);
        auto* R = master.getWritePointer (1, written);

        for (int s = 0; s < n; ++s)
        {
            pfl::performance::PerformanceOutputs pout;
            perf.processSample (sampleRate, pout);
            lastPerf = pout;

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
            sL = dcL.processSample (sL);
            sR = dcR.processSample (sR);
            sL = limL.processSample (sL);
            sR = limR.processSample (sR);
            const float fade = pout.silenceGain * pout.reseedFade * 0.65f;
            sL = std::clamp (sL * fade, -0.99f, 0.99f);
            sR = std::clamp (sR * fade, -0.99f, 0.99f);
            L[s] = sL;
            R[s] = sR;
            peak = std::max (peak, std::max (std::abs (sL), std::abs (sR)));
            if (! std::isfinite (sL) || ! std::isfinite (sR))
            {
                std::cerr << "NaN/Inf in performance render\n";
                return 1;
            }
        }

        written += n;
        ppq = ppqEnd;
    }

    const auto wavPath = outDir.getChildFile ("performance-demo-seed-2002.wav");
    if (! writeWav (wavPath, master, sampleRate))
    {
        std::cerr << "Failed to write " << wavPath.getFullPathName() << "\n";
        return 1;
    }

    const auto logPath = outDir.getChildFile ("performance-demo-events.txt");
    {
        std::ofstream log (logPath.getFullPathName().toStdString());
        log << "# Performance demo script + event trace\n";
        log << "# tempo=72 BPM seed=2002 duration=" << durationSec << "s\n";
        log << "# SCRIPT:\n";
        for (const auto& e : script)
        {
            log << "bar " << e.bar << " cmd " << static_cast<int> (e.cmd) << "\n";
        }
        log << "# TRACE:\n";
        for (const auto& e : perf.events())
        {
            log << "ppq=" << e.ppq << " cmd=" << static_cast<int> (e.command)
                << " seed=" << e.seedAfter << "\n";
        }
    }

    std::cout << "Wrote " << wavPath.getFullPathName() << " peak=" << peak << "\n";
    std::cout << "Wrote " << logPath.getFullPathName() << "\n";
    return 0;
}

/** Short A/B comparison renders for freeze / mutate / collapse / reseed. */
static int renderComparisons (const juce::File& outDir)
{
    // Lightweight: 45s freeze demo with mutate mid-freeze
    constexpr double sampleRate = 48000.0;
    constexpr int kBlock = 256;
    constexpr double bpm = 72.0;
    constexpr uint64_t seed = 2002;
    constexpr double durationSec = 45.0;
    constexpr int kNumVoices = 3;
    const std::array<float, kNumVoices> pans { -0.28f, 0.05f, 0.32f };
    const std::array<float, kNumVoices> driftOff { -3.5f, 2.0f, 1.5f };

    const std::vector<ScriptEvent> script {
        { 8.0, pfl::performance::Command::FreezeOn },
        { 12.0, pfl::performance::Command::Mutate },
        { 14.0, pfl::performance::Command::Mutate },
        { 16.0, pfl::performance::Command::FreezeOff },
        { 20.0, pfl::performance::Command::Collapse },
    };

    pfl::generative::Composer composer;
    composer.setParams ({ 0.45f, 0.35f });
    composer.reseed (seed);
    pfl::performance::PerformanceController perf;
    perf.reset (seed);

    std::array<pfl::dsp::Voice, kNumVoices> voices;
    for (int i = 0; i < kNumVoices; ++i)
    {
        voices[static_cast<size_t> (i)].prepare (sampleRate);
        voices[static_cast<size_t> (i)].setDriftRng (
            pfl::generative::DeterministicRNG::derived (seed, 0x44524654ull + static_cast<uint64_t> (i)));
    }
    pfl::dsp::DirtBus dirtBus;
    pfl::dsp::FeedbackDelay space;
    pfl::dsp::DCBlocker dcL, dcR;
    pfl::dsp::SafetyLimiter limL, limR;
    dirtBus.prepare (sampleRate);
    dirtBus.setNoiseSeed (seed);
    dirtBus.setDirt (0.45f);
    space.prepare (sampleRate);
    space.setSpace (0.55f);
    dcL.prepare (sampleRate);
    dcR.prepare (sampleRate);
    limL.prepare (sampleRate);
    limR.prepare (sampleRate);

    const int totalSamples = static_cast<int> (durationSec * sampleRate);
    juce::AudioBuffer<float> master (2, totalSamples);
    master.clear();

    double ppq = 0.0;
    const double beatsPerSec = bpm / 60.0;
    const double bpb = 4.0;
    int written = 0;
    size_t scriptIdx = 0;
    pfl::performance::PerformanceOutputs lastPerf {};

    while (written < totalSamples)
    {
        const int n = std::min (kBlock, totalSamples - written);
        const double blockBeats = (static_cast<double> (n) / sampleRate) * beatsPerSec;
        const double ppqEnd = ppq + blockBeats;

        while (scriptIdx < script.size() && script[scriptIdx].bar * bpb <= ppq + 1.0e-9)
        {
            perf.trigger (script[scriptIdx].cmd, script[scriptIdx].bar * bpb, composer);
            ++scriptIdx;
        }

        pfl::generative::ClockSnapshot snap;
        snap.playing = true;
        snap.ppq = ppq;
        snap.tempoBpm = bpm;
        composer.clock().advance (snap);
        const int first = static_cast<int> (std::floor (ppq / bpb + 1.0e-9)) + 1;
        const int last = static_cast<int> (std::floor (ppqEnd / bpb + 1.0e-9));
        for (int b = first; b <= last; ++b)
            perf.onBar (b, static_cast<double> (b) * bpb, composer);
        perf.syncComposerLock (composer);
        composer.processTimeRange (ppq, ppqEnd, true);

        float drift = std::clamp (0.35f + lastPerf.driftBoost, 0.0f, 1.0f);
        dirtBus.setDirt (std::clamp (0.45f + lastPerf.dirtBoost, 0.0f, 1.0f));
        space.setSpace (std::clamp (0.55f + lastPerf.spaceBoost, 0.0f, 1.0f));

        for (int i = 0; i < kNumVoices; ++i)
        {
            const auto& cv = composer.voice (i);
            pfl::dsp::VoiceParams p;
            p.baseMidiNote = static_cast<float> (cv.pitch.midiNote);
            p.level = 0.28f;
            p.attackSec = 1.2f;
            p.releaseSec = 2.5f;
            p.osc2DetuneCents = 6.0f;
            p.oscBlend = 0.3f;
            p.pan = pans[static_cast<size_t> (i)];
            p.driftAmount = drift;
            p.driftVoiceOffsetCents = driftOff[static_cast<size_t> (i)] * (0.35f + drift);
            bool active = cv.active;
            if (lastPerf.maxActiveVoices >= 0 && i >= lastPerf.maxActiveVoices)
                active = false;
            p.gate = active;
            voices[static_cast<size_t> (i)].setParams (p);
        }

        auto* L = master.getWritePointer (0, written);
        auto* R = master.getWritePointer (1, written);
        for (int s = 0; s < n; ++s)
        {
            pfl::performance::PerformanceOutputs pout;
            perf.processSample (sampleRate, pout);
            lastPerf = pout;
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
            sL = limL.processSample (dcL.processSample (sL));
            sR = limR.processSample (dcR.processSample (sR));
            const float fade = pout.silenceGain * pout.reseedFade * 0.65f;
            L[s] = std::clamp (sL * fade, -0.99f, 0.99f);
            R[s] = std::clamp (sR * fade, -0.99f, 0.99f);
        }
        written += n;
        ppq = ppqEnd;
    }

    const auto path = outDir.getChildFile ("performance-gestures-45s.wav");
    if (! writeWav (path, master, sampleRate))
        return 1;
    std::cout << "Wrote " << path.getFullPathName() << "\n";
    return 0;
}

int main (int argc, char** argv)
{
    const std::string mode = (argc > 1) ? argv[1] : "demo";
    const juce::File outDir = (argc > 2) ? juce::File (argv[2])
                                         : juce::File::getCurrentWorkingDirectory().getChildFile ("renders/phase4");

    outDir.createDirectory();
    if (mode == "gestures")
        return renderComparisons (outDir);
    return renderPerformance (outDir);
}

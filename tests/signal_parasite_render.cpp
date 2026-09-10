#include "dsp/ParamSmoother.h"
#include "dsp/SignalParasiteEngine.h"
#include "performance/SignalParasitePerformanceController.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kSr = 48000.0;
constexpr double kBpm = 120.0;

struct FixtureLcg
{
    uint32_t s = 12345u;
    float next() noexcept
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float> ((s >> 9) & 0x7FFFFF) / static_cast<float> (0x7FFFFF) * 2.0f
               - 1.0f;
    }
};

int beatsToSamples (double beats)
{
    return static_cast<int> (beats * kSr * 60.0 / kBpm);
}

/** Drop one synthetic percussive hit into `x`. Matches the test fixtures. */
void addHit (std::vector<float>& x, double beat, float amp, double decayMs, double toneHz,
             float noiseMix, FixtureLcg& lcg)
{
    const double spb = kSr * 60.0 / kBpm;
    const auto n = static_cast<int64_t> (x.size());
    const int64_t start = std::llround (beat * spb);
    const int64_t len = static_cast<int64_t> (decayMs * 0.001 * kSr * 5.0);
    for (int64_t i = 0; i < len; ++i)
    {
        const int64_t j = start + i;
        if (j < 0 || j >= n)
            continue;
        const double t = static_cast<double> (i) / kSr;
        const float env = static_cast<float> (std::exp (-t / (decayMs * 0.001)));
        const float tone = static_cast<float> (std::sin (2.0 * kPi * toneHz * t));
        x[static_cast<size_t> (j)] +=
            amp * env * ((1.0f - noiseMix) * tone + noiseMix * lcg.next());
    }
}

void clampFixture (std::vector<float>& x)
{
    for (auto& v : x)
        v = std::clamp (v, -0.99f, 0.99f);
}

void addDrumBar (std::vector<float>& x, double b0, FixtureLcg& lcg)
{
    addHit (x, b0 + 0.0, 0.85f, 90.0, 55.0, 0.05f, lcg);
    addHit (x, b0 + 2.0, 0.85f, 90.0, 55.0, 0.05f, lcg);
    addHit (x, b0 + 1.0, 0.45f, 60.0, 190.0, 0.65f, lcg);
    addHit (x, b0 + 3.0, 0.45f, 60.0, 190.0, 0.65f, lcg);
    for (int e = 0; e < 8; ++e)
        addHit (x, b0 + e * 0.5 + 0.25, 0.16f, 25.0, 5000.0, 0.90f, lcg);
    for (int s = 0; s < 16; ++s)
        if (s % 4 == 3)
            addHit (x, b0 + s * 0.25, 0.055f, 18.0, 7000.0, 0.95f, lcg);
}

/** Sparse partner: a downbeat, and every other bar one answer. Lots of room. */
void addSparseBar (std::vector<float>& x, double b0, int bar, FixtureLcg& lcg)
{
    addHit (x, b0 + 0.0, 0.80f, 120.0, 60.0, 0.10f, lcg);
    if (bar % 2 == 1)
        addHit (x, b0 + 2.5, 0.45f, 60.0, 2400.0, 0.80f, lcg);
}

/** Busy partner: loud sixteenths, short tails, almost no room. */
void addBusyBar (std::vector<float>& x, double b0, FixtureLcg& lcg)
{
    for (int s = 0; s < 16; ++s)
        addHit (x, b0 + s * 0.25, s % 4 == 0 ? 0.90f : 0.65f, 28.0, s % 2 ? 2600.0 : 80.0,
                s % 2 ? 0.85f : 0.20f, lcg);
}

std::vector<float> makeDrums (int n)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    FixtureLcg lcg;
    const double spb = kSr * 60.0 / kBpm;
    const int bars = static_cast<int> (n / (spb * 4.0)) + 1;
    for (int b = 0; b < bars; ++b)
        addDrumBar (x, b * 4.0, lcg);
    clampFixture (x);
    return x;
}

std::vector<float> makeSparse (int n)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    FixtureLcg lcg { 4242u };
    const double spb = kSr * 60.0 / kBpm;
    const int bars = static_cast<int> (n / (spb * 4.0)) + 1;
    for (int b = 0; b < bars; ++b)
        addSparseBar (x, b * 4.0, b, lcg);
    clampFixture (x);
    return x;
}

std::vector<float> makeBusy (int n)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    FixtureLcg lcg { 909u };
    const double spb = kSr * 60.0 / kBpm;
    const int bars = static_cast<int> (n / (spb * 4.0)) + 1;
    for (int b = 0; b < bars; ++b)
        addBusyBar (x, b * 4.0, lcg);
    clampFixture (x);
    return x;
}

/** Sparse → busy → sparse. The whole relationship arc in one take. */
std::vector<float> makeJourney (int n, double sectionBeats)
{
    std::vector<float> x (static_cast<size_t> (n), 0.0f);
    FixtureLcg lcg { 1717u };
    const int barsPerSection = static_cast<int> (sectionBeats / 4.0);
    for (int b = 0; b < barsPerSection; ++b)
        addSparseBar (x, b * 4.0, b, lcg);
    for (int b = 0; b < barsPerSection; ++b)
        addBusyBar (x, sectionBeats + b * 4.0, lcg);
    for (int b = 0; b < barsPerSection; ++b)
        addSparseBar (x, 2.0 * sectionBeats + b * 4.0, b, lcg);
    clampFixture (x);
    return x;
}

/** Sustained bed with slow swells, one level step and one timbre step. */
std::vector<float> makePad (int n)
{
    std::vector<float> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        const double swell = 0.55 + 0.45 * std::sin (2.0 * kPi * t / 6.0);
        const double level = (t > 8.0 && t < 16.0) ? 1.7 : 1.0;
        const double bright = t > 12.0 ? 0.55 : 0.12;
        const double v = std::sin (2.0 * kPi * 110.0 * t)
                         + 0.7 * std::sin (2.0 * kPi * 220.0 * t)
                         + bright * std::sin (2.0 * kPi * 1320.0 * t);
        x[static_cast<size_t> (i)] = static_cast<float> (0.16 * swell * level * v);
    }
    for (auto& v : x)
        v = std::clamp (v, -0.99f, 0.99f);
    return x;
}

void writeWav (const fs::path& path, const std::vector<float>& L, const std::vector<float>& R)
{
    juce::AudioBuffer<float> buf (2, static_cast<int> (L.size()));
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        buf.setSample (0, i, L[static_cast<size_t> (i)]);
        buf.setSample (1, i, R[static_cast<size_t> (i)]);
    }
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> stream (
        new juce::FileOutputStream (juce::File (path.string())));
    if (stream->failedToOpen())
    {
        std::cerr << "could not open " << path << "\n";
        return;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (stream.release(), kSr, 2, 24, {}, 0));
    if (writer)
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    std::cout << "wrote " << path.filename().string() << "\n";
}

enum class Fixture { Drums, Sparse, Busy, Pad, Journey, Silence };

const char* fixtureName (Fixture f)
{
    switch (f)
    {
        case Fixture::Drums: return "drums";
        case Fixture::Sparse: return "sparse";
        case Fixture::Busy: return "busy";
        case Fixture::Pad: return "pad";
        case Fixture::Journey: return "journey";
        default: return "silence";
    }
}

struct Variant
{
    std::string name;
    Fixture fixture = Fixture::Drums;
    float mix = 1.0f;
    float sens = 0.50f;
    float hunger = 0.35f;
    float mutation = 0.25f;
    float output = 0.85f;
    uint64_t seed = 2002;
    double beats = 64.0;
};

std::vector<float> makeFixture (Fixture f, int n, double beats)
{
    switch (f)
    {
        case Fixture::Drums: return makeDrums (n);
        case Fixture::Sparse: return makeSparse (n);
        case Fixture::Busy: return makeBusy (n);
        case Fixture::Pad: return makePad (n);
        case Fixture::Journey: return makeJourney (n, beats / 3.0);
        default: return std::vector<float> (static_cast<size_t> (n), 0.0f);
    }
}

void render (const fs::path& dir, const Variant& v)
{
    const int n = beatsToSamples (v.beats);
    std::vector<float> in = makeFixture (v.fixture, n, v.beats);
    // Slight stereo offset so the balance feature and the pan law have something
    // to work with, without turning the fixture into a different signal.
    std::vector<float> inL (in), inR (in);
    for (size_t i = 0; i < inR.size(); ++i)
        inR[i] *= 0.92f;

    std::vector<float> outL (static_cast<size_t> (n)), outR (static_cast<size_t> (n));

    pfl::dsp::SignalParasiteEngine eng;
    eng.prepare (kSr, 256);
    eng.setSeed (v.seed);
    eng.setMacros (v.mix, v.sens, v.hunger, v.mutation, v.output);
    eng.snapMacros();
    eng.forceRebuild (0);
    eng.setTraceEnabled (true);

    const double bps = (kBpm / 60.0) / kSr;
    for (int done = 0; done < n; done += 256)
    {
        const int m = std::min (256, n - done);
        eng.process (inL.data() + done, inR.data() + done, outL.data() + done,
                     outR.data() + done, m, static_cast<double> (done) * bps, kBpm, true);
    }

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::max (std::abs (outL[static_cast<size_t> (i)]),
                                         std::abs (outR[static_cast<size_t> (i)])));

    writeWav (dir / (v.name + ".wav"), outL, outR);

    std::ofstream tr (dir / (v.name + "-trace.txt"));
    tr << "PFL Signal Parasite - Stage 2 (algorithm v"
       << pfl::dsp::SignalParasiteEngine::kAlgorithmVersion << ")\n";
    tr << "fixture=" << fixtureName (v.fixture) << " sr=" << kSr << " bpm=" << kBpm
       << " beats=" << v.beats << "\n";
    tr << "mix=" << v.mix << " sensitivity=" << v.sens << " hunger=" << v.hunger
       << " mutation=" << v.mutation << " output=" << v.output << " seed=" << v.seed << "\n";
    tr << "stimuli=" << eng.stimulusCount() << " (attack=" << eng.attackCount()
       << " shift=" << eng.shiftCount() << ")"
       << " responses=" << eng.responseCount()
       << " responses/16beats=" << (eng.responseCount() * 16.0 / v.beats) << "\n";
    tr << "dnaGeneration=" << eng.dnaGeneration() << " responseDuty=" << eng.responseDuty()
       << " minResponseGapBeats=" << eng.minResponseGapBeats() << " peak=" << peak << "\n";
    tr << "overflowDrops=" << eng.overflowDrops();
    for (int i = 0; i < static_cast<int> (pfl::dsp::ParasiteSuppressReason::Count); ++i)
    {
        const auto reason = static_cast<pfl::dsp::ParasiteSuppressReason> (i);
        if (reason == pfl::dsp::ParasiteSuppressReason::None)
            continue;
        tr << " " << pfl::dsp::parasiteSuppressName (reason) << "=" << eng.suppressCount (reason);
    }
    tr << "\n";
    tr << "acceptRatio=" << eng.acceptRatio() << " historySize=" << eng.historySize()
       << " stateTransitions=" << eng.stateTransitions()
       << " finalState=" << pfl::dsp::parasiteRelationshipName (eng.relationshipState()) << "\n";

    tr << "occupancy:";
    for (int i = 0; i < static_cast<int> (pfl::dsp::ParasiteRelationship::Count); ++i)
    {
        const auto st = static_cast<pfl::dsp::ParasiteRelationship> (i);
        tr << " " << pfl::dsp::parasiteRelationshipName (st) << "=" << eng.stateOccupancy (st);
    }
    tr << "\n";

    const auto& pr = eng.pressures();
    tr << "pressures: source=" << pr.source << " attachment=" << pr.attachment
       << " conversation=" << pr.conversation << " withdrawal=" << pr.withdrawal
       << " fatigue=" << pr.fatigue << "\n";
    const auto& hs = eng.historySummary();
    tr << "window: offered=" << hs.offered << " answered=" << hs.answered
       << " exchanges=" << hs.exchanges << " ratePerBeat=" << hs.ratePerBeat
       << " interest=" << hs.interest << " gapScore=" << hs.gapScore
       << " success=" << hs.success << "\n";

    tr << "stimulusFingerprint=" << eng.stimulusFingerprint() << "\n";
    tr << "responseFingerprint=" << eng.responseFingerprint() << "\n";
    tr << "stateFingerprint=" << eng.stateFingerprint() << "\n";
    tr << "dnaFingerprint=" << eng.dnaFingerprint() << "\n\n";

    const double spb = kSr * 60.0 / kBpm;
    tr << "# state transitions: beat from > to tick\n";
    for (const auto& t : eng.stateTransitionTrace())
    {
        tr << static_cast<double> (t.relSample) / spb << " "
           << pfl::dsp::parasiteRelationshipName (
                  static_cast<pfl::dsp::ParasiteRelationship> (t.from))
           << " > "
           << pfl::dsp::parasiteRelationshipName (
                  static_cast<pfl::dsp::ParasiteRelationship> (t.to))
           << " " << (t.major ? "major" : "minor") << "\n";
    }

    tr << "\n# responses: onsetBeat delaySlot durSlot durationBeats strength brightness pan kind"
          " state\n";
    for (const auto& r : eng.responses())
    {
        tr << r.onsetBeat << " " << r.delaySlot << " " << r.durSlot << " " << r.durationBeats
           << " " << r.strength << " " << r.brightness << " " << r.pan << " "
           << (r.kind == pfl::dsp::StimulusKind::Attack ? "ATTACK" : "SHIFT") << " "
           << pfl::dsp::parasiteRelationshipName (
                  static_cast<pfl::dsp::ParasiteRelationship> (r.state))
           << "\n";
    }

    tr << "\n# stimuli: beat kind strength energy brightness change balance\n";
    for (const auto& s : eng.stimuli())
    {
        tr << s.ppq << " " << (s.kind == pfl::dsp::StimulusKind::Attack ? "ATTACK" : "SHIFT")
           << " " << s.strength << " " << s.energy << " " << s.brightness << " " << s.change
           << " " << s.balance << "\n";
    }
}

/** Wet only, dry only and the shipped default mix for one fixture. */
void renderMixTrio (const fs::path& dir, const std::string& stem, Fixture fixture)
{
    render (dir, { stem + "-dry", fixture, 0.0f });
    render (dir, { stem + "-wet", fixture, 1.0f });
    render (dir, { stem + "-mix050", fixture, 0.50f });
}

// ---------------------------------------------------------------------------
// Stage 3 — performance intervention
// ---------------------------------------------------------------------------

namespace perf_ns = pfl::parasite_perf;

struct Cue
{
    double beat = 0.0;
    perf_ns::Command cmd = perf_ns::Command::FreezeOn;
};

struct PerfVariant
{
    std::string name;
    Fixture fixture = Fixture::Sparse;
    double beats = 96.0;
    std::vector<Cue> cues;
    float mix = 1.0f;
    float sens = 0.50f;
    float hunger = 0.60f;
    float mutation = 0.25f;
    float output = 0.85f;
    uint64_t seed = 2002;
    // Optional HUNGER automation, for the "collapse is not a fade" comparison.
    double hungerFadeFrom = -1.0;
    double hungerFadeTo = -1.0;
};

void renderPerf (const fs::path& dir, const PerfVariant& v)
{
    const int n = beatsToSamples (v.beats);
    std::vector<float> in = makeFixture (v.fixture, n, v.beats);
    std::vector<float> inL (in), inR (in);
    for (size_t i = 0; i < inR.size(); ++i)
        inR[i] *= 0.92f;

    std::vector<float> outL (static_cast<size_t> (n)), outR (static_cast<size_t> (n));

    pfl::dsp::SignalParasiteEngine eng;
    perf_ns::SignalParasitePerformanceController perf;
    eng.prepare (kSr, 256);
    eng.setSeed (v.seed);
    eng.setMacros (v.mix, v.sens, v.hunger, v.mutation, v.output);
    eng.snapMacros();
    eng.forceRebuild (0);
    eng.setTraceEnabled (true);
    perf.reset (v.seed);
    perf.setTraceEnabled (true);

    // The processor's ~4 ms click-safe mute, reproduced so the wav shows what
    // SILENCE actually sounds like rather than what the engine did underneath.
    pfl::dsp::ParamSmoother silenceSm;
    silenceSm.prepare (kSr, 0.004f);
    silenceSm.setCurrentAndTarget (1.0f);

    const double bps = (kBpm / 60.0) / kSr;
    size_t nextCue = 0;
    for (int done = 0; done < n; done += 256)
    {
        const int m = std::min (256, n - done);
        const double ppq = static_cast<double> (done) * bps;

        while (nextCue < v.cues.size() && v.cues[nextCue].beat <= ppq)
        {
            perf.trigger (v.cues[nextCue].cmd, ppq, eng);
            ++nextCue;
        }
        if (v.hungerFadeFrom >= 0.0 && ppq >= v.hungerFadeFrom)
        {
            const double t = std::clamp (
                (ppq - v.hungerFadeFrom) / std::max (1.0, v.hungerFadeTo - v.hungerFadeFrom),
                0.0, 1.0);
            eng.setHunger (static_cast<float> (v.hunger * (1.0 - t)));
        }

        perf.tick (ppq, true, eng);
        eng.process (inL.data() + done, inR.data() + done, outL.data() + done,
                     outR.data() + done, m, ppq, kBpm, true);

        silenceSm.setTarget (perf.mode() == perf_ns::Mode::Silenced ? 0.0f : 1.0f);
        for (int i = 0; i < m; ++i)
        {
            const float g = silenceSm.getNext();
            outL[static_cast<size_t> (done + i)] *= g;
            outR[static_cast<size_t> (done + i)] *= g;
        }
    }

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::max (std::abs (outL[static_cast<size_t> (i)]),
                                         std::abs (outR[static_cast<size_t> (i)])));

    writeWav (dir / (v.name + ".wav"), outL, outR);

    std::ofstream tr (dir / (v.name + "-trace.txt"));
    tr << "PFL Signal Parasite - Stage 3 (algorithm v"
       << pfl::dsp::SignalParasiteEngine::kAlgorithmVersion << ", performance engine v"
       << perf_ns::kPerformanceEngineVersion << ")\n";
    tr << "fixture=" << fixtureName (v.fixture) << " sr=" << kSr << " bpm=" << kBpm
       << " beats=" << v.beats << "\n";
    tr << "mix=" << v.mix << " sensitivity=" << v.sens << " hunger=" << v.hunger
       << " mutation=" << v.mutation << " output=" << v.output << " seed=" << v.seed << "\n";
    tr << "finalMode=" << perf_ns::modeName (perf.mode())
       << " collapsePhase=" << perf_ns::collapsePhaseName (perf.collapsePhase())
       << " dormant=" << (perf.dormant() ? 1 : 0)
       << " freezeLatched=" << (perf.state().freezeLatched ? 1 : 0)
       << " seedNow=" << eng.seed() << " reseeds=" << perf.state().reseedCount << "\n";
    tr << "stimuli=" << eng.stimulusCount() << " responses=" << eng.responseCount()
       << " dnaGeneration=" << eng.dnaGeneration() << " responseDuty=" << eng.responseDuty()
       << " peak=" << peak << "\n";
    tr << "suppress:";
    for (int i = 0; i < static_cast<int> (pfl::dsp::ParasiteSuppressReason::Count); ++i)
    {
        const auto reason = static_cast<pfl::dsp::ParasiteSuppressReason> (i);
        if (reason == pfl::dsp::ParasiteSuppressReason::None)
            continue;
        tr << " " << pfl::dsp::parasiteSuppressName (reason) << "=" << eng.suppressCount (reason);
    }
    tr << "\n";
    tr << "finalState=" << pfl::dsp::parasiteRelationshipName (eng.relationshipState())
       << " transitions=" << eng.stateTransitions() << " historySize=" << eng.historySize()
       << "\n";
    tr << "stimulusFingerprint=" << eng.stimulusFingerprint() << "\n";
    tr << "responseFingerprint=" << eng.responseFingerprint() << "\n";
    tr << "stateFingerprint=" << eng.stateFingerprint() << "\n";
    tr << "dnaFingerprint=" << eng.dnaFingerprint() << "\n\n";

    tr << "# performance events: ppq command detail\n";
    for (const auto& e : perf.events())
        tr << e.ppq << " " << perf_ns::commandName (e.command) << " " << e.detail << "\n";

    tr << "\n# responses: onsetBeat delaySlot durSlot durationBeats strength brightness pan"
          " kind state\n";
    for (const auto& r : eng.responses())
    {
        tr << r.onsetBeat << " " << r.delaySlot << " " << r.durSlot << " " << r.durationBeats
           << " " << r.strength << " " << r.brightness << " " << r.pan << " "
           << (r.kind == pfl::dsp::StimulusKind::Attack ? "ATTACK" : "SHIFT") << " "
           << pfl::dsp::parasiteRelationshipName (
                  static_cast<pfl::dsp::ParasiteRelationship> (r.state))
           << "\n";
    }
}

void renderStage3 (const fs::path& dir)
{
    using Cmd = perf_ns::Command;

    // FREEZE holds the mood and the DNA; MUTATE nudges one trait under the hold.
    renderPerf (dir, { "freeze-mutate", Fixture::Sparse, 128.0,
                       { { 32.0, Cmd::FreezeOn },
                         { 48.0, Cmd::Mutate },
                         { 64.0, Cmd::Mutate },
                         { 96.0, Cmd::FreezeOff } } });

    // The whole 24-beat arc, then eight bars of the dormancy it lands in.
    renderPerf (dir, { "collapse-active", Fixture::Sparse, 128.0, { { 24.0, Cmd::Collapse } } });
    renderPerf (dir, { "collapse-drums", Fixture::Drums, 128.0, { { 24.0, Cmd::Collapse } } });

    // Same source, same appetite, HUNGER faded to nothing instead. A dimmer,
    // not an arc: no phases, no ending, and it comes back if you turn it up.
    PerfVariant fade;
    fade.name = "collapse-vs-hunger-fade";
    fade.fixture = Fixture::Sparse;
    fade.beats = 128.0;
    fade.hungerFadeFrom = 24.0;
    fade.hungerFadeTo = 48.0;
    renderPerf (dir, fade);

    // Dormant with a busy source in front of it: still listening, still silent.
    renderPerf (dir, { "dormant-under-pressure", Fixture::Busy, 128.0,
                       { { 8.0, Cmd::Collapse } } });

    // RESEED is the way out of dormancy — a different parasite, same source.
    renderPerf (dir, { "reseed-from-dormant", Fixture::Sparse, 160.0,
                       { { 16.0, Cmd::Collapse }, { 64.0, Cmd::Reseed } } });
    renderPerf (dir, { "reseed-live", Fixture::Drums, 128.0, { { 48.0, Cmd::Reseed } } });

    // SILENCE mutes the plugin while the relationship keeps running underneath,
    // and nothing owed is paid back when the mute lifts.
    renderPerf (dir, { "silence-listening", Fixture::Drums, 128.0,
                       { { 32.0, Cmd::SilenceOn }, { 80.0, Cmd::SilenceOff } } });
    renderPerf (dir, { "silence-frozen", Fixture::Drums, 128.0,
                       { { 24.0, Cmd::FreezeOn },
                         { 32.0, Cmd::SilenceOn },
                         { 80.0, Cmd::SilenceOff } } });

    // One take through the whole vocabulary, at the shipped mix.
    PerfVariant journey;
    journey.name = "performance-journey";
    journey.fixture = Fixture::Journey;
    journey.beats = 240.0;
    journey.mix = 0.50f;
    journey.cues = { { 32.0, Cmd::FreezeOn }, { 44.0, Cmd::Mutate },  { 60.0, Cmd::FreezeOff },
                     { 88.0, Cmd::SilenceOn }, { 104.0, Cmd::SilenceOff },
                     { 132.0, Cmd::Collapse }, { 180.0, Cmd::Reseed } };
    renderPerf (dir, journey);
}
} // namespace

int main (int argc, char** argv)
{
    fs::path dir = "renders/signal-parasite/stage3";
    if (argc > 1)
        dir = argv[1];
    std::error_code ec;
    fs::create_directories (dir, ec);

    const auto dirStr = dir.string();
    if (dirStr.find ("stage3") != std::string::npos)
    {
        renderStage3 (dir);
        std::cout << "stage3 renders in " << dir << "\n";
        return 0;
    }

    renderMixTrio (dir, "drums", Fixture::Drums);
    renderMixTrio (dir, "pad", Fixture::Pad);

    // SENSITIVITY: how much of the source it hears. HUNGER fixed.
    for (auto s : { 0.20f, 0.50f, 0.90f })
        render (dir, { "sens-" + juce::String (s, 2).toStdString(), Fixture::Drums, 1.0f, s,
                       0.35f });

    // HUNGER: how often it answers what it heard. SENSITIVITY fixed.
    for (auto h : { 0.00f, 0.35f, 0.70f, 1.00f })
        render (dir, { "hunger-" + juce::String (h, 2).toStdString(), Fixture::Drums, 1.0f, 0.50f,
                       h });

    // MUTATION: how fast the response grammar drifts. Density must not move.
    for (auto m : { 0.00f, 0.25f, 1.00f })
        render (dir, { "mutation-" + juce::String (m, 2).toStdString(), Fixture::Drums, 1.0f,
                       0.50f, 0.50f, m, 0.85f, 2002, 128.0 });

    // Seed is the whole personality at fixed macros.
    for (uint64_t seed : { 2002ull, 7777ull })
        render (dir, { "seed-" + std::to_string (seed), Fixture::Drums, 1.0f, 0.50f, 0.50f, 0.25f,
                       0.85f, seed });

    // Nothing in, nothing out — the parasite has no voice of its own, and no
    // state can invent one.
    render (dir, { "silence", Fixture::Silence, 1.0f, 1.00f, 1.00f, 1.00f });

    // Stage 2: the same ears and the same appetite on three kinds of partner.
    // Sparse leaves room and gets bonded with; busy is a wall it backs away
    // from; the pad is a slow acquaintance.
    renderMixTrio (dir, "sparse", Fixture::Sparse);
    renderMixTrio (dir, "busy", Fixture::Busy);
    for (auto h : { 0.35f, 0.70f })
    {
        const auto tag = juce::String (h, 2).toStdString();
        render (dir, { "state-sparse-hunger-" + tag, Fixture::Sparse, 1.0f, 0.50f, h, 0.0f,
                       0.85f, 2002, 96.0 });
        render (dir, { "state-busy-hunger-" + tag, Fixture::Busy, 1.0f, 0.50f, h, 0.0f, 0.85f,
                       2002, 96.0 });
        render (dir, { "state-pad-hunger-" + tag, Fixture::Pad, 1.0f, 0.50f, h, 0.0f, 0.85f,
                       2002, 96.0 });
    }

    // The whole arc in one take: sparse → busy → sparse. Bond, back off, return.
    render (dir, { "journey-relationship-wet", Fixture::Journey, 1.0f, 0.50f, 0.60f, 0.25f, 0.85f,
                   2002, 192.0 });
    render (dir, { "journey-relationship-mix", Fixture::Journey, 0.50f, 0.50f, 0.60f, 0.25f,
                   0.85f, 2002, 192.0 });
    // Same arc with DNA frozen, so every difference in the trace is the state.
    render (dir, { "journey-relationship-nomut", Fixture::Journey, 1.0f, 0.50f, 0.60f, 0.00f,
                   0.85f, 2002, 192.0 });

    // Long journey at defaults: 64 bars of drums, wet and at the shipped mix.
    render (dir, { "journey-wet", Fixture::Drums, 1.0f, 0.50f, 0.35f, 0.25f, 0.85f, 2002, 256.0 });
    render (dir, { "journey-mix", Fixture::Drums, 0.50f, 0.50f, 0.35f, 0.25f, 0.85f, 2002,
                   256.0 });

    std::cout << "renders in " << dir << "\n";
    return 0;
}

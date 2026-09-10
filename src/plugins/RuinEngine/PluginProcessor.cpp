#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
float readParam (juce::AudioProcessorValueTreeState& apvts, const char* id, float fallback) noexcept
{
    if (auto* p = apvts.getRawParameterValue (id))
        return p->load();
    return fallback;
}
} // namespace

RuinEngineProcessor::RuinEngineProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", createParameterLayout())
{
}

RuinEngineProcessor::~RuinEngineProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout RuinEngineProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "MIX",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.45f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "age", 1 }, "AGE",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "instability", 1 }, "INSTABILITY",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "OUTPUT",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.85f));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "seed", 1 }, "SEED", 0, 999999, 2002));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "freeze", 1 }, "Freeze", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "silence", 1 }, "Silence", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mutate", 1 }, "Mutate",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "collapse", 1 }, "Collapse",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reseed", 1 }, "Reseed",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.0f));

    return { params.begin(), params.end() };
}

void RuinEngineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    engine_.prepare (sampleRate_);
    monoScratch_.assign (static_cast<size_t> (std::max (1, samplesPerBlock)), 0.0f);
    syncEngineFromParams();
    engine_.snapMacros();
    lastPpq_ = 0.0;
    lastPlaying_ = false;

    lastFreezeParam_ = readParam (apvts_, "freeze", 0.0f) > 0.5f;
    lastSilenceParam_ = readParam (apvts_, "silence", 0.0f) > 0.5f;
    lastMutateParam_ = readParam (apvts_, "mutate", 0.0f);
    lastCollapseParam_ = readParam (apvts_, "collapse", 0.0f);
    lastReseedParam_ = readParam (apvts_, "reseed", 0.0f);

    const auto seed = static_cast<uint64_t> (juce::jlimit (
        0, 999999, static_cast<int> (readParam (apvts_, "seed", 2002.0f))));
    performance_.reset (seed == 0 ? 1ull : seed);

    // Restore latched toggles without false edges
    if (lastFreezeParam_)
        performance_.trigger (pfl::ruin_perf::Command::FreezeOn, 0.0, engine_);
    if (lastSilenceParam_)
        performance_.trigger (pfl::ruin_perf::Command::SilenceOn, 0.0, engine_);
}

void RuinEngineProcessor::releaseResources() {}

bool RuinEngineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in.isDisabled() || out.isDisabled())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in.size() == out.size();
}

void RuinEngineProcessor::writeSeedToHost (uint64_t seed) noexcept
{
    const int s = static_cast<int> (std::clamp (seed, 0ull, 999999ull));
    if (auto* p = apvts_.getParameter ("seed"))
    {
        const float norm = apvts_.getParameterRange ("seed").convertTo0to1 (static_cast<float> (s));
        p->setValueNotifyingHost (norm);
    }
    lastSeedParam_ = s;
}

void RuinEngineProcessor::syncEngineFromParams() noexcept
{
    const auto seed = static_cast<uint64_t> (juce::jlimit (
        0, 999999, static_cast<int> (readParam (apvts_, "seed", 2002.0f))));
    const uint64_t s = seed == 0 ? 1ull : seed;
    if (lastSeedParam_ < 0)
    {
        engine_.setSeed (s);
        performance_.reset (s);
        lastSeedParam_ = static_cast<int> (s);
    }
    else if (static_cast<int> (s) != lastSeedParam_)
    {
        lastSeedParam_ = static_cast<int> (s);
        engine_.setSeed (s);
    }

    engine_.setMix (readParam (apvts_, "mix", 0.45f));
    engine_.setAge (readParam (apvts_, "age", 0.35f));
    engine_.setInstability (readParam (apvts_, "instability", 0.35f));
    engine_.setOutput (readParam (apvts_, "output", 0.85f));
}

void RuinEngineProcessor::syncPerformanceCommands (double ppq) noexcept
{
    using Cmd = pfl::ruin_perf::Command;

    const bool freeze = readParam (apvts_, "freeze", 0.0f) > 0.5f;
    const bool silence = readParam (apvts_, "silence", 0.0f) > 0.5f;
    const float mutate = readParam (apvts_, "mutate", 0.0f);
    const float collapse = readParam (apvts_, "collapse", 0.0f);
    const float reseed = readParam (apvts_, "reseed", 0.0f);

    if (freeze && ! lastFreezeParam_)
        performance_.trigger (Cmd::FreezeOn, ppq, engine_);
    else if (! freeze && lastFreezeParam_)
        performance_.trigger (Cmd::FreezeOff, ppq, engine_);

    if (silence && ! lastSilenceParam_)
        performance_.trigger (Cmd::SilenceOn, ppq, engine_);
    else if (! silence && lastSilenceParam_)
        performance_.trigger (Cmd::SilenceOff, ppq, engine_);

    if (mutate >= 0.5f && lastMutateParam_ < 0.5f)
        performance_.trigger (Cmd::Mutate, ppq, engine_);
    if (collapse >= 0.5f && lastCollapseParam_ < 0.5f)
        performance_.trigger (Cmd::Collapse, ppq, engine_);
    if (reseed >= 0.5f && lastReseedParam_ < 0.5f)
        performance_.trigger (Cmd::Reseed, ppq, engine_);

    lastFreezeParam_ = freeze;
    lastSilenceParam_ = silence;
    lastMutateParam_ = mutate;
    lastCollapseParam_ = collapse;
    lastReseedParam_ = reseed;

    uint64_t newSeed = 0;
    if (performance_.takeSeedDirty (newSeed))
        writeSeedToHost (newSeed);
}

void RuinEngineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    syncEngineFromParams();

    auto* playHead = getPlayHead();
    double ppq = lastPpq_;
    double bpm = 120.0;
    bool playing = false;
    if (playHead != nullptr)
    {
        if (auto pos = playHead->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = *b;
            if (auto p = pos->getPpqPosition())
                ppq = *p;
            playing = pos->getIsPlaying();
        }
    }

    if (! playing)
        engine_.setEvolutionPaused (true);
    else
        engine_.setBypassed (false);

    syncPerformanceCommands (ppq);
    performance_.tick (ppq, playing, engine_);

    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (n <= 0 || numCh <= 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    const double beatsPerSample = ((bpm > 1.0 ? bpm : 120.0) / 60.0) / sampleRate_;

    if (R == nullptr)
    {
        const int scratchN = std::max (1, static_cast<int> (monoScratch_.size()));
        int offset = 0;
        while (offset < n)
        {
            const int chunk = std::min (n - offset, scratchN);
            for (int i = 0; i < chunk; ++i)
                monoScratch_[static_cast<size_t> (i)] = L[offset + i];
            engine_.process (L + offset, monoScratch_.data(), chunk, playing,
                             ppq + static_cast<double> (offset) * beatsPerSample, bpm);
            offset += chunk;
        }
    }
    else
    {
        engine_.process (L, R, n, playing, ppq, bpm);
    }

    lastPpq_ = ppq;
    lastPlaying_ = playing;
}

void RuinEngineProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    engine_.setBypassed (true);
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
}

void RuinEngineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("PFLRuinEngineState");
    root.setProperty ("stateVersion", 4, nullptr);
    root.setProperty ("algorithmVersion", pfl::dsp::RuinEngine::kAlgorithmVersion, nullptr);
    root.appendChild (apvts_.copyState(), nullptr);

    const auto w = engine_.wearState();
    const auto f = engine_.scarFloor();
    juce::ValueTree wear ("WearState");
    wear.setProperty ("spectral", w.spectral, nullptr);
    wear.setProperty ("nonlinear", w.nonlinear, nullptr);
    wear.setProperty ("temporal", w.temporal, nullptr);
    wear.setProperty ("scarSpectral", f.spectral, nullptr);
    wear.setProperty ("scarNonlinear", f.nonlinear, nullptr);
    wear.setProperty ("scarTemporal", f.temporal, nullptr);
    root.appendChild (wear, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void RuinEngineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (tree.hasType (apvts_.state.getType()))
        {
            apvts_.replaceState (tree);
            engine_.resetWearFresh();
            syncEngineFromParams();
            engine_.snapMacros();
            return;
        }

        if (tree.hasType ("PFLRuinEngineState"))
        {
            if (auto params = tree.getChildWithName (apvts_.state.getType()); params.isValid())
                apvts_.replaceState (params);

            pfl::dsp::WearState w {}, floor {};
            if (auto wear = tree.getChildWithName ("WearState"); wear.isValid())
            {
                w.spectral = (float) wear.getProperty ("spectral", 0.0f);
                w.nonlinear = (float) wear.getProperty ("nonlinear", 0.0f);
                w.temporal = (float) wear.getProperty ("temporal", 0.0f);
                floor.spectral = (float) wear.getProperty ("scarSpectral", 0.0f);
                floor.nonlinear = (float) wear.getProperty ("scarNonlinear", 0.0f);
                floor.temporal = (float) wear.getProperty ("scarTemporal", 0.0f);
            }
            syncEngineFromParams();
            engine_.setWearState (w, floor);
            engine_.snapMacros();

            // Re-seed performance from params; cancel mid-collapse (not journaled).
            const auto seed = static_cast<uint64_t> (juce::jlimit (
                0, 999999, static_cast<int> (readParam (apvts_, "seed", 2002.0f))));
            performance_.reset (seed == 0 ? 1ull : seed);
            lastFreezeParam_ = readParam (apvts_, "freeze", 0.0f) > 0.5f;
            lastSilenceParam_ = readParam (apvts_, "silence", 0.0f) > 0.5f;
            lastMutateParam_ = readParam (apvts_, "mutate", 0.0f);
            lastCollapseParam_ = readParam (apvts_, "collapse", 0.0f);
            lastReseedParam_ = readParam (apvts_, "reseed", 0.0f);
            if (lastFreezeParam_)
                performance_.trigger (pfl::ruin_perf::Command::FreezeOn, 0.0, engine_);
            if (lastSilenceParam_)
                performance_.trigger (pfl::ruin_perf::Command::SilenceOn, 0.0, engine_);
        }
    }
}

juce::AudioProcessorEditor* RuinEngineProcessor::createEditor()
{
    return new RuinEngineEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RuinEngineProcessor();
}

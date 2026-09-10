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

PulseColonyProcessor::PulseColonyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", createParameterLayout())
{
}

PulseColonyProcessor::~PulseColonyProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout PulseColonyProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "MIX",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.70f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "density", 1 }, "DENSITY",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.50f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mutation", 1 }, "MUTATION",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "motion", 1 }, "MOTION",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "OUTPUT",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.85f));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "seed", 1 }, "SEED", 0, 999999, 2002));
    return { params.begin(), params.end() };
}

void PulseColonyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    engine_.prepare (sampleRate_, samplesPerBlock);
    monoScratch_.assign (static_cast<size_t> (std::max (1, samplesPerBlock)), 0.0f);
    syncEngineFromParams();
    engine_.snapMacros();
    engine_.forceRebuild (0);
    lastPpq_ = 0.0;
    setLatencySamples (0);
}

void PulseColonyProcessor::releaseResources() {}

bool PulseColonyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void PulseColonyProcessor::syncEngineFromParams() noexcept
{
    const auto seed = static_cast<uint64_t> (juce::jlimit (
        0, 999999, static_cast<int> (readParam (apvts_, "seed", 2002.0f))));
    const uint64_t s = seed == 0 ? 1ull : seed;
    if (static_cast<int> (s) != lastSeedParam_)
    {
        engine_.setSeed (s);
        lastSeedParam_ = static_cast<int> (s);
    }
    engine_.setMix (readParam (apvts_, "mix", 0.70f));
    engine_.setDensity (readParam (apvts_, "density", 0.50f));
    engine_.setMutation (readParam (apvts_, "mutation", 0.35f));
    engine_.setMotion (readParam (apvts_, "motion", 0.35f));
    engine_.setOutput (readParam (apvts_, "output", 0.85f));
}

void PulseColonyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
}

void PulseColonyProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
}

void PulseColonyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("PFLPulseColonyState");
    root.setProperty ("stateVersion", 1, nullptr);
    root.setProperty ("algorithmVersion", pfl::dsp::PulseColonyEngine::kAlgorithmVersion, nullptr);
    root.appendChild (apvts_.copyState(), nullptr);
    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void PulseColonyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (apvts_.state.getType()))
        {
            apvts_.replaceState (tree);
            syncEngineFromParams();
            engine_.snapMacros();
            engine_.forceRebuild (0);
            return;
        }
        if (tree.hasType ("PFLPulseColonyState"))
        {
            if (auto params = tree.getChildWithName (apvts_.state.getType()); params.isValid())
                apvts_.replaceState (params);
            syncEngineFromParams();
            engine_.snapMacros();
            engine_.forceRebuild (0);
        }
    }
}

juce::AudioProcessorEditor* PulseColonyProcessor::createEditor()
{
    return new PulseColonyEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PulseColonyProcessor();
}

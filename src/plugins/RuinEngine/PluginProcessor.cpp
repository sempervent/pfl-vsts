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

void RuinEngineProcessor::syncEngineFromParams() noexcept
{
    const auto seed = static_cast<uint64_t> (juce::jlimit (
        0, 999999, static_cast<int> (readParam (apvts_, "seed", 2002.0f))));
    engine_.setSeed (seed == 0 ? 1ull : seed);
    engine_.setMix (readParam (apvts_, "mix", 0.45f));
    engine_.setAge (readParam (apvts_, "age", 0.35f));
    engine_.setInstability (readParam (apvts_, "instability", 0.35f));
    engine_.setOutput (readParam (apvts_, "output", 0.85f));
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

    // Prefer contiguous PPQ when host reports playing; if stopped, pause evolution.
    if (! playing)
        engine_.setEvolutionPaused (true);
    else
        engine_.setBypassed (false);

    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (n <= 0 || numCh <= 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    const double beatsPerSample = ((bpm > 1.0 ? bpm : 120.0) / 60.0) / sampleRate_;

    // Mono: duplicate into preallocated scratch (chunk if host exceeds prepare size).
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
    // Host expects input → output passthrough for effects.
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
}

void RuinEngineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts_.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void RuinEngineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts_.state.getType()))
        {
            apvts_.replaceState (juce::ValueTree::fromXml (*xml));
            syncEngineFromParams();
            engine_.snapMacros();
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

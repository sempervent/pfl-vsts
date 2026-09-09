#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
DroneOrganismProcessor::DroneOrganismProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", createParameterLayout())
{
}

DroneOrganismProcessor::~DroneOrganismProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout DroneOrganismProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 },
        "Output",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f, 0.5f },
        0.7f));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "seed", 1 },
        "Seed",
        0, 999999, 1001));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "density", 1 },
        "Density",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.55f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drift", 1 },
        "Drift",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.25f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dirt", 1 },
        "Dirt",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.35f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "space", 1 },
        "Space",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.4f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mutation", 1 },
        "Mutation",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.3f));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String DroneOrganismProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DroneOrganismProcessor::acceptsMidi() const { return true; }
bool DroneOrganismProcessor::producesMidi() const { return false; }
bool DroneOrganismProcessor::isMidiEffect() const { return false; }
double DroneOrganismProcessor::getTailLengthSeconds() const { return 6.0; }

int DroneOrganismProcessor::getNumPrograms() { return 1; }
int DroneOrganismProcessor::getCurrentProgram() { return 0; }
void DroneOrganismProcessor::setCurrentProgram (int) {}
const juce::String DroneOrganismProcessor::getProgramName (int) { return {}; }
void DroneOrganismProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void DroneOrganismProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;

    for (auto& v : voices_)
        v.prepare (sampleRate);

    dcLeft_.prepare (sampleRate);
    dcRight_.prepare (sampleRate);
    limiterLeft_.prepare (sampleRate);
    limiterRight_.prepare (sampleRate);

    outputSmooth_.reset (sampleRate, 0.05);
    outputSmooth_.setCurrentAndTargetValue (apvts_.getRawParameterValue ("output")->load());

    updateVoicesFromParams();
}

void DroneOrganismProcessor::releaseResources() {}

bool DroneOrganismProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void DroneOrganismProcessor::updateVoicesFromParams() noexcept
{
    const float density = apvts_.getRawParameterValue ("density")->load();
    const float drift = apvts_.getRawParameterValue ("drift")->load();
    const float dirt = apvts_.getRawParameterValue ("dirt")->load();

    // Phase 1: density gates how many of the 4 voices are held open
    const int activeCount = 1 + static_cast<int> (std::round (density * static_cast<float> (kNumVoices - 1)));

    for (int i = 0; i < kNumVoices; ++i)
    {
        pfl::dsp::VoiceParams p;
        p.baseMidiNote = kBaseNotes[static_cast<size_t> (i)];
        p.level = 0.22f;
        p.attackSec = 2.0f + 0.4f * static_cast<float> (i);
        p.releaseSec = 3.5f + 0.5f * static_cast<float> (i);
        p.osc2DetuneCents = 5.0f + 3.0f * static_cast<float> (i);
        p.oscBlend = 0.25f + 0.12f * static_cast<float> (i);
        p.filterCutoffHz = 700.0f + 350.0f * static_cast<float> (i) - dirt * 280.0f;
        p.filterRes = 0.08f + dirt * 0.25f;
        p.satDrive = 0.1f + dirt * 0.55f;
        p.driftAmount = drift;
        p.driftVoiceOffsetCents = kVoiceDriftOffsets[static_cast<size_t> (i)] * (0.3f + drift);
        p.gate = i < activeCount;
        voices_[static_cast<size_t> (i)].setParams (p);
    }
}

void DroneOrganismProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    updateVoicesFromParams();
    outputSmooth_.setTargetValue (apvts_.getRawParameterValue ("output")->load());

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    auto* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float mix = 0.0f;
        for (auto& v : voices_)
            mix += v.processSample();

        const float g = outputSmooth_.getNextValue();
        float L = mix * g;
        float R = mix * g;

        L = dcLeft_.processSample (L);
        R = dcRight_.processSample (R);
        L = limiterLeft_.processSample (L);
        R = limiterRight_.processSample (R);

        left[i] = L;
        if (right != nullptr)
            right[i] = R;
    }
}

void DroneOrganismProcessor::renderOffline (juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer empty;
    processBlock (buffer, empty);
}

//==============================================================================
bool DroneOrganismProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* DroneOrganismProcessor::createEditor()
{
    return new DroneOrganismEditor (*this);
}

//==============================================================================
void DroneOrganismProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts_.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DroneOrganismProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts_.state.getType()))
            apvts_.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DroneOrganismProcessor();
}

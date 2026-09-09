#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr float kProofToneHz = 110.0f;   // A2
constexpr float kProofToneAmp = 0.05f;   // quiet, unmistakably present
}

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

    // Placeholders for 0.1 public surface — wired in later phases
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "seed", 1 },
        "Seed",
        0, 999999, 1001));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "density", 1 },
        "Density",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.45f));

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
double DroneOrganismProcessor::getTailLengthSeconds() const { return 0.0; }

int DroneOrganismProcessor::getNumPrograms() { return 1; }
int DroneOrganismProcessor::getCurrentProgram() { return 0; }
void DroneOrganismProcessor::setCurrentProgram (int) {}
const juce::String DroneOrganismProcessor::getProgramName (int) { return {}; }
void DroneOrganismProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void DroneOrganismProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;
    phase_ = 0.0;
    phaseDelta_ = juce::MathConstants<double>::twoPi * static_cast<double> (kProofToneHz) / sampleRate_;

    dcLeft_.prepare (sampleRate);
    dcRight_.prepare (sampleRate);
    limiterLeft_.prepare (sampleRate);
    limiterRight_.prepare (sampleRate);
}

void DroneOrganismProcessor::releaseResources() {}

bool DroneOrganismProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void DroneOrganismProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const float outputGain = apvts_.getRawParameterValue ("output")->load();
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    auto* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float sample = static_cast<float> (std::sin (phase_)) * kProofToneAmp * outputGain;
        phase_ += phaseDelta_;
        if (phase_ >= juce::MathConstants<double>::twoPi)
            phase_ -= juce::MathConstants<double>::twoPi;

        float L = sample;
        float R = sample;

        L = dcLeft_.processSample (L);
        R = dcRight_.processSample (R);
        L = limiterLeft_.processSample (L);
        R = limiterRight_.processSample (R);

        left[i] = L;
        if (right != nullptr)
            right[i] = R;
    }
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

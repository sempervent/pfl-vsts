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

    // Public audio-engine surface
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drift", 1 },
        "Drift",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.35f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dirt", 1 },
        "Dirt",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.45f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "space", 1 },
        "Space",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.55f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 },
        "Output",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f, 0.5f },
        0.65f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "density", 1 },
        "Density",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.7f));

    // Seed affects deterministic drift/noise streams (useful already)
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "seed", 1 },
        "Seed",
        0, 999999, 1001));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String DroneOrganismProcessor::getName() const { return JucePlugin_Name; }
bool DroneOrganismProcessor::acceptsMidi() const { return true; }
bool DroneOrganismProcessor::producesMidi() const { return false; }
bool DroneOrganismProcessor::isMidiEffect() const { return false; }
double DroneOrganismProcessor::getTailLengthSeconds() const { return 8.0; }

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

    dirtBus_.prepare (sampleRate);
    space_.prepare (sampleRate, 2.5f);
    dcLeft_.prepare (sampleRate);
    dcRight_.prepare (sampleRate);
    limiterLeft_.prepare (sampleRate);
    limiterRight_.prepare (sampleRate);

    outputSmooth_.prepare (sampleRate, 0.05f);
    outputSmooth_.setCurrentAndTarget (apvts_.getRawParameterValue ("output")->load());

    transportGate_.prepare (sampleRate, 0.02f);
    transportGate_.setTime (0.02f); // coeff updated per gate direction below
    transportGate_.setCurrentAndTarget (0.0f);

    lastSeed_ = -1;
    reseedDriftStreams();
    updateVoicesFromParams (false);
}

void DroneOrganismProcessor::releaseResources() {}

bool DroneOrganismProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DroneOrganismProcessor::reseedDriftStreams() noexcept
{
    const int seed = static_cast<int> (apvts_.getRawParameterValue ("seed")->load());
    if (seed == lastSeed_)
        return;
    lastSeed_ = seed;

    const auto master = static_cast<uint64_t> (seed) ^ 0x50464C01ull; // "PFL\1"
    for (int i = 0; i < kNumVoices; ++i)
    {
        auto rng = pfl::generative::DeterministicRNG::derived (master, 0x44524654ull + static_cast<uint64_t> (i)); // DRFT+i
        voices_[static_cast<size_t> (i)].setDriftRng (rng);
    }
    dirtBus_.setNoiseSeed (master);
}

bool DroneOrganismProcessor::isHostTransportPlaying() const noexcept
{
    if (offlineTransportPlaying_ && isNonRealtime())
        return true;

    // Standalone has no musical transport — keep sounding so the instrument remains playable.
    if (wrapperType == wrapperType_Standalone)
        return true;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
            return position->getIsPlaying();
    }

    // Unknown host playhead: fail safe to silent gate (Option B preference in DAW contexts)
    return false;
}

void DroneOrganismProcessor::updateVoicesFromParams (bool transportPlaying) noexcept
{
    const float density = apvts_.getRawParameterValue ("density")->load();
    const float drift = apvts_.getRawParameterValue ("drift")->load();

    const int activeCount = 1 + static_cast<int> (std::round (density * static_cast<float> (kNumVoices - 1)));

    for (int i = 0; i < kNumVoices; ++i)
    {
        pfl::dsp::VoiceParams p;
        p.baseMidiNote = kBaseNotes[static_cast<size_t> (i)];
        p.level = 0.28f;
        p.attackSec = 1.2f + 0.5f * static_cast<float> (i);
        p.releaseSec = 2.5f + 0.8f * static_cast<float> (i);
        p.osc2DetuneCents = 6.0f + 2.5f * static_cast<float> (i);
        p.oscBlend = 0.28f + 0.1f * static_cast<float> (i);
        p.pan = kPans[static_cast<size_t> (i)];
        p.driftAmount = drift;
        p.driftVoiceOffsetCents = kVoiceDriftOffsets[static_cast<size_t> (i)] * (0.35f + drift);
        p.gate = transportPlaying && (i < activeCount);
        voices_[static_cast<size_t> (i)].setParams (p);
    }
}

void DroneOrganismProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    reseedDriftStreams();

    const bool playing = isHostTransportPlaying();
    updateVoicesFromParams (playing);

    // Transport gate: slow attack/release overlay so enable/disable never clicks
    transportGate_.setTime (playing ? 0.8f : 2.5f);
    transportGate_.setTarget (playing ? 1.0f : 0.0f);

    dirtBus_.setDirt (apvts_.getRawParameterValue ("dirt")->load());
    space_.setSpace (apvts_.getRawParameterValue ("space")->load());
    outputSmooth_.setTarget (apvts_.getRawParameterValue ("output")->load());

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    auto* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float mixL = 0.0f;
        float mixR = 0.0f;
        for (auto& v : voices_)
        {
            float vl = 0.0f, vr = 0.0f;
            v.processSample (vl, vr);
            mixL += vl;
            mixR += vr;
        }

        float dirtL = 0.0f, dirtR = 0.0f;
        dirtBus_.processSample (mixL, mixR, dirtL, dirtR);

        float spaceL = 0.0f, spaceR = 0.0f;
        space_.processSample (dirtL, dirtR, spaceL, spaceR);

        const float gate = transportGate_.getNext();
        float L = spaceL * gate;
        float R = spaceR * gate;

        // Safety (musical dirt already applied) — never bypassed
        L = dcLeft_.processSample (L);
        R = dcRight_.processSample (R);
        L = limiterLeft_.processSample (L);
        R = limiterRight_.processSample (R);

        // OUTPUT after safety soft stage; hard ceiling remains inside limiter
        const float outG = outputSmooth_.getNext();
        L *= outG;
        R *= outG;
        L = std::clamp (L, -0.99f, 0.99f);
        R = std::clamp (R, -0.99f, 0.99f);

        left[i] = L;
        if (right != nullptr)
            right[i] = R;
        else
            left[i] = 0.5f * (L + R);
    }
}

void DroneOrganismProcessor::renderOffline (juce::AudioBuffer<float>& buffer)
{
    setNonRealtime (true);
    setOfflineTransportPlaying (true);
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

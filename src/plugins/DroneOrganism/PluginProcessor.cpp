#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
DroneOrganismProcessor::DroneOrganismProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", createParameterLayout())
{
    composer_.reseed (1001);
}

DroneOrganismProcessor::~DroneOrganismProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout DroneOrganismProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "seed", 1 }, "Seed", 0, 999999, 1001));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "density", 1 }, "Density",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.45f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mutation", 1 }, "Mutation",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.35f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drift", 1 }, "Drift",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.35f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dirt", 1 }, "Dirt",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.45f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "space", 1 }, "Space",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.55f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f, 0.5f }, 0.65f));

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
    transportGate_.setCurrentAndTarget (0.0f);

    lastSeedParam_ = -1;
    wasPlaying_ = false;
    lastHostPpq_ = 0.0;
    syncComposerFromParams();
    applyComposerToVoices (false);
}

void DroneOrganismProcessor::releaseResources() {}

bool DroneOrganismProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DroneOrganismProcessor::resetOfflineTimeline() noexcept
{
    offlinePpq_ = 0.0;
    wasPlaying_ = false;
    lastHostPpq_ = 0.0;
    lastSeedParam_ = -1;
    syncComposerFromParams();
    composer_.reseed (static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load()));
}

void DroneOrganismProcessor::syncComposerFromParams() noexcept
{
    pfl::generative::ComposerParams cp;
    cp.density = apvts_.getRawParameterValue ("density")->load();
    cp.mutation = apvts_.getRawParameterValue ("mutation")->load();
    composer_.setParams (cp);

    const int seed = static_cast<int> (apvts_.getRawParameterValue ("seed")->load());
    if (lastSeedParam_ < 0)
    {
        composer_.reseed (static_cast<uint64_t> (seed));
        lastSeedParam_ = seed;
        dirtBus_.setNoiseSeed (static_cast<uint64_t> (seed));
        for (int i = 0; i < kNumVoices; ++i)
            voices_[static_cast<size_t> (i)].setDriftRng (
                pfl::generative::DeterministicRNG::derived (static_cast<uint64_t> (seed),
                                                            0x44524654ull + static_cast<uint64_t> (i)));
    }
    else if (seed != lastSeedParam_)
    {
        composer_.requestReseed (static_cast<uint64_t> (seed));
        lastSeedParam_ = seed;
        dirtBus_.setNoiseSeed (static_cast<uint64_t> (seed));
        for (int i = 0; i < kNumVoices; ++i)
            voices_[static_cast<size_t> (i)].setDriftRng (
                pfl::generative::DeterministicRNG::derived (static_cast<uint64_t> (seed),
                                                            0x44524654ull + static_cast<uint64_t> (i)));
    }
}

bool DroneOrganismProcessor::readHostClock (pfl::generative::ClockSnapshot& snap, int numSamples) noexcept
{
    snap.timeSigNumerator = 4;
    snap.timeSigDenominator = 4;

    if (isNonRealtime() || wrapperType == wrapperType_Standalone)
    {
        snap.playing = offlineTransportPlaying_ || wrapperType == wrapperType_Standalone;
        snap.tempoBpm = offlineTempoBpm_;
        snap.ppq = offlinePpq_;
        if (snap.playing && sampleRate_ > 0.0)
            offlinePpq_ += (static_cast<double> (numSamples) / sampleRate_) * (offlineTempoBpm_ / 60.0);
        return true;
    }

    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            snap.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())
                snap.tempoBpm = *bpm;
            else
                snap.tempoBpm = 120.0;

            if (auto ppq = pos->getPpqPosition())
                snap.ppq = *ppq;
            else
                snap.ppq = lastHostPpq_;

            if (auto sig = pos->getTimeSignature())
            {
                snap.timeSigNumerator = sig->numerator;
                snap.timeSigDenominator = sig->denominator;
            }
            return true;
        }
    }

    snap.playing = false;
    snap.tempoBpm = 120.0;
    snap.ppq = lastHostPpq_;
    return false;
}

void DroneOrganismProcessor::applyComposerToVoices (bool transportPlaying) noexcept
{
    const float drift = apvts_.getRawParameterValue ("drift")->load();

    for (int i = 0; i < kNumVoices; ++i)
    {
        const auto& cv = composer_.voice (i);
        pfl::dsp::VoiceParams p;
        p.baseMidiNote = static_cast<float> (cv.pitch.midiNote);
        p.level = 0.28f;
        p.attackSec = 1.2f + 0.5f * static_cast<float> (i);
        p.releaseSec = 2.5f + 0.8f * static_cast<float> (i);
        p.osc2DetuneCents = 6.0f + 2.5f * static_cast<float> (i);
        p.oscBlend = 0.28f + 0.1f * static_cast<float> (i);
        p.pan = kPans[static_cast<size_t> (i)];
        p.driftAmount = drift;
        p.driftVoiceOffsetCents = kVoiceDriftOffsets[static_cast<size_t> (i)] * (0.35f + drift);
        p.gate = transportPlaying && cv.active;
        voices_[static_cast<size_t> (i)].setParams (p);
    }
}

void DroneOrganismProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    syncComposerFromParams();

    pfl::generative::ClockSnapshot snap;
    readHostClock (snap, buffer.getNumSamples());

    const double beatsPerSec = snap.tempoBpm / 60.0;
    const double blockBeats = sampleRate_ > 0.0
                                  ? (static_cast<double> (buffer.getNumSamples()) / sampleRate_) * beatsPerSec
                                  : 0.0;
    const double ppqStart = snap.ppq;
    const double ppqEnd = ppqStart + blockBeats;

    // Transport start from beginning → full deterministic restart
    if (snap.playing && ! wasPlaying_)
    {
        if (ppqStart <= 0.25)
        {
            const auto seed = static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load());
            composer_.reseed (seed);
        }
    }

    // Seek detection (non-contiguous jump while playing)
    if (snap.playing && wasPlaying_)
    {
        const double expected = lastHostPpq_;
        const double delta = ppqStart - expected;
        if (delta < -0.001 || delta > 2.0)
            composer_.handleSeek (ppqStart);
    }

    if (snap.playing)
    {
        composer_.clock().advance (snap);
        composer_.processTimeRange (ppqStart, ppqEnd, true);
    }

    wasPlaying_ = snap.playing;
    lastHostPpq_ = snap.playing ? ppqEnd : ppqStart;

    applyComposerToVoices (snap.playing);

    transportGate_.setTime (snap.playing ? 0.8f : 2.5f);
    transportGate_.setTarget (snap.playing ? 1.0f : 0.0f);
    dirtBus_.setDirt (apvts_.getRawParameterValue ("dirt")->load());
    space_.setSpace (apvts_.getRawParameterValue ("space")->load());
    outputSmooth_.setTarget (apvts_.getRawParameterValue ("output")->load());

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    auto* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float mixL = 0.0f, mixR = 0.0f;
        for (auto& v : voices_)
        {
            float a, b;
            v.processSample (a, b);
            mixL += a;
            mixR += b;
        }

        float dirtL, dirtR, spaceL, spaceR;
        dirtBus_.processSample (mixL, mixR, dirtL, dirtR);
        space_.processSample (dirtL, dirtR, spaceL, spaceR);

        const float gate = transportGate_.getNext();
        float L = spaceL * gate;
        float R = spaceR * gate;

        L = dcLeft_.processSample (L);
        R = dcRight_.processSample (R);
        L = limiterLeft_.processSample (L);
        R = limiterRight_.processSample (R);

        const float outG = outputSmooth_.getNext();
        L = std::clamp (L * outG, -0.99f, 0.99f);
        R = std::clamp (R * outG, -0.99f, 0.99f);

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

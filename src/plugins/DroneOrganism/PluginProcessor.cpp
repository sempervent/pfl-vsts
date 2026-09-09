#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
DroneOrganismProcessor::DroneOrganismProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", createParameterLayout())
{
    composer_.reseed (1001);
    performance_.reset (1001);
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

    // Performance — toggles
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "freeze", 1 }, "Freeze", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "silence", 1 }, "Silence", false));

    // Performance — momentary triggers (0→1 edge fires once; hold at 1 does not retrigger)
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
    lastFreezeParam_ = false;
    lastSilenceParam_ = false;
    lastMutateParam_ = 0.0f;
    lastCollapseParam_ = 0.0f;
    lastReseedParam_ = 0.0f;

    // Transient performance gestures reset safely on prepare
    syncComposerFromParams();
    performance_.reset (static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load()));
    composer_.setCompositionLocked (false);
    applyComposerToVoices (false, {});
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
    lastFreezeParam_ = false;
    lastSilenceParam_ = false;
    lastMutateParam_ = 0.0f;
    lastCollapseParam_ = 0.0f;
    lastReseedParam_ = 0.0f;
    syncComposerFromParams();
    const auto seed = static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load());
    composer_.reseed (seed);
    performance_.reset (seed);
}

void DroneOrganismProcessor::performanceTrigger (pfl::performance::Command cmd) noexcept
{
    performance_.trigger (cmd, lastHostPpq_, composer_);
    uint64_t newSeed = 0;
    if (performance_.takeSeedDirty (newSeed))
        applyPerformanceSeedToParams (newSeed);
}

void DroneOrganismProcessor::applyPerformanceSeedToParams (uint64_t seed) noexcept
{
    suppressingSeedSync_ = true;
    lastSeedParam_ = static_cast<int> (seed);
    if (auto* p = apvts_.getParameter ("seed"))
    {
        const float norm = p->convertTo0to1 (static_cast<float> (seed));
        p->setValueNotifyingHost (norm);
    }
    dirtBus_.setNoiseSeed (seed);
    for (int i = 0; i < kNumVoices; ++i)
        voices_[static_cast<size_t> (i)].setDriftRng (
            pfl::generative::DeterministicRNG::derived (seed, 0x44524654ull + static_cast<uint64_t> (i)));
    suppressingSeedSync_ = false;
}

void DroneOrganismProcessor::syncComposerFromParams() noexcept
{
    pfl::generative::ComposerParams cp;
    cp.density = apvts_.getRawParameterValue ("density")->load();
    cp.mutation = apvts_.getRawParameterValue ("mutation")->load();
    composer_.setParams (cp);

    if (suppressingSeedSync_)
        return;

    const int seed = static_cast<int> (apvts_.getRawParameterValue ("seed")->load());
    if (lastSeedParam_ < 0)
    {
        composer_.reseed (static_cast<uint64_t> (seed));
        performance_.reset (static_cast<uint64_t> (seed));
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
        performance_.reset (static_cast<uint64_t> (seed));
        lastSeedParam_ = seed;
        dirtBus_.setNoiseSeed (static_cast<uint64_t> (seed));
        for (int i = 0; i < kNumVoices; ++i)
            voices_[static_cast<size_t> (i)].setDriftRng (
                pfl::generative::DeterministicRNG::derived (static_cast<uint64_t> (seed),
                                                            0x44524654ull + static_cast<uint64_t> (i)));
    }
}

void DroneOrganismProcessor::pollPerformanceParams (double ppq) noexcept
{
    const bool freeze = apvts_.getRawParameterValue ("freeze")->load() > 0.5f;
    const bool silence = apvts_.getRawParameterValue ("silence")->load() > 0.5f;
    const float mutate = apvts_.getRawParameterValue ("mutate")->load();
    const float collapse = apvts_.getRawParameterValue ("collapse")->load();
    const float reseed = apvts_.getRawParameterValue ("reseed")->load();

    if (freeze && ! lastFreezeParam_)
        performance_.trigger (pfl::performance::Command::FreezeOn, ppq, composer_);
    else if (! freeze && lastFreezeParam_)
        performance_.trigger (pfl::performance::Command::FreezeOff, ppq, composer_);

    if (silence && ! lastSilenceParam_)
        performance_.trigger (pfl::performance::Command::SilenceOn, ppq, composer_);
    else if (! silence && lastSilenceParam_)
        performance_.trigger (pfl::performance::Command::SilenceOff, ppq, composer_);

    if (mutate >= 0.5f && lastMutateParam_ < 0.5f)
        performance_.trigger (pfl::performance::Command::Mutate, ppq, composer_);
    if (collapse >= 0.5f && lastCollapseParam_ < 0.5f)
        performance_.trigger (pfl::performance::Command::Collapse, ppq, composer_);
    if (reseed >= 0.5f && lastReseedParam_ < 0.5f)
    {
        const double beatsPerSec = offlineTempoBpm_ / 60.0;
        // Prefer ~1 bar fade at current tempo
        const int fadeSamples = static_cast<int> (sampleRate_ * (4.0 / std::max (1.0e-9, beatsPerSec)));
        performance_.armReseedFade (std::max (1, fadeSamples / 4)); // ~1 beat
        performance_.trigger (pfl::performance::Command::Reseed, ppq, composer_);
    }

    lastFreezeParam_ = freeze;
    lastSilenceParam_ = silence;
    lastMutateParam_ = mutate;
    lastCollapseParam_ = collapse;
    lastReseedParam_ = reseed;

    uint64_t newSeed = 0;
    if (performance_.takeSeedDirty (newSeed))
        applyPerformanceSeedToParams (newSeed);
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

void DroneOrganismProcessor::applyComposerToVoices (bool transportPlaying,
                                                    const pfl::performance::PerformanceOutputs& perf) noexcept
{
    float drift = apvts_.getRawParameterValue ("drift")->load();
    drift = std::clamp (drift + perf.driftBoost, 0.0f, 1.0f);

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

        bool active = cv.active;
        if (perf.maxActiveVoices >= 0 && i >= perf.maxActiveVoices)
            active = false;

        p.gate = transportPlaying && active;
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

    pollPerformanceParams (ppqStart);

    // Transport start from beginning → full deterministic restart
    if (snap.playing && ! wasPlaying_)
    {
        if (ppqStart <= 0.25)
        {
            const auto seed = static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load());
            composer_.reseed (seed);
            performance_.reset (seed);
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

        const double bpb = std::max (1.0e-9, composer_.clock().beatsPerBar());
        const int first = static_cast<int> (std::floor (ppqStart / bpb + 1.0e-9)) + 1;
        const int last = static_cast<int> (std::floor (ppqEnd / bpb + 1.0e-9));
        for (int b = first; b <= last; ++b)
            performance_.onBar (b, static_cast<double> (b) * bpb, composer_);

        performance_.syncComposerLock (composer_);

        // Density override during collapse (params for any unlocked path)
        if (lastPerfOut_.densityOverride >= 0.0f)
        {
            pfl::generative::ComposerParams cp = composer_.params();
            cp.density = lastPerfOut_.densityOverride;
            composer_.setParams (cp);
        }

        composer_.processTimeRange (ppqStart, ppqEnd, true);
    }

    wasPlaying_ = snap.playing;
    lastHostPpq_ = snap.playing ? ppqEnd : ppqStart;

    float dirt = apvts_.getRawParameterValue ("dirt")->load();
    float spaceAmt = apvts_.getRawParameterValue ("space")->load();
    dirt = std::clamp (dirt + lastPerfOut_.dirtBoost, 0.0f, 1.0f);
    spaceAmt = std::clamp (spaceAmt + lastPerfOut_.spaceBoost, 0.0f, 1.0f);

    applyComposerToVoices (snap.playing, lastPerfOut_);

    transportGate_.setTime (snap.playing ? 0.8f : 2.5f);
    transportGate_.setTarget (snap.playing ? 1.0f : 0.0f);
    dirtBus_.setDirt (dirt);
    space_.setSpace (spaceAmt);
    outputSmooth_.setTarget (apvts_.getRawParameterValue ("output")->load());

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    auto* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        pfl::performance::PerformanceOutputs perf;
        performance_.processSample (sampleRate_, perf);
        lastPerfOut_ = perf;
        performance_.syncComposerLock (composer_);

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
        const float fade = perf.silenceGain * perf.reseedFade;
        L = std::clamp (L * outG * fade, -0.99f, 0.99f);
        R = std::clamp (R * outG * fade, -0.99f, 0.99f);

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

    // Transient performance gestures do not restore mid-collapse/freeze
    lastFreezeParam_ = false;
    lastSilenceParam_ = false;
    lastMutateParam_ = 0.0f;
    lastCollapseParam_ = 0.0f;
    lastReseedParam_ = 0.0f;
    if (auto* freeze = apvts_.getParameter ("freeze"))
        freeze->setValueNotifyingHost (0.0f);
    if (auto* silence = apvts_.getParameter ("silence"))
        silence->setValueNotifyingHost (0.0f);
    if (auto* mutate = apvts_.getParameter ("mutate"))
        mutate->setValueNotifyingHost (0.0f);
    if (auto* collapse = apvts_.getParameter ("collapse"))
        collapse->setValueNotifyingHost (0.0f);
    if (auto* reseed = apvts_.getParameter ("reseed"))
        reseed->setValueNotifyingHost (0.0f);

    const auto seed = static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load());
    performance_.reset (seed);
    composer_.setCompositionLocked (false);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DroneOrganismProcessor();
}

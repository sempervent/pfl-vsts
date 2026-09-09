#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
BrokenConductorProcessor::BrokenConductorProcessor()
    : AudioProcessor (BusesProperties()), // MIDI effect: no audio buses
      apvts_ (*this, nullptr, "PARAMS", createParameterLayout())
{
    engine_.reseed (1001);
}

BrokenConductorProcessor::~BrokenConductorProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout BrokenConductorProcessor::createParameterLayout()
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

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String BrokenConductorProcessor::getName() const { return JucePlugin_Name; }
bool BrokenConductorProcessor::acceptsMidi() const { return true; }
bool BrokenConductorProcessor::producesMidi() const { return true; }
bool BrokenConductorProcessor::isMidiEffect() const { return true; }
double BrokenConductorProcessor::getTailLengthSeconds() const { return 0.0; }

int BrokenConductorProcessor::getNumPrograms() { return 1; }
int BrokenConductorProcessor::getCurrentProgram() { return 0; }
void BrokenConductorProcessor::setCurrentProgram (int) {}
const juce::String BrokenConductorProcessor::getProgramName (int) { return {}; }
void BrokenConductorProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void BrokenConductorProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;
    lastSeedParam_ = -1;
    wasPlaying_ = false;
    lastHostPpq_ = 0.0;
    syncEngineFromParams();
}

void BrokenConductorProcessor::releaseResources() {}

bool BrokenConductorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // MIDI effect: accept empty / no audio I/O
    juce::ignoreUnused (layouts);
    return true;
}

void BrokenConductorProcessor::resetOfflineTimeline() noexcept
{
    offlinePpq_ = 0.0;
    wasPlaying_ = false;
    lastHostPpq_ = 0.0;
    lastSeedParam_ = -1;
    syncEngineFromParams();
    engine_.reseed (static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load()));
}

void BrokenConductorProcessor::syncEngineFromParams() noexcept
{
    pfl::generative::ConductorParams cp;
    cp.density = apvts_.getRawParameterValue ("density")->load();
    cp.mutation = apvts_.getRawParameterValue ("mutation")->load();
    engine_.setParams (cp);

    const int seed = static_cast<int> (apvts_.getRawParameterValue ("seed")->load());
    if (lastSeedParam_ < 0)
    {
        engine_.reseed (static_cast<uint64_t> (seed));
        lastSeedParam_ = seed;
    }
    else if (seed != lastSeedParam_)
    {
        // Reseed applied in processBlock after panic when transport is live
        lastSeedParam_ = seed;
        pendingReseed_ = true;
    }
}

bool BrokenConductorProcessor::readHostClock (pfl::generative::ClockSnapshot& snap, int numSamples) noexcept
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

void BrokenConductorProcessor::flushHostNotes (juce::MidiBuffer& midi, int sampleOffset) noexcept
{
    // Explicit offs for owned notes + channel panic
    std::vector<pfl::generative::MidiTraceEvent> offs;
    // Copy active set via panic into temp without relying on engine.panic side effects alone
    engine_.panic (lastHostPpq_);
    const auto pending = engine_.drainPending();
    for (const auto& e : pending)
    {
        if (e.kind == pfl::generative::MidiMsgKind::NoteOff)
            midi.addEvent (juce::MidiMessage::noteOff (e.channel, e.note), sampleOffset);
    }
    midi.addEvent (juce::MidiMessage::allNotesOff (pfl::generative::ConductorEngine::kMidiChannel),
                   sampleOffset);
}

void BrokenConductorProcessor::panicMidi (juce::MidiBuffer& midi, int sampleOffset) noexcept
{
    flushHostNotes (midi, sampleOffset);
}

void BrokenConductorProcessor::emitMidi (juce::MidiBuffer& midi,
                                         const std::vector<pfl::generative::MidiTraceEvent>& events,
                                         double ppqStart,
                                         double beatsPerSec,
                                         int numSamples) noexcept
{
    if (beatsPerSec <= 1.0e-9 || sampleRate_ <= 0.0)
        return;

    for (const auto& e : events)
    {
        double offsetBeats = e.ppq - ppqStart;
        if (offsetBeats < 0.0)
            offsetBeats = 0.0;
        int sample = static_cast<int> (std::llround (offsetBeats / beatsPerSec * sampleRate_));
        sample = std::clamp (sample, 0, std::max (0, numSamples - 1));

        if (e.kind == pfl::generative::MidiMsgKind::NoteOn)
            midi.addEvent (juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity), sample);
        else if (e.kind == pfl::generative::MidiMsgKind::NoteOff)
            midi.addEvent (juce::MidiMessage::noteOff (e.channel, e.note), sample);
        else if (e.kind == pfl::generative::MidiMsgKind::AllNotesOff)
            midi.addEvent (juce::MidiMessage::allNotesOff (e.channel), sample);
    }
}

void BrokenConductorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midi.clear();

    syncEngineFromParams();

    pfl::generative::ClockSnapshot snap;
    readHostClock (snap, buffer.getNumSamples());

    const double beatsPerSec = snap.tempoBpm / 60.0;
    const double blockBeats = sampleRate_ > 0.0
                                  ? (static_cast<double> (buffer.getNumSamples()) / sampleRate_) * beatsPerSec
                                  : 0.0;
    const double ppqStart = snap.ppq;
    const double ppqEnd = ppqStart + blockBeats;

    if (pendingReseed_)
    {
        flushHostNotes (midi, 0);
        const auto seed = static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load());
        engine_.reseed (seed);
        pendingReseed_ = false;
    }

    if (snap.playing && ! wasPlaying_)
    {
        if (ppqStart <= 0.25)
        {
            const auto seed = static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load());
            engine_.reseed (seed);
        }
    }

    if (! snap.playing && wasPlaying_)
        flushHostNotes (midi, 0);

    if (snap.playing && wasPlaying_)
    {
        const double delta = ppqStart - lastHostPpq_;
        if (delta < -0.001 || delta > 2.0)
        {
            flushHostNotes (midi, 0);
            engine_.handleSeek (ppqStart);
            if (engine_.needsHostRetrigger())
            {
                midi.addEvent (juce::MidiMessage::noteOn (
                                   pfl::generative::ConductorEngine::kMidiChannel,
                                   engine_.soundingMidiNote(),
                                   (juce::uint8) std::clamp (engine_.lastVelocity(), 1, 127)),
                               0);
            }
        }
    }

    if (snap.playing)
    {
        engine_.clock().advance (snap);
        engine_.processTimeRange (ppqStart, ppqEnd, true);
        const auto pending = engine_.drainPending();
        emitMidi (midi, pending, ppqStart, beatsPerSec, buffer.getNumSamples());
    }

    wasPlaying_ = snap.playing;
    lastHostPpq_ = snap.playing ? ppqEnd : ppqStart;
}

//==============================================================================
bool BrokenConductorProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* BrokenConductorProcessor::createEditor()
{
    return new BrokenConductorEditor (*this);
}

//==============================================================================
void BrokenConductorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts_.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BrokenConductorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts_.state.getType()))
            apvts_.replaceState (juce::ValueTree::fromXml (*xml));

    lastSeedParam_ = -1;
    syncEngineFromParams();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BrokenConductorProcessor();
}

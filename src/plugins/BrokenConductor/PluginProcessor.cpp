#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
BrokenConductorProcessor::BrokenConductorProcessor()
    : AudioProcessor (BusesProperties()
                          // Ableton Live 11 rejects MIDI-out VST3s without a valid audio input bus
                          // (Log: "plugin has an effect category, but no valid audio input bus").
                          // Input is host-required shell only — ignored; output stays silent.
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", createParameterLayout())
{
    engine_.reseed (1001);
    performance_.reset (1001);
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

    // Configuration / routing — not a continuous performance control.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "outputRole", 1 }, "Output Role",
        juce::StringArray { "ENSEMBLE", "FOUNDATION", "PULSE", "WANDERER", "ACCENT" },
        0));

    // Performance toggles
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "freeze", 1 }, "Freeze", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "silence", 1 }, "Silence", false));

    // Performance one-shots (0→1 edge fires once)
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
const juce::String BrokenConductorProcessor::getName() const { return JucePlugin_Name; }
bool BrokenConductorProcessor::acceptsMidi() const { return true; }
bool BrokenConductorProcessor::producesMidi() const { return true; }
bool BrokenConductorProcessor::isMidiEffect() const { return false; }
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
    lastPerfBar_ = -1;
    // Seed edge latches from current APVTS so held one-shots do not false-fire
    lastFreezeParam_ = apvts_.getRawParameterValue ("freeze")->load() > 0.5f;
    lastSilenceParam_ = apvts_.getRawParameterValue ("silence")->load() > 0.5f;
    lastMutateParam_ = apvts_.getRawParameterValue ("mutate")->load();
    lastCollapseParam_ = apvts_.getRawParameterValue ("collapse")->load();
    lastReseedParam_ = apvts_.getRawParameterValue ("reseed")->load();
    syncEngineFromParams();
}

void BrokenConductorProcessor::releaseResources()
{
    // No MidiBuffer available here. Transport-stop / seek / reseed paths emit panic
    // MIDI; abrupt host unload may leave notes depending on Live lifecycle.
}

bool BrokenConductorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in.size() == out.size();
}

void BrokenConductorProcessor::resetOfflineTimeline() noexcept
{
    offlinePpq_ = 0.0;
    wasPlaying_ = false;
    lastHostPpq_ = 0.0;
    lastSeedParam_ = -1;
    lastPerfBar_ = -1;
    syncEngineFromParams();
    const auto seed = static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load());
    engine_.reseed (seed);
    performance_.reset (seed);
}

void BrokenConductorProcessor::writeSeedToHost (uint64_t seed) noexcept
{
    const int s = static_cast<int> (std::clamp (seed, 0ull, 999999ull));
    if (auto* p = apvts_.getParameter ("seed"))
    {
        const float norm = apvts_.getParameterRange ("seed").convertTo0to1 (static_cast<float> (s));
        p->setValueNotifyingHost (norm);
    }
    lastSeedParam_ = s;
    pendingReseed_ = false;
}

void BrokenConductorProcessor::syncEngineFromParams() noexcept
{
    pfl::generative::ConductorParams cp;
    cp.density = apvts_.getRawParameterValue ("density")->load();
    cp.mutation = apvts_.getRawParameterValue ("mutation")->load();
    engine_.setParams (cp);

    const int roleChoice = static_cast<int> (apvts_.getRawParameterValue ("outputRole")->load());
    const auto role = static_cast<pfl::generative::OutputRole> (
        std::clamp (roleChoice, 0, 4));
    engine_.setOutputRole (role);

    const int seed = static_cast<int> (apvts_.getRawParameterValue ("seed")->load());
    if (lastSeedParam_ < 0)
    {
        engine_.reseed (static_cast<uint64_t> (seed));
        performance_.reset (static_cast<uint64_t> (seed));
        lastSeedParam_ = seed;
    }
    else if (seed != lastSeedParam_)
    {
        lastSeedParam_ = seed;
        pendingReseed_ = true;
    }
}

void BrokenConductorProcessor::syncPerformanceCommands (double ppq) noexcept
{
    using Cmd = pfl::conductor_perf::Command;

    const bool freeze = apvts_.getRawParameterValue ("freeze")->load() > 0.5f;
    const bool silence = apvts_.getRawParameterValue ("silence")->load() > 0.5f;
    const float mutate = apvts_.getRawParameterValue ("mutate")->load();
    const float collapse = apvts_.getRawParameterValue ("collapse")->load();
    const float reseed = apvts_.getRawParameterValue ("reseed")->load();

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
    {
        writeSeedToHost (newSeed);
        // Re-apply latched toggles (RESEED does not clear host freeze/silence params)
        if (silence && performance_.mode() != pfl::conductor_perf::Mode::Silenced)
            performance_.trigger (Cmd::SilenceOn, ppq, engine_);
        else if (freeze && performance_.mode() == pfl::conductor_perf::Mode::Normal)
            performance_.trigger (Cmd::FreezeOn, ppq, engine_);
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

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

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
        performance_.reset (seed);
        pendingReseed_ = false;
    }

    if (snap.playing && ! wasPlaying_)
    {
        if (ppqStart <= 0.25)
        {
            const auto seed = static_cast<uint64_t> (apvts_.getRawParameterValue ("seed")->load());
            engine_.reseed (seed);
            performance_.reset (seed);
            // Clear sticky performance toggles on transport restart from zero
            if (auto* freeze = apvts_.getParameter ("freeze"))
                freeze->setValueNotifyingHost (0.0f);
            if (auto* silence = apvts_.getParameter ("silence"))
                silence->setValueNotifyingHost (0.0f);
            lastFreezeParam_ = false;
            lastSilenceParam_ = false;
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
            performance_.syncEngine (engine_);
            if (engine_.needsHostRetrigger() && ! engine_.silenceActive())
            {
                pfl::generative::ConductorEngine::HostSoundingNote notes[pfl::generative::ConductorEngine::kNumVoices];
                const int n = engine_.copySoundingNotes (notes, pfl::generative::ConductorEngine::kNumVoices);
                for (int i = 0; i < n; ++i)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (
                                       pfl::generative::ConductorEngine::kMidiChannel,
                                       notes[i].note,
                                       (juce::uint8) std::clamp (notes[i].velocity, 1, 127)),
                                   0);
                }
                engine_.markProjectedSoundingAsEmitted (ppqStart);
            }
        }
    }

    if (snap.playing)
    {
        syncPerformanceCommands (ppqStart);

        const int bar = static_cast<int> (std::floor (ppqStart / 4.0));
        if (bar != lastPerfBar_)
        {
            performance_.tick (ppqStart, bar, engine_);
            lastPerfBar_ = bar;
        }
        else
        {
            performance_.tick (ppqStart, bar, engine_);
        }

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
    lastFreezeParam_ = apvts_.getRawParameterValue ("freeze")->load() > 0.5f;
    lastSilenceParam_ = apvts_.getRawParameterValue ("silence")->load() > 0.5f;
    lastMutateParam_ = apvts_.getRawParameterValue ("mutate")->load();
    lastCollapseParam_ = apvts_.getRawParameterValue ("collapse")->load();
    lastReseedParam_ = apvts_.getRawParameterValue ("reseed")->load();
    syncEngineFromParams();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BrokenConductorProcessor();
}

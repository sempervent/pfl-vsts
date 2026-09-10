#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>

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

void PulseColonyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    engine_.prepare (sampleRate_, samplesPerBlock);
    silenceSm_.prepare (sampleRate_, 0.004f); // ~4 ms click-safe ramp
    silenceSm_.setCurrentAndTarget (1.0f);
    monoScratch_.assign (static_cast<size_t> (std::max (1, samplesPerBlock)), 0.0f);
    syncEngineFromParams();
    engine_.snapMacros();
    engine_.forceRebuild (0);
    restorePerformanceFromParams();
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

void PulseColonyProcessor::writeSeedToHost (uint64_t seed) noexcept
{
    const int s = static_cast<int> (std::clamp (seed, 0ull, 999999ull));
    if (auto* p = apvts_.getParameter ("seed"))
    {
        const float norm = apvts_.getParameterRange ("seed").convertTo0to1 (static_cast<float> (s));
        p->setValueNotifyingHost (norm);
    }
    lastSeedParam_ = s;
}

void PulseColonyProcessor::restorePerformanceFromParams() noexcept
{
    const auto seed = static_cast<uint64_t> (juce::jlimit (
        0, 999999, static_cast<int> (readParam (apvts_, "seed", 2002.0f))));
    performance_.reset (seed == 0 ? 1ull : seed);

    lastFreezeParam_ = readParam (apvts_, "freeze", 0.0f) > 0.5f;
    lastSilenceParam_ = readParam (apvts_, "silence", 0.0f) > 0.5f;
    lastMutateParam_ = readParam (apvts_, "mutate", 0.0f);
    lastCollapseParam_ = readParam (apvts_, "collapse", 0.0f);
    lastReseedParam_ = readParam (apvts_, "reseed", 0.0f);

    // Restore toggles only — mid-collapse is never journaled active.
    if (lastFreezeParam_)
        performance_.trigger (pfl::pulse_perf::Command::FreezeOn, 0.0, engine_);
    if (lastSilenceParam_)
        performance_.trigger (pfl::pulse_perf::Command::SilenceOn, 0.0, engine_);
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

void PulseColonyProcessor::syncPerformanceCommands (double ppq) noexcept
{
    using Cmd = pfl::pulse_perf::Command;

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

void PulseColonyProcessor::applySilenceGain (float* L, float* R, int n) noexcept
{
    const bool silenced = performance_.mode() == pfl::pulse_perf::Mode::Silenced;
    silenceSm_.setTarget (silenced ? 0.0f : 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const float g = silenceSm_.getNext();
        L[i] *= g;
        if (R != nullptr)
            R[i] *= g;
    }
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
            applySilenceGain (L + offset, nullptr, chunk);
            offset += chunk;
        }
    }
    else
    {
        engine_.process (L, R, n, playing, ppq, bpm);
        applySilenceGain (L, R, n);
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
    root.setProperty ("stateVersion", 2, nullptr);
    root.setProperty ("algorithmVersion", pfl::dsp::PulseColonyEngine::kAlgorithmVersion, nullptr);
    root.setProperty ("performanceEngineVersion",
                      pfl::pulse_perf::kPerformanceEngineVersion, nullptr);
    root.setProperty ("freeze", performance_.state().freezeLatched, nullptr);
    root.setProperty ("silence", performance_.mode() == pfl::pulse_perf::Mode::Silenced, nullptr);
    // Mid-collapse → persist stable mode only (Collapsed/Frozen/Normal), not active arc.
    const auto mode = performance_.mode();
    const int modePersist =
        (mode == pfl::pulse_perf::Mode::Collapsing) ? static_cast<int> (pfl::pulse_perf::Mode::Collapsed)
                                                    : static_cast<int> (mode);
    root.setProperty ("perfMode", modePersist, nullptr);
    root.setProperty ("residueRole", performance_.state().residueRole, nullptr);

    if (performance_.dnaEdited()
        || mode == pfl::pulse_perf::Mode::Collapsed
        || mode == pfl::pulse_perf::Mode::Collapsing
        || mode == pfl::pulse_perf::Mode::Frozen)
    {
        std::array<pfl::dsp::PulseColonyEngine::CompactCellDna,
                   pfl::dsp::PulseColonyEngine::kNumRoles> compact {};
        engine_.exportCompactDna (compact);
        juce::ValueTree dnaTree ("CompactDna");
        for (int r = 0; r < pfl::dsp::PulseColonyEngine::kNumRoles; ++r)
        {
            juce::ValueTree cell ("Cell");
            cell.setProperty ("role", r, nullptr);
            cell.setProperty ("lengthBars", compact[static_cast<size_t> (r)].lengthBars, nullptr);
            cell.setProperty ("phaseShiftSlots", compact[static_cast<size_t> (r)].phaseShiftSlots, nullptr);
            cell.setProperty ("generation", compact[static_cast<size_t> (r)].generation, nullptr);
            cell.setProperty ("birthBar", compact[static_cast<size_t> (r)].birthBar, nullptr);
            cell.setProperty ("lifespanBars", compact[static_cast<size_t> (r)].lifespanBars, nullptr);
            cell.setProperty ("lastOp", static_cast<int> (compact[static_cast<size_t> (r)].lastOp), nullptr);
            cell.setProperty ("windowCount", compact[static_cast<size_t> (r)].windowCount, nullptr);
            juce::MemoryBlock winBlob (compact[static_cast<size_t> (r)].windows.data(),
                                       sizeof (compact[static_cast<size_t> (r)].windows));
            cell.setProperty ("windows", winBlob.toBase64Encoding(), nullptr);
            dnaTree.appendChild (cell, nullptr);
        }
        root.appendChild (dnaTree, nullptr);
    }

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
            restorePerformanceFromParams();
            return;
        }
        if (tree.hasType ("PFLPulseColonyState"))
        {
            if (auto params = tree.getChildWithName (apvts_.state.getType()); params.isValid())
                apvts_.replaceState (params);
            syncEngineFromParams();
            engine_.snapMacros();
            engine_.forceRebuild (0);

            if (auto dnaTree = tree.getChildWithName ("CompactDna"); dnaTree.isValid())
            {
                std::array<pfl::dsp::PulseColonyEngine::CompactCellDna,
                           pfl::dsp::PulseColonyEngine::kNumRoles> compact {};
                for (int i = 0; i < dnaTree.getNumChildren(); ++i)
                {
                    auto cell = dnaTree.getChild (i);
                    const int r = static_cast<int> (cell.getProperty ("role", -1));
                    if (r < 0 || r >= pfl::dsp::PulseColonyEngine::kNumRoles)
                        continue;
                    auto& c = compact[static_cast<size_t> (r)];
                    c.lengthBars = cell.getProperty ("lengthBars", 2);
                    c.phaseShiftSlots = cell.getProperty ("phaseShiftSlots", 0);
                    c.generation = cell.getProperty ("generation", 0);
                    c.birthBar = cell.getProperty ("birthBar", 0);
                    c.lifespanBars = cell.getProperty ("lifespanBars", 6);
                    c.lastOp = static_cast<uint32_t> (static_cast<int> (cell.getProperty ("lastOp", 0)));
                    c.windowCount = cell.getProperty ("windowCount", 0);
                    juce::MemoryBlock winBlob;
                    winBlob.fromBase64Encoding (cell.getProperty ("windows").toString());
                    if (winBlob.getSize() >= sizeof (c.windows))
                        std::memcpy (c.windows.data(), winBlob.getData(), sizeof (c.windows));
                }
                engine_.importCompactDna (compact, 0);
            }

            restorePerformanceFromParams();
            // Mid-collapse restore → stable Collapsed/Frozen residue (never resume arc).
            const int residue = tree.getProperty ("residueRole", -1);
            const bool freezeLatch = tree.getProperty ("freeze", false)
                                     || (readParam (apvts_, "freeze", 0.0f) > 0.5f);
            if (residue >= 0 || freezeLatch)
                performance_.restoreStableMode (residue, freezeLatch, engine_);
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

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "generative/ConductorEngine.h"
#include "performance/ConductorPerformanceController.h"

//==============================================================================
class BrokenConductorProcessor final : public juce::AudioProcessor
{
public:
    BrokenConductorProcessor();
    ~BrokenConductorProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    pfl::generative::ConductorEngine& engine() noexcept { return engine_; }
    pfl::conductor_perf::ConductorPerformanceController& performance() noexcept { return performance_; }

    void setOfflineTransportPlaying (bool playing) noexcept { offlineTransportPlaying_ = playing; }
    void setOfflineTempoBpm (double bpm) noexcept { offlineTempoBpm_ = bpm; }
    void setOfflinePpq (double ppq) noexcept { offlinePpq_ = ppq; }
    void resetOfflineTimeline() noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void syncEngineFromParams() noexcept;
    void syncPerformanceCommands (double ppq) noexcept;
    bool readHostClock (pfl::generative::ClockSnapshot& snap, int numSamples) noexcept;
    void emitMidi (juce::MidiBuffer& midi, const std::vector<pfl::generative::MidiTraceEvent>& events,
                   double ppqStart, double beatsPerSec, int numSamples) noexcept;
    void panicMidi (juce::MidiBuffer& midi, int sampleOffset) noexcept;
    void flushHostNotes (juce::MidiBuffer& midi, int sampleOffset) noexcept;
    void writeSeedToHost (uint64_t seed) noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    pfl::generative::ConductorEngine engine_;
    pfl::conductor_perf::ConductorPerformanceController performance_;

    double sampleRate_ = 44100.0;
    int lastSeedParam_ = -1;
    bool pendingReseed_ = false;
    bool offlineTransportPlaying_ = true;
    double offlineTempoBpm_ = 72.0;
    double offlinePpq_ = 0.0;
    bool wasPlaying_ = false;
    double lastHostPpq_ = 0.0;
    int lastPerfBar_ = -1;

    bool lastFreezeParam_ = false;
    bool lastSilenceParam_ = false;
    float lastMutateParam_ = 0.0f;
    float lastCollapseParam_ = 0.0f;
    float lastReseedParam_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrokenConductorProcessor)
};

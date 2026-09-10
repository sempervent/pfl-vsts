#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/MemoryEaterEngine.h"
#include "performance/MemoryEaterPerformanceController.h"

#include <vector>

class MemoryEaterProcessor final : public juce::AudioProcessor
{
public:
    MemoryEaterProcessor();
    ~MemoryEaterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    pfl::dsp::MemoryEaterEngine& engine() noexcept { return engine_; }
    pfl::memory_perf::MemoryEaterPerformanceController& performance() noexcept { return performance_; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void syncEngineFromParams() noexcept;
    void syncPerformanceCommands (double ppq) noexcept;
    void writeSeedToHost (uint64_t seed) noexcept;
    void restorePerformanceFromParams() noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    pfl::dsp::MemoryEaterEngine engine_;
    pfl::memory_perf::MemoryEaterPerformanceController performance_;
    double sampleRate_ = 44100.0;
    double lastPpq_ = 0.0;
    int lastSeedParam_ = -1;
    bool lastFreezeParam_ = false;
    bool lastSilenceParam_ = false;
    float lastMutateParam_ = 0.0f;
    float lastCollapseParam_ = 0.0f;
    float lastReseedParam_ = 0.0f;
    std::vector<float> monoScratch_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryEaterProcessor)
};

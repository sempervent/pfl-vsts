#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/DCBlocker.h"
#include "dsp/SafetyLimiter.h"

//==============================================================================
class DroneOrganismProcessor final : public juce::AudioProcessor
{
public:
    DroneOrganismProcessor();
    ~DroneOrganismProcessor() override;

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts_;

    // Phase 0 proof-of-life oscillator (replaced by voice engine in Phase 1)
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    double phaseDelta_ = 0.0;

    pfl::dsp::DCBlocker dcLeft_;
    pfl::dsp::DCBlocker dcRight_;
    pfl::dsp::SafetyLimiter limiterLeft_;
    pfl::dsp::SafetyLimiter limiterRight_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DroneOrganismProcessor)
};

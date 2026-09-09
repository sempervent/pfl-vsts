#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/DCBlocker.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/Voice.h"

#include <array>

//==============================================================================
class DroneOrganismProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kNumVoices = 4;

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

    /** Offline / test render helper — advances the same DSP path as the plugin. */
    void renderOffline (juce::AudioBuffer<float>& buffer);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateVoicesFromParams() noexcept;

    juce::AudioProcessorValueTreeState apvts_;

    double sampleRate_ = 44100.0;
    std::array<pfl::dsp::Voice, kNumVoices> voices_;

    // D minor pentatonic chord-ish stack (MIDI): D2, A2, F3, C4
    static constexpr std::array<float, kNumVoices> kBaseNotes { 38.0f, 45.0f, 53.0f, 60.0f };
    static constexpr std::array<float, kNumVoices> kVoiceDriftOffsets { -4.0f, 2.5f, -1.5f, 3.0f };

    pfl::dsp::DCBlocker dcLeft_;
    pfl::dsp::DCBlocker dcRight_;
    pfl::dsp::SafetyLimiter limiterLeft_;
    pfl::dsp::SafetyLimiter limiterRight_;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputSmooth_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DroneOrganismProcessor)
};

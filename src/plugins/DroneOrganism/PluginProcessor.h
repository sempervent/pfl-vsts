#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/DCBlocker.h"
#include "dsp/DirtBus.h"
#include "dsp/FeedbackDelay.h"
#include "dsp/ParamSmoother.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/Voice.h"
#include "generative/DeterministicRNG.h"

#include <array>

//==============================================================================
class DroneOrganismProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kNumVoices = 3;

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

    /** Offline / test: force transport-playing gate (Option B). */
    void setOfflineTransportPlaying (bool playing) noexcept { offlineTransportPlaying_ = playing; }

    void renderOffline (juce::AudioBuffer<float>& buffer);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateVoicesFromParams (bool transportPlaying) noexcept;
    void reseedDriftStreams() noexcept;
    bool isHostTransportPlaying() const noexcept;

    juce::AudioProcessorValueTreeState apvts_;

    double sampleRate_ = 44100.0;
    int lastSeed_ = -1;
    bool offlineTransportPlaying_ = true;

    std::array<pfl::dsp::Voice, kNumVoices> voices_;

    // Fixed drone voicing: D2, A2, D3
    static constexpr std::array<float, kNumVoices> kBaseNotes { 38.0f, 45.0f, 50.0f };
    static constexpr std::array<float, kNumVoices> kVoiceDriftOffsets { -3.5f, 2.0f, 1.5f };
    static constexpr std::array<float, kNumVoices> kPans { -0.28f, 0.05f, 0.32f };

    pfl::dsp::DirtBus dirtBus_;
    pfl::dsp::FeedbackDelay space_;
    pfl::dsp::DCBlocker dcLeft_;
    pfl::dsp::DCBlocker dcRight_;
    pfl::dsp::SafetyLimiter limiterLeft_;
    pfl::dsp::SafetyLimiter limiterRight_;

    pfl::dsp::ParamSmoother outputSmooth_;
    pfl::dsp::ParamSmoother transportGate_; // Option B fade

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DroneOrganismProcessor)
};

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/DCBlocker.h"
#include "dsp/DirtBus.h"
#include "dsp/FeedbackDelay.h"
#include "dsp/ParamSmoother.h"
#include "dsp/SafetyLimiter.h"
#include "dsp/Voice.h"
#include "generative/Composer.h"

#include <array>

//==============================================================================
class DroneOrganismProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kNumVoices = pfl::generative::Composer::kMaxVoices;

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
    pfl::generative::Composer& composer() noexcept { return composer_; }

    void setOfflineTransportPlaying (bool playing) noexcept { offlineTransportPlaying_ = playing; }
    void setOfflineTempoBpm (double bpm) noexcept { offlineTempoBpm_ = bpm; }
    void setOfflinePpq (double ppq) noexcept { offlinePpq_ = ppq; }
    void resetOfflineTimeline() noexcept;

    void renderOffline (juce::AudioBuffer<float>& buffer);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void syncComposerFromParams() noexcept;
    void applyComposerToVoices (bool transportPlaying) noexcept;
    bool readHostClock (pfl::generative::ClockSnapshot& snap, int numSamples) noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    pfl::generative::Composer composer_;

    double sampleRate_ = 44100.0;
    int lastSeedParam_ = -1;
    bool offlineTransportPlaying_ = true;
    double offlineTempoBpm_ = 72.0;
    double offlinePpq_ = 0.0;
    bool wasPlaying_ = false;
    double lastHostPpq_ = 0.0;

    std::array<pfl::dsp::Voice, kNumVoices> voices_;
    static constexpr std::array<float, kNumVoices> kVoiceDriftOffsets { -3.5f, 2.0f, 1.5f };
    static constexpr std::array<float, kNumVoices> kPans { -0.28f, 0.05f, 0.32f };

    pfl::dsp::DirtBus dirtBus_;
    pfl::dsp::FeedbackDelay space_;
    pfl::dsp::DCBlocker dcLeft_;
    pfl::dsp::DCBlocker dcRight_;
    pfl::dsp::SafetyLimiter limiterLeft_;
    pfl::dsp::SafetyLimiter limiterRight_;
    pfl::dsp::ParamSmoother outputSmooth_;
    pfl::dsp::ParamSmoother transportGate_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DroneOrganismProcessor)
};

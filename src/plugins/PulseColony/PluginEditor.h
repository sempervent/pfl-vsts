#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class PulseColonyProcessor;

class PulseColonyEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PulseColonyEditor (PulseColonyProcessor&);
    ~PulseColonyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PulseColonyProcessor& processor_;
    juce::GenericAudioProcessorEditor genericEditor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulseColonyEditor)
};

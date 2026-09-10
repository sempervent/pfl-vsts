#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class SignalParasiteProcessor;

class SignalParasiteEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SignalParasiteEditor (SignalParasiteProcessor&);
    ~SignalParasiteEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SignalParasiteProcessor& processor_;
    juce::GenericAudioProcessorEditor genericEditor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalParasiteEditor)
};

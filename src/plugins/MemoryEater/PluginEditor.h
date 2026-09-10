#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MemoryEaterProcessor;

class MemoryEaterEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MemoryEaterEditor (MemoryEaterProcessor&);
    ~MemoryEaterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MemoryEaterProcessor& processor_;
    juce::GenericAudioProcessorEditor genericEditor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryEaterEditor)
};

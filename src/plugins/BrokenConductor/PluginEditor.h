#pragma once

#include "PluginProcessor.h"

//==============================================================================
class BrokenConductorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BrokenConductorEditor (BrokenConductorProcessor&);
    ~BrokenConductorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::GenericAudioProcessorEditor genericEditor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrokenConductorEditor)
};

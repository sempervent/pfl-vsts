#pragma once

#include "PluginProcessor.h"

//==============================================================================
class RuinEngineEditor final : public juce::AudioProcessorEditor
{
public:
    explicit RuinEngineEditor (RuinEngineProcessor&);
    ~RuinEngineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RuinEngineProcessor& processor_;
    juce::GenericAudioProcessorEditor genericEditor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RuinEngineEditor)
};

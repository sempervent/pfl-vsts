#pragma once

#include "PluginProcessor.h"

//==============================================================================
class DroneOrganismEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DroneOrganismEditor (DroneOrganismProcessor&);
    ~DroneOrganismEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::GenericAudioProcessorEditor genericEditor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DroneOrganismEditor)
};

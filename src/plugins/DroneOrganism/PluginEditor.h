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
    DroneOrganismProcessor& processor_;
    juce::GenericAudioProcessorEditor genericEditor_;

    juce::TextButton freezeBtn_ { "FREEZE" };
    juce::TextButton mutateBtn_ { "MUTATE" };
    juce::TextButton collapseBtn_ { "COLLAPSE" };
    juce::TextButton reseedBtn_ { "RESEED" };
    juce::TextButton silenceBtn_ { "SILENCE" };

    bool freezeLatched_ = false;
    bool silenceLatched_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DroneOrganismEditor)
};

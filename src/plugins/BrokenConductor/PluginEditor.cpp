#include "PluginEditor.h"

BrokenConductorEditor::BrokenConductorEditor (BrokenConductorProcessor& p)
    : AudioProcessorEditor (&p),
      genericEditor_ (p)
{
    addAndMakeVisible (genericEditor_);
    setSize (400, 200);
}

BrokenConductorEditor::~BrokenConductorEditor() = default;

void BrokenConductorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::grey);
    g.setFont (12.0f);
    g.drawText ("PFL Broken Conductor — Stage 1", getLocalBounds().removeFromBottom (22).reduced (8, 2),
                juce::Justification::centredLeft);
}

void BrokenConductorEditor::resized()
{
    genericEditor_.setBounds (getLocalBounds());
}

#include "PluginEditor.h"

DroneOrganismEditor::DroneOrganismEditor (DroneOrganismProcessor& p)
    : AudioProcessorEditor (&p),
      genericEditor_ (p)
{
    addAndMakeVisible (genericEditor_);
    setSize (400, 320);
}

DroneOrganismEditor::~DroneOrganismEditor() = default;

void DroneOrganismEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void DroneOrganismEditor::resized()
{
    genericEditor_.setBounds (getLocalBounds());
}

#include "PluginEditor.h"
#include "PluginProcessor.h"

PulseColonyEditor::PulseColonyEditor (PulseColonyProcessor& p)
    : AudioProcessorEditor (&p),
      processor_ (p),
      genericEditor_ (p)
{
    juce::ignoreUnused (processor_);
    addAndMakeVisible (genericEditor_);
    setSize (400, 320);
}

PulseColonyEditor::~PulseColonyEditor() = default;

void PulseColonyEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::grey);
    g.setFont (12.0f);
    g.drawText ("PFL Pulse Colony — Stage 3",
                getLocalBounds().removeFromBottom (24).reduced (8, 2),
                juce::Justification::centredLeft);
}

void PulseColonyEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (24);
    genericEditor_.setBounds (area);
}

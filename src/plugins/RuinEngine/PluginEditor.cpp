#include "PluginEditor.h"

RuinEngineEditor::RuinEngineEditor (RuinEngineProcessor& p)
    : AudioProcessorEditor (&p),
      processor_ (p),
      genericEditor_ (p)
{
    juce::ignoreUnused (processor_);
    addAndMakeVisible (genericEditor_);
    setSize (420, 520);
}

RuinEngineEditor::~RuinEngineEditor() = default;

void RuinEngineEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::grey);
    g.setFont (12.0f);
    g.drawText ("PFL Ruin Engine — Stage 4",
                getLocalBounds().removeFromBottom (24).reduced (8, 2),
                juce::Justification::centredLeft);
}

void RuinEngineEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (24);
    genericEditor_.setBounds (area);
}

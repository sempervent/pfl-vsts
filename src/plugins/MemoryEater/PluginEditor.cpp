#include "PluginEditor.h"
#include "PluginProcessor.h"

MemoryEaterEditor::MemoryEaterEditor (MemoryEaterProcessor& p)
    : AudioProcessorEditor (&p),
      processor_ (p),
      genericEditor_ (p)
{
    juce::ignoreUnused (processor_);
    addAndMakeVisible (genericEditor_);
    setSize (400, 280);
}

MemoryEaterEditor::~MemoryEaterEditor() = default;

void MemoryEaterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::grey);
    g.setFont (12.0f);
    g.drawText ("PFL Memory Eater — Stage 2",
                getLocalBounds().removeFromBottom (24).reduced (8, 2),
                juce::Justification::centredLeft);
}

void MemoryEaterEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (24);
    genericEditor_.setBounds (area);
}

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "plugins/PflGenericEditorSizing.h"

MemoryEaterEditor::MemoryEaterEditor (MemoryEaterProcessor& p)
    : AudioProcessorEditor (&p),
      processor_ (p),
      genericEditor_ (p)
{
    juce::ignoreUnused (processor_);
    addAndMakeVisible (genericEditor_);
    const auto b = pfl::ui::preferredGenericEditorBounds (p, 24);
    setSize (b.getWidth(), b.getHeight());
}

MemoryEaterEditor::~MemoryEaterEditor() = default;

void MemoryEaterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::grey);
    g.setFont (12.0f);
    g.drawText (pfl::ui::pluginFooterLabel ("PFL Memory Eater", "Stage 4"),
                getLocalBounds().removeFromBottom (24).reduced (8, 2),
                juce::Justification::centredLeft);
}

void MemoryEaterEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (24);
    genericEditor_.setBounds (area);
}

#include "PluginEditor.h"
#include "plugins/PflGenericEditorSizing.h"

DroneOrganismEditor::DroneOrganismEditor (DroneOrganismProcessor& p)
    : AudioProcessorEditor (&p),
      processor_ (p),
      genericEditor_ (p)
{
    addAndMakeVisible (genericEditor_);

    auto styleBtn = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colours::darkgrey);
        b.setColour (juce::TextButton::textColourOffId, juce::Colours::whitesmoke);
    };

    for (auto* b : { &freezeBtn_, &mutateBtn_, &collapseBtn_, &reseedBtn_, &silenceBtn_ })
    {
        styleBtn (*b);
        addAndMakeVisible (*b);
    }

    freezeBtn_.onClick = [this]
    {
        freezeLatched_ = ! freezeLatched_;
        if (auto* param = processor_.getAPVTS().getParameter ("freeze"))
            param->setValueNotifyingHost (freezeLatched_ ? 1.0f : 0.0f);
        freezeBtn_.setButtonText (freezeLatched_ ? "FREEZE*" : "FREEZE");
    };

    silenceBtn_.onClick = [this]
    {
        silenceLatched_ = ! silenceLatched_;
        if (auto* param = processor_.getAPVTS().getParameter ("silence"))
            param->setValueNotifyingHost (silenceLatched_ ? 1.0f : 0.0f);
        silenceBtn_.setButtonText (silenceLatched_ ? "SILENCE*" : "SILENCE");
    };

    mutateBtn_.onClick = [this]
    {
        processor_.performanceTrigger (pfl::performance::Command::Mutate);
    };

    collapseBtn_.onClick = [this]
    {
        processor_.performanceTrigger (pfl::performance::Command::Collapse);
    };

    reseedBtn_.onClick = [this]
    {
        const double bpm = 72.0;
        const int fadeSamples = static_cast<int> (48000.0 * (60.0 / bpm)); // ~1 beat @48k/72
        processor_.performance().armReseedFade (std::max (1, fadeSamples));
        processor_.performanceTrigger (pfl::performance::Command::Reseed);
    };

    // Extra chrome: performance button row (56) + "Performance" label strip (28).
    const auto b = pfl::ui::preferredGenericEditorBounds (p, 28, 56);
    setSize (b.getWidth(), b.getHeight());
}

DroneOrganismEditor::~DroneOrganismEditor() = default;

void DroneOrganismEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::grey);
    g.setFont (12.0f);
    g.drawText ("Performance", getLocalBounds().removeFromBottom (28).reduced (8, 4),
                juce::Justification::centredLeft);
}

void DroneOrganismEditor::resized()
{
    auto area = getLocalBounds();
    auto perf = area.removeFromBottom (56).reduced (6);
    genericEditor_.setBounds (area);

    const int w = perf.getWidth() / 5;
    freezeBtn_.setBounds (perf.removeFromLeft (w).reduced (2));
    mutateBtn_.setBounds (perf.removeFromLeft (w).reduced (2));
    collapseBtn_.setBounds (perf.removeFromLeft (w).reduced (2));
    reseedBtn_.setBounds (perf.removeFromLeft (w).reduced (2));
    silenceBtn_.setBounds (perf.reduced (2));
}

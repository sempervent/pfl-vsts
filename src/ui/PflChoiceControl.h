#pragma once

#include "ui/PflTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pfl::ui
{

/** Discrete configuration choice. Only Broken Conductor's `outputRole` uses this. */
class PflChoiceControl final : public juce::Component
{
public:
    PflChoiceControl (juce::AudioProcessorValueTreeState& state,
                      const juce::String& paramId,
                      const juce::String& displayName,
                      const Theme& theme)
    {
        name_.setText (displayName, juce::dontSendNotification);
        name_.setJustificationType (juce::Justification::centredLeft);
        name_.setColour (juce::Label::textColourId, theme.textDim);
        name_.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
        addAndMakeVisible (name_);

        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            box_.addItemList (param->choices, 1);

        box_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (box_);

        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            state, paramId, box_);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        name_.setBounds (area.removeFromTop (15));
        box_.setBounds (area.reduced (0, 1));
    }

private:
    juce::Label name_;
    juce::ComboBox box_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PflChoiceControl)
};

} // namespace pfl::ui

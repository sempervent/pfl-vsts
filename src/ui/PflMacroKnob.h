#pragma once

#include "ui/PflTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pfl::ui
{

/** Continuous macro: name above, rotary in the middle, numeric read-out below. */
class PflMacroKnob final : public juce::Component
{
public:
    PflMacroKnob (juce::AudioProcessorValueTreeState& state,
                  const juce::String& paramId,
                  const juce::String& displayName,
                  const Theme& theme,
                  const juce::String& tooltip = {})
    {
        name_.setText (displayName, juce::dontSendNotification);
        name_.setJustificationType (juce::Justification::centred);
        name_.setColour (juce::Label::textColourId, theme.textDim);
        name_.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
        addAndMakeVisible (name_);

        slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider_.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                     juce::MathConstants<float>::pi * 2.8f, true);
        if (tooltip.isNotEmpty())
            slider_.setTooltip (tooltip);
        addAndMakeVisible (slider_);

        value_.setJustificationType (juce::Justification::centred);
        value_.setColour (juce::Label::textColourId, theme.text);
        value_.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (value_);

        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, paramId, slider_);

        slider_.onValueChange = [this] { refreshValueText(); };
        refreshValueText();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        name_.setBounds (area.removeFromTop (15));
        value_.setBounds (area.removeFromBottom (16));
        slider_.setBounds (area.reduced (4, 2));
    }

    juce::Slider& slider() noexcept { return slider_; }

private:
    void refreshValueText()
    {
        const auto v = slider_.getValue();
        const auto interval = slider_.getInterval();
        const bool integral = interval >= 1.0 - 1.0e-9;
        value_.setText (integral ? juce::String (juce::roundToInt (v))
                                 : juce::String (v, 2),
                        juce::dontSendNotification);
    }

    juce::Label name_;
    juce::Slider slider_;
    juce::Label value_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PflMacroKnob)
};

} // namespace pfl::ui

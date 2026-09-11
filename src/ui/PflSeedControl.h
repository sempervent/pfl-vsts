#pragma once

#include "ui/PflTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pfl::ui
{

/** Integer SEED. Deliberately shaped as a horizontal value field so it never
    reads as another performance verb next to RESEED.
*/
class PflSeedControl final : public juce::Component
{
public:
    PflSeedControl (juce::AudioProcessorValueTreeState& state,
                    const juce::String& paramId,
                    const Theme& theme)
        : theme_ (theme)
    {
        name_.setText ("SEED", juce::dontSendNotification);
        name_.setJustificationType (juce::Justification::centredLeft);
        name_.setColour (juce::Label::textColourId, theme.textDim);
        name_.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
        addAndMakeVisible (name_);

        slider_.setSliderStyle (juce::Slider::LinearHorizontal);
        slider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider_.setTooltip ("Deterministic seed. Same seed reproduces the same performance.");
        addAndMakeVisible (slider_);

        value_.setJustificationType (juce::Justification::centredRight);
        value_.setColour (juce::Label::textColourId, theme.text);
        value_.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
        addAndMakeVisible (value_);

        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, paramId, slider_);

        slider_.onValueChange = [this] { refreshValueText(); };
        refreshValueText();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme_.panel.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (theme_.outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8, 6);
        name_.setBounds (area.removeFromLeft (44));
        value_.setBounds (area.removeFromRight (68));
        slider_.setBounds (area.reduced (8, 0));
    }

private:
    void refreshValueText()
    {
        value_.setText (juce::String (juce::roundToInt (slider_.getValue())),
                        juce::dontSendNotification);
    }

    Theme theme_;
    juce::Label name_;
    juce::Slider slider_;
    juce::Label value_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PflSeedControl)
};

} // namespace pfl::ui

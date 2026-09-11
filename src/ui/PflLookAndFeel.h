#pragma once

#include "ui/PflTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace pfl::ui
{

class PflLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit PflLookAndFeel (Theme theme) : theme_ (std::move (theme))
    {
        setColour (juce::ResizableWindow::backgroundColourId, theme_.background);
        setColour (juce::Label::textColourId, theme_.text);
        setColour (juce::Slider::rotarySliderFillColourId, theme_.accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, theme_.track);
        setColour (juce::Slider::thumbColourId, theme_.text);
        setColour (juce::Slider::trackColourId, theme_.accent);
        setColour (juce::Slider::backgroundColourId, theme_.track);
        setColour (juce::TextButton::buttonColourId, theme_.panel);
        setColour (juce::TextButton::buttonOnColourId, theme_.accentDim);
        setColour (juce::TextButton::textColourOffId, theme_.text);
        setColour (juce::TextButton::textColourOnId, theme_.text);
        setColour (juce::ComboBox::backgroundColourId, theme_.panel);
        setColour (juce::ComboBox::outlineColourId, theme_.outline);
        setColour (juce::ComboBox::textColourId, theme_.text);
        setColour (juce::ComboBox::arrowColourId, theme_.accent);
        setColour (juce::PopupMenu::backgroundColourId, theme_.panel);
        setColour (juce::PopupMenu::textColourId, theme_.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme_.accentDim);
    }

    const Theme& theme() const noexcept { return theme_; }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        juce::ignoreUnused (slider);
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                                .reduced (6.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        g.setColour (theme_.track);
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        g.setColour (theme_.accent);
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                rotaryStartAngle, toAngle, true);
        g.strokePath (valueArc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        juce::Path pointer;
        const float pointerLen = radius * 0.72f;
        pointer.startNewSubPath (centre.x, centre.y);
        pointer.lineTo (centre.x + pointerLen * std::sin (toAngle),
                        centre.y - pointerLen * std::cos (toAngle));
        g.setColour (theme_.text);
        g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        g.fillEllipse (centre.x - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        auto fill = backgroundColour;
        if (shouldDrawButtonAsDown)
            fill = fill.brighter (0.15f);
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter (0.08f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 4.0f);

        const bool on = button.getToggleState();
        g.setColour (on ? theme_.accent : theme_.outline);
        g.drawRoundedRectangle (bounds, 4.0f, on ? 2.0f : 1.0f);

        if (on)
        {
            g.setColour (theme_.accent);
            g.fillRect (bounds.getX() + 4.0f, bounds.getY() + 4.0f, 6.0f, 6.0f);
        }
    }

private:
    Theme theme_;
};

} // namespace pfl::ui

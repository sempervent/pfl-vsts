#pragma once

#include "ui/PflLayout.h"
#include "ui/PflTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pfl::ui
{

/** Momentary verb button.

    The processors fire on a rising edge of a 0..1 float parameter, so a press
    writes 0 then 1 (guaranteeing an edge from any prior value) and re-arms by
    returning to 0 shortly after — well beyond one audio block.
*/
class PflEdgeButton final : public juce::TextButton,
                            private juce::Timer
{
public:
    PflEdgeButton (juce::RangedAudioParameter* param, const juce::String& text)
        : juce::TextButton (text), param_ (param)
    {
        onClick = [this] { fire(); };
    }

    ~PflEdgeButton() override { stopTimer(); }

private:
    void fire()
    {
        if (param_ == nullptr)
            return;

        param_->beginChangeGesture();
        param_->setValueNotifyingHost (0.0f);
        param_->setValueNotifyingHost (1.0f);
        param_->endChangeGesture();

        startTimer (kReleaseMs);
    }

    void timerCallback() override
    {
        stopTimer();

        if (param_ != nullptr)
        {
            param_->beginChangeGesture();
            param_->setValueNotifyingHost (0.0f);
            param_->endChangeGesture();
        }
    }

    static constexpr int kReleaseMs = 160;

    juce::RangedAudioParameter* param_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PflEdgeButton)
};

//==============================================================================
/** FREEZE / SILENCE latches plus MUTATE / COLLAPSE / RESEED one-shots. */
class PflPerfStrip final : public juce::Component
{
public:
    PflPerfStrip (juce::AudioProcessorValueTreeState& state, const Theme& theme)
        : theme_ (theme)
    {
        header_.setText ("PERFORMANCE", juce::dontSendNotification);
        header_.setJustificationType (juce::Justification::centredLeft);
        header_.setColour (juce::Label::textColourId, theme.textDim);
        header_.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
        addAndMakeVisible (header_);

        setUpLatch (freeze_, freezeAttachment_, state, "freeze", "FREEZE",
                    "Hold the current material.");
        setUpLatch (silence_, silenceAttachment_, state, "silence", "SILENCE",
                    "Stop output without losing state.");

        // SILENCE is the panic control: give it its own border weight so it is
        // findable at a glance and never confused with FREEZE.
        silence_.setColour (juce::TextButton::buttonColourId, theme.panel.brighter (0.14f));

        mutate_ = std::make_unique<PflEdgeButton> (
            dynamic_cast<juce::RangedAudioParameter*> (state.getParameter ("mutate")), "MUTATE");
        collapse_ = std::make_unique<PflEdgeButton> (
            dynamic_cast<juce::RangedAudioParameter*> (state.getParameter ("collapse")), "COLLAPSE");
        reseed_ = std::make_unique<PflEdgeButton> (
            dynamic_cast<juce::RangedAudioParameter*> (state.getParameter ("reseed")), "RESEED");

        mutate_->setTooltip ("Force one mutation now.");
        collapse_->setTooltip ("Collapse toward sparseness, then recover.");
        reseed_->setTooltip ("Jump to a new seed with a short fade.");

        // COLLAPSE is the most destructive verb of the three.
        collapse_->setColour (juce::TextButton::buttonColourId,
                              theme.strong.withAlpha (0.24f).overlaidWith (theme.panel.withAlpha (0.6f)));

        for (auto* b : { mutate_.get(), collapse_.get(), reseed_.get() })
            addAndMakeVisible (*b);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme_.panel.withAlpha (0.45f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (theme_.outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8, Layout::perfPadV);
        header_.setBounds (area.removeFromTop (Layout::perfHeaderH));
        area.removeFromTop (Layout::perfHeaderGap);

        const int rowH = (area.getHeight() - kRowGap) / 2;

        auto row1 = area.removeFromTop (rowH);
        const int halfW = row1.getWidth() / 2;
        freeze_.setBounds (row1.removeFromLeft (halfW).reduced (2, 1));
        silence_.setBounds (row1.reduced (2, 1));

        area.removeFromTop (kRowGap);
        auto row2 = area;
        const int thirdW = row2.getWidth() / 3;
        mutate_->setBounds (row2.removeFromLeft (thirdW).reduced (2, 1));
        collapse_->setBounds (row2.removeFromLeft (thirdW).reduced (2, 1));
        reseed_->setBounds (row2.reduced (2, 1));
    }

    /** Height needed for header + two button rows. */
    static constexpr int preferredHeight() { return Layout::perfH; }

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setUpLatch (juce::TextButton& button,
                     std::unique_ptr<ButtonAttachment>& attachment,
                     juce::AudioProcessorValueTreeState& state,
                     const juce::String& paramId,
                     const juce::String& baseText,
                     const juce::String& tooltip)
    {
        button.setClickingTogglesState (true);
        button.setButtonText (baseText);
        button.setTooltip (tooltip);
        addAndMakeVisible (button);

        // Text carries the state too, so the latch does not rely on colour.
        button.onStateChange = [&button, baseText]
        {
            const auto wanted = button.getToggleState() ? baseText + " *" : baseText;
            if (button.getButtonText() != wanted)
                button.setButtonText (wanted);
        };

        attachment = std::make_unique<ButtonAttachment> (state, paramId, button);
    }

    static constexpr int kRowGap = Layout::perfRowGap;

    Theme theme_;
    juce::Label header_;
    juce::TextButton freeze_;
    juce::TextButton silence_;
    std::unique_ptr<ButtonAttachment> freezeAttachment_;
    std::unique_ptr<ButtonAttachment> silenceAttachment_;
    std::unique_ptr<PflEdgeButton> mutate_;
    std::unique_ptr<PflEdgeButton> collapse_;
    std::unique_ptr<PflEdgeButton> reseed_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PflPerfStrip)
};

} // namespace pfl::ui

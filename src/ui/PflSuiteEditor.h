#pragma once

#include "ui/PflChoiceControl.h"
#include "ui/PflLayout.h"
#include "ui/PflLookAndFeel.h"
#include "ui/PflMacroKnob.h"
#include "ui/PflPerfStrip.h"
#include "ui/PflSeedControl.h"
#include "ui/PflTheme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace pfl::ui
{

struct KnobSpec
{
    const char* paramId = nullptr;
    const char* label = nullptr;
    const char* tooltip = nullptr;
};

/** Shared fixed-size PFL editor shell used by all six suite plugins. */
class PflSuiteEditor : public juce::AudioProcessorEditor
{
public:
    PflSuiteEditor (juce::AudioProcessor& proc,
                    juce::AudioProcessorValueTreeState& apvts,
                    ProductId product,
                    juce::String title,
                    juce::String subtitle,
                    std::initializer_list<KnobSpec> knobs,
                    bool hasSeed = true,
                    bool hasPerf = true,
                    const char* choiceParamId = nullptr,
                    const char* choiceLabel = nullptr)
        : AudioProcessorEditor (&proc),
          theme_ (Theme::forProduct (product)),
          lnf_ (theme_),
          title_ (std::move (title)),
          subtitle_ (std::move (subtitle)),
          product_ (product)
    {
        setLookAndFeel (&lnf_);

        for (const auto& k : knobs)
        {
            auto knob = std::make_unique<PflMacroKnob> (
                apvts,
                k.paramId,
                k.label,
                theme_,
                k.tooltip != nullptr ? juce::String (k.tooltip) : juce::String());
            addAndMakeVisible (*knob);
            knobs_.push_back (std::move (knob));
        }

        if (choiceParamId != nullptr && choiceLabel != nullptr)
        {
            choice_ = std::make_unique<PflChoiceControl> (apvts, choiceParamId, choiceLabel, theme_);
            addAndMakeVisible (*choice_);
        }

        if (hasSeed)
        {
            seed_ = std::make_unique<PflSeedControl> (apvts, "seed", theme_);
            addAndMakeVisible (*seed_);
        }

        if (hasPerf)
        {
            perf_ = std::make_unique<PflPerfStrip> (apvts, theme_);
            addAndMakeVisible (*perf_);
        }

        const auto size = preferredFixedSize ((int) knobs_.size(),
                                              seed_ != nullptr,
                                              perf_ != nullptr,
                                              choice_ != nullptr);
        setResizable (false, false);
        setSize (size.x, size.y);

        // Lets tests and hosts read the product name without a stage number.
        setName (title_);
    }

    ~PflSuiteEditor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme_.background);
        theme_.paintMotif (g, getLocalBounds().toFloat());

        auto header = getLocalBounds().removeFromTop (Layout::headerH).reduced (14, 8);
        g.setColour (theme_.text);
        g.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
        g.drawText (title_, header.removeFromTop (24), juce::Justification::centredLeft);

        g.setColour (theme_.textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (subtitle_, header, juce::Justification::centredLeft);

        g.setColour (theme_.textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("PFL",
                    getLocalBounds().removeFromBottom (Layout::footerH).reduced (14, 2),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        area.removeFromTop (Layout::headerH);
        area.removeFromBottom (Layout::footerH);

        if (perf_ != nullptr)
            perf_->setBounds (area.removeFromBottom (Layout::perfH).reduced (0, 2));

        if (seed_ != nullptr)
            seed_->setBounds (area.removeFromBottom (Layout::seedH).reduced (0, 2));

        if (choice_ != nullptr)
            choice_->setBounds (area.removeFromBottom (Layout::choiceH).reduced (0, 2));

        const int n = (int) knobs_.size();
        if (n == 0)
            return;

        const int cols = juce::jmin (3, n);
        const int rows = (n + cols - 1) / cols;
        const int cellW = area.getWidth() / cols;
        const int cellH = area.getHeight() / juce::jmax (1, rows);

        for (int i = 0; i < n; ++i)
        {
            const int row = i / cols;
            const int col = i % cols;
            knobs_[(size_t) i]->setBounds (area.getX() + col * cellW,
                                           area.getY() + row * cellH,
                                           cellW,
                                           cellH);
        }
    }

    ProductId productId() const noexcept { return product_; }
    const juce::String& productTitle() const noexcept { return title_; }

private:
    Theme theme_;
    PflLookAndFeel lnf_;
    juce::String title_;
    juce::String subtitle_;
    ProductId product_;
    std::vector<std::unique_ptr<PflMacroKnob>> knobs_;
    std::unique_ptr<PflChoiceControl> choice_;
    std::unique_ptr<PflSeedControl> seed_;
    std::unique_ptr<PflPerfStrip> perf_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PflSuiteEditor)
};

} // namespace pfl::ui

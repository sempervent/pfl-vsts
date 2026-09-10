#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pfl::ui
{

/** Preferred size for a PFL wrapper around juce::GenericAudioProcessorEditor.
 *
 *  JUCE's generic editor uses 40 px TreeView rows and clamps its *initial*
 *  height to 400. Hosts still scroll whenever our wrapper is shorter than
 *  (paramCount * 40). Size the outer window from the automatable parameter
 *  count so ordinary desktop opens show every control without scrolling.
 */
inline juce::Rectangle<int> preferredGenericEditorBounds (juce::AudioProcessor& processor,
                                                          int footerHeight = 24,
                                                          int extraBottomChrome = 0,
                                                          int width = 420) noexcept
{
    int n = 0;
    for (auto* p : processor.getParameters())
        if (p != nullptr && p->isAutomatable())
            ++n;

    constexpr int kRowH = 40; // juce::ParamControlItem::getItemHeight()
    constexpr int kTopPad = 8;
    const int contentH = kTopPad + n * kRowH;
    // Leave a little slack so the TreeView viewport is never the tight clip.
    const int height = contentH + footerHeight + extraBottomChrome + 12;
    return { 0, 0, width, juce::jmax (height, 200) };
}

/** ASCII-safe footer label. Avoid UTF-8 em dashes that hosts mis-decode as mojibake. */
inline juce::String pluginFooterLabel (const juce::String& product, const juce::String& stage) noexcept
{
    return product + " - " + stage;
}

} // namespace pfl::ui

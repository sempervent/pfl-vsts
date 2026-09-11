#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pfl::ui
{

/** Legacy helper retained for documentation/tests of the previous generic-editor era.
 *  Custom PflSuiteEditor no longer sizes from generic TreeView rows.
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

    constexpr int kRowH = 40;
    constexpr int kTopPad = 8;
    const int contentH = kTopPad + n * kRowH;
    const int height = contentH + footerHeight + extraBottomChrome + 12;
    return { 0, 0, width, juce::jmax (height, 200) };
}

/** ASCII-safe product footer. Stage numbers must not appear in musician-facing UI. */
inline juce::String pluginFooterLabel (const juce::String& product,
                                       const juce::String& /*stageIgnored*/ = {}) noexcept
{
    return product;
}

} // namespace pfl::ui

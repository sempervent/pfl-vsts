#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pfl::ui
{

/** Fixed metrics shared by every PFL editor.

    Editors are not resizable and never scroll: the window is sized from the
    control inventory so every control is on screen at its natural size.
*/
struct Layout
{
    static constexpr int width = 580;
    static constexpr int headerH = 52;
    static constexpr int footerH = 22;

    static constexpr int knobsPerRow = 3;
    static constexpr int knobCellH = 118;

    static constexpr int choiceH = 48;
    static constexpr int seedH = 40;

    static constexpr int perfPadV = 6;
    static constexpr int perfHeaderH = 13;
    static constexpr int perfHeaderGap = 3;
    static constexpr int perfRowH = 30;
    static constexpr int perfRowGap = 5;
    static constexpr int perfH = perfPadV + perfHeaderH + perfHeaderGap
                                 + 2 * perfRowH + perfRowGap + perfPadV;

    static constexpr int sectionGap = 8;
    static constexpr int outerPad = 20;
};

inline int knobRowCount (int numKnobs) noexcept
{
    return numKnobs <= 0 ? 1 : (numKnobs + Layout::knobsPerRow - 1) / Layout::knobsPerRow;
}

/** Window size for a given control inventory. Width is constant across the
    suite; height grows with knob rows and whichever sections are present.
*/
inline juce::Point<int> preferredFixedSize (int numKnobs,
                                            bool hasSeed,
                                            bool hasPerf,
                                            bool hasChoice) noexcept
{
    int h = Layout::headerH + knobRowCount (numKnobs) * Layout::knobCellH;

    if (hasChoice)
        h += Layout::choiceH;
    if (hasSeed)
        h += Layout::seedH;
    if (hasPerf)
        h += Layout::perfH + Layout::sectionGap;

    h += Layout::footerH + Layout::outerPad;

    return { Layout::width, h };
}

} // namespace pfl::ui

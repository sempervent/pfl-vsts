// Runtime companion to ui_editor_tests.cpp.
//
// ui_editor_tests.cpp guards source-level invariants (no generic editor, no
// stage text). This one actually builds a processor, asks it for an editor, and
// checks the window a musician would see: sane fixed bounds, real child
// controls, and a title free of stage numbers. Editor geometry for the whole
// suite is checked through the shared layout helper, which is header-only and
// therefore does not need every plugin linked in.

#include "plugins/DroneOrganism/PluginProcessor.h"
#include "ui/PflLayout.h"

#include <juce_events/juce_events.h>

#include <iostream>

static int gFails = 0;
#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << #cond << "\n"; \
            ++gFails; \
        } \
    } while (0)

namespace
{

constexpr int kMinWidth = 500;
constexpr int kMaxWidth = 700;
constexpr int kMinHeight = 280;
constexpr int kMaxHeight = 700;

int countDescendants (const juce::Component& c)
{
    int n = c.getNumChildComponents();
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* child = c.getChildComponent (i))
            n += countDescendants (*child);
    return n;
}

struct SuiteEditorSize
{
    const char* product;
    int knobs;
    bool hasSeed;
    bool hasPerf;
    bool hasChoice;
};

// Mirrors the control inventory each PluginEditor.cpp declares.
const SuiteEditorSize kSuite[]
{
    { "PFL Drone Organism",   6, true, true, false },
    { "PFL Broken Conductor", 2, true, true, true },
    { "PFL Ruin Engine",      4, true, true, false },
    { "PFL Memory Eater",     4, true, true, false },
    { "PFL Pulse Colony",     5, true, true, false },
    { "PFL Signal Parasite",  5, true, true, false },
};

void checkSuiteGeometry()
{
    for (const auto& entry : kSuite)
    {
        const auto size = pfl::ui::preferredFixedSize (entry.knobs, entry.hasSeed,
                                                       entry.hasPerf, entry.hasChoice);

        std::cout << entry.product << ": " << size.x << " x " << size.y << "\n";

        EXPECT (size.x >= kMinWidth && size.x <= kMaxWidth);
        EXPECT (size.y >= kMinHeight && size.y <= kMaxHeight);
    }

    // Every editor in the suite shares one width.
    for (const auto& entry : kSuite)
        EXPECT (pfl::ui::preferredFixedSize (entry.knobs, entry.hasSeed, entry.hasPerf,
                                             entry.hasChoice).x
                == pfl::ui::preferredFixedSize (kSuite[0].knobs, kSuite[0].hasSeed,
                                                kSuite[0].hasPerf, kSuite[0].hasChoice).x);

    // More controls must never produce a shorter window.
    EXPECT (pfl::ui::preferredFixedSize (6, true, true, false).y
            > pfl::ui::preferredFixedSize (2, true, true, false).y);
}

void checkLiveEditor()
{
    DroneOrganismProcessor processor;

    EXPECT (processor.hasEditor());
    EXPECT (! processor.getName().contains ("Stage"));

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    EXPECT (editor != nullptr);

    if (editor == nullptr)
        return;

    const auto w = editor->getWidth();
    const auto h = editor->getHeight();
    std::cout << "live editor: " << w << " x " << h << "\n";

    EXPECT (w > 0 && h > 0);
    EXPECT (w >= kMinWidth && w <= kMaxWidth);
    EXPECT (h >= kMinHeight && h <= kMaxHeight);

    // Fixed size: the shell must not hand the host a resizer.
    EXPECT (! editor->isResizable());

    // Name is the product title, with no stage number anywhere in it.
    EXPECT (editor->getName().isNotEmpty());
    EXPECT (! editor->getName().contains ("Stage"));
    EXPECT (! editor->getName().containsIgnoreCase ("stage"));

    // Real controls, not an empty shell.
    EXPECT (countDescendants (*editor) > 5);

    // paint() must survive a headless offscreen render and actually draw.
    juce::Image snapshot (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (snapshot);
        editor->paintEntireComponent (g, true);
    }

    bool drewSomething = false;
    for (int y = 0; y < h && ! drewSomething; y += 4)
        for (int x = 0; x < w; x += 4)
            if (snapshot.getPixelAt (x, y).getAlpha() != 0)
            {
                drewSomething = true;
                break;
            }
    EXPECT (drewSomething);

#if defined (PFL_SOURCE_ROOT)
    // Review artifact (not a committed binary requirement): Drone Organism shell.
    {
        juce::File dir (juce::String (PFL_SOURCE_ROOT) + "/analysis/ui");
        dir.createDirectory();
        juce::File png = dir.getChildFile ("drone-organism.png");
        if (auto out = png.createOutputStream())
        {
            juce::PNGImageFormat fmt;
            EXPECT (fmt.writeImageToStream (snapshot, *out));
            std::cout << "wrote " << png.getFullPathName() << "\n";
        }
    }
#endif
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    checkSuiteGeometry();
    checkLiveEditor();

    if (gFails == 0)
    {
        std::cout << "ui_editor_runtime_tests: OK\n";
        return 0;
    }

    std::cerr << "ui_editor_runtime_tests: " << gFails << " failure(s)\n";
    return 1;
}

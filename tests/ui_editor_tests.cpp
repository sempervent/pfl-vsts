#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef PFL_SOURCE_ROOT
#error "PFL_SOURCE_ROOT must be defined"
#endif

static int gFails = 0;
#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << #cond << "\n"; \
            ++gFails; \
        } \
    } while (0)

static std::string readFile (const std::string& path)
{
    std::ifstream in (path);
    EXPECT (in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void expectNoStageInEditor (const std::string& relPath)
{
    const std::string src = readFile (std::string (PFL_SOURCE_ROOT) + "/" + relPath);
    EXPECT (src.find ("Stage ") == std::string::npos);
    EXPECT (src.find ("GenericAudioProcessorEditor") == std::string::npos);
    EXPECT (src.find ("PflSuiteEditor") != std::string::npos
            || src.find ("ui/PflSuiteEditor.h") != std::string::npos);
}

int main()
{
    const char* editors[] = {
        "src/plugins/DroneOrganism/PluginEditor.h",
        "src/plugins/BrokenConductor/PluginEditor.h",
        "src/plugins/RuinEngine/PluginEditor.h",
        "src/plugins/MemoryEater/PluginEditor.h",
        "src/plugins/PulseColony/PluginEditor.h",
        "src/plugins/SignalParasite/PluginEditor.h",
    };

    for (auto* path : editors)
        expectNoStageInEditor (path);

    EXPECT (readFile (std::string (PFL_SOURCE_ROOT) + "/src/ui/PflSuiteEditor.h").find ("PflSuiteEditor")
            != std::string::npos);
    EXPECT (readFile (std::string (PFL_SOURCE_ROOT) + "/src/ui/PflMacroKnob.h").find ("RotaryHorizontalVerticalDrag")
            != std::string::npos);
    EXPECT (readFile (std::string (PFL_SOURCE_ROOT) + "/src/ui/PflPerfStrip.h").find ("FREEZE")
            != std::string::npos);
    EXPECT (readFile (std::string (PFL_SOURCE_ROOT) + "/src/ui/PflSeedControl.h").find ("SEED")
            != std::string::npos);

    const auto sizing = readFile (std::string (PFL_SOURCE_ROOT) + "/src/plugins/PflGenericEditorSizing.h");
    EXPECT (sizing.find ("product + \" - \" + stage") == std::string::npos);

    const auto layout = readFile (std::string (PFL_SOURCE_ROOT) + "/src/ui/PflLayout.h");
    EXPECT (layout.find ("width = 580") != std::string::npos);
    const auto suite = readFile (std::string (PFL_SOURCE_ROOT) + "/src/ui/PflSuiteEditor.h");
    EXPECT (suite.find ("setResizable (false, false)") != std::string::npos);

    if (gFails == 0)
    {
        std::cout << "ui_editor_tests: OK\n";
        return 0;
    }
    std::cerr << "ui_editor_tests: " << gFails << " failure(s)\n";
    return 1;
}

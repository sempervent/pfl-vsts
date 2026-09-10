#include "generative/ConductorEngine.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef PFL_SOURCE_ROOT
#error "PFL_SOURCE_ROOT must be defined (tests/CMakeLists.txt)"
#endif

// Regression: multi-plugin VST3 / AU / bundle identities must stay unique.
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

struct PluginId
{
    const char* name;
    const char* bundleId;
    const char* pluginCode; // 4-char
    const char* manufacturerCode;
};

// Keep in sync with src/plugins/*/CMakeLists.txt and docs/PLUGIN_IDENTITIES.md
static const PluginId kPlugins[] = {
    { "PFL Drone Organism", "com.pfl.droneorganism", "Dro1", "PflG" },
    { "PFL Broken Conductor", "com.pfl.brokenconductor", "Brk1", "PflG" },
    { "PFL Ruin Engine", "com.pfl.ruinengine", "Rui1", "PflG" },
};

int main()
{
    const int n = (int) (sizeof kPlugins / sizeof kPlugins[0]);
    for (int i = 0; i < n; ++i)
    {
        EXPECT (std::string (kPlugins[i].pluginCode).size() == 4);
        EXPECT (std::string (kPlugins[i].manufacturerCode).size() == 4);
        EXPECT (std::string (kPlugins[i].bundleId).find ("com.pfl.") == 0);
        for (int j = i + 1; j < n; ++j)
        {
            EXPECT (std::string (kPlugins[i].bundleId) != kPlugins[j].bundleId);
            EXPECT (std::string (kPlugins[i].pluginCode) != kPlugins[j].pluginCode);
        }
    }

    // Documented Ableton requirement: BC declares MIDI out (engine constant channel).
    EXPECT (pfl::generative::ConductorEngine::kMidiChannel == 1);

    // Stage 1C: Live rejects MIDI-out VST3 without audio input bus — source must keep both.
    {
        const std::string bcProc = std::string (PFL_SOURCE_ROOT)
            + "/src/plugins/BrokenConductor/PluginProcessor.cpp";
        const std::string src = readFile (bcProc);
        EXPECT (src.find (".withInput") != std::string::npos);
        EXPECT (src.find (".withOutput") != std::string::npos);
        EXPECT (src.find ("getMainInputChannelSet() != juce::AudioChannelSet::disabled()")
                == std::string::npos);
    }

    if (gFails == 0)
    {
        std::cout << "plugin_identity_tests: OK\n";
        return 0;
    }
    std::cerr << "plugin_identity_tests: " << gFails << " failure(s)\n";
    return 1;
}

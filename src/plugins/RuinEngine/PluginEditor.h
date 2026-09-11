#pragma once

#include "PluginProcessor.h"
#include "ui/PflSuiteEditor.h"

class RuinEngineEditor final : public pfl::ui::PflSuiteEditor
{
public:
    explicit RuinEngineEditor (RuinEngineProcessor& p)
        : pfl::ui::PflSuiteEditor (
              p,
              p.getAPVTS(),
              pfl::ui::ProductId::RuinEngine,
              "PFL Ruin Engine",
              "GENERATIVE AUDIO DEGRADATION",
              {
                  { "mix", "MIX", "Dry/wet balance of the ruined signal." },
                  { "age", "AGE", "How aged and worn the material becomes." },
                  { "instability", "INSTABILITY", "How erratically the ruin drifts." },
                  { "output", "OUTPUT", "Output level." },
              })
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RuinEngineEditor)
};

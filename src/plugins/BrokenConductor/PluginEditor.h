#pragma once

#include "PluginProcessor.h"
#include "ui/PflSuiteEditor.h"

class BrokenConductorEditor final : public pfl::ui::PflSuiteEditor
{
public:
    explicit BrokenConductorEditor (BrokenConductorProcessor& p)
        : pfl::ui::PflSuiteEditor (
              p,
              p.getAPVTS(),
              pfl::ui::ProductId::BrokenConductor,
              "PFL Broken Conductor",
              "GENERATIVE MIDI COMPOSER",
              {
                  { "density", "DENSITY", "How dense the phrase activity becomes." },
                  { "mutation", "MUTATION", "How eagerly phrases fracture and reform." },
              },
              true,
              true,
              "outputRole",
              "OUTPUT ROLE")
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrokenConductorEditor)
};

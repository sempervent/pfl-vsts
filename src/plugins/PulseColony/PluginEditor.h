#pragma once

#include "PluginProcessor.h"
#include "ui/PflSuiteEditor.h"

class PulseColonyEditor final : public pfl::ui::PflSuiteEditor
{
public:
    explicit PulseColonyEditor (PulseColonyProcessor& p)
        : pfl::ui::PflSuiteEditor (
              p,
              p.getAPVTS(),
              pfl::ui::ProductId::PulseColony,
              "PFL Pulse Colony",
              "GENERATIVE RHYTHMIC PROCESSOR",
              {
                  { "mix", "MIX", "Dry/wet balance of the colony gate." },
                  { "density", "DENSITY", "How populated the rhythmic colony becomes." },
                  { "mutation", "MUTATION", "How readily cell patterns transform." },
                  { "motion", "MOTION", "How actively cells renegotiate timing." },
                  { "output", "OUTPUT", "Output level." },
              })
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulseColonyEditor)
};

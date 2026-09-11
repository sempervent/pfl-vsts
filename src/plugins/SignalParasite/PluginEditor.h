#pragma once

#include "PluginProcessor.h"
#include "ui/PflSuiteEditor.h"

class SignalParasiteEditor final : public pfl::ui::PflSuiteEditor
{
public:
    explicit SignalParasiteEditor (SignalParasiteProcessor& p)
        : pfl::ui::PflSuiteEditor (
              p,
              p.getAPVTS(),
              pfl::ui::ProductId::SignalParasite,
              "PFL Signal Parasite",
              "AUDIO-REACTIVE COLLABORATOR",
              {
                  { "mix", "MIX", "Dry/wet balance of parasitic response." },
                  { "sensitivity", "SENSITIVITY", "How readily the parasite notices stimulus." },
                  { "hunger", "HUNGER", "How eagerly the parasite responds." },
                  { "mutation", "MUTATION", "How readily attached behaviour transforms." },
                  { "output", "OUTPUT", "Output level." },
              })
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalParasiteEditor)
};

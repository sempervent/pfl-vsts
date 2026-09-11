#pragma once

#include "PluginProcessor.h"
#include "ui/PflSuiteEditor.h"

class MemoryEaterEditor final : public pfl::ui::PflSuiteEditor
{
public:
    explicit MemoryEaterEditor (MemoryEaterProcessor& p)
        : pfl::ui::PflSuiteEditor (
              p,
              p.getAPVTS(),
              pfl::ui::ProductId::MemoryEater,
              "PFL Memory Eater",
              "GENERATIVE AUDIO MEMORY",
              {
                  { "mix", "MIX", "Dry/wet balance of recalled material." },
                  { "hunger", "HUNGER", "How eagerly memory is consumed and returned." },
                  { "memory", "MEMORY", "How far back the organism reaches." },
                  { "output", "OUTPUT", "Output level." },
              })
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryEaterEditor)
};

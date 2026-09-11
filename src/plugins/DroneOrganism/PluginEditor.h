#pragma once

#include "PluginProcessor.h"
#include "ui/PflSuiteEditor.h"

class DroneOrganismEditor final : public pfl::ui::PflSuiteEditor
{
public:
    explicit DroneOrganismEditor (DroneOrganismProcessor& p)
        : pfl::ui::PflSuiteEditor (
              p,
              p.getAPVTS(),
              pfl::ui::ProductId::DroneOrganism,
              "PFL Drone Organism",
              "GENERATIVE DRONE COMPOSER",
              {
                  { "density", "DENSITY", "How dense the colony of voices becomes." },
                  { "mutation", "MUTATION", "How readily the material transforms." },
                  { "drift", "DRIFT", "Slow wandering of pitch and motion." },
                  { "dirt", "DIRT", "Noise and grit in the texture." },
                  { "space", "SPACE", "Ambient space around the drone." },
                  { "output", "OUTPUT", "Output level." },
              })
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DroneOrganismEditor)
};

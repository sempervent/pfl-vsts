juce_add_plugin(DroneOrganism
    COMPANY_NAME "PFL"
    BUNDLE_ID "com.pfl.droneorganism"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${PFL_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE PflG
    PLUGIN_CODE Dro1
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "PFL Drone Organism"
    DESCRIPTION "Self-composing generative drone instrument"
    VST3_CATEGORIES "Instrument|Synth"
    AU_MAIN_TYPE "kAudioUnitType_MusicDevice"
    AU_SANDBOX_SAFE TRUE
)

target_sources(DroneOrganism
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
)

target_include_directories(DroneOrganism
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(DroneOrganism
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(DroneOrganism
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

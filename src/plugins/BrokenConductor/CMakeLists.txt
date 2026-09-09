juce_add_plugin(BrokenConductor
    COMPANY_NAME "PFL"
    BUNDLE_ID "com.pfl.brokenconductor"
    # Ableton Live 11 requires an Instrument-class VST3 with audio outs to host
    # a MIDI generator reliably. Silent stereo out; product remains MIDI-only.
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${PFL_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE PflG
    PLUGIN_CODE Brk1
    # Standalone omitted: does not prove DAW MIDI routing (Stage 1B)
    FORMATS AU VST3
    PRODUCT_NAME "PFL Broken Conductor"
    DESCRIPTION "Deterministic generative MIDI composer"
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE TRUE
)

target_sources(BrokenConductor
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
)

target_include_directories(BrokenConductor
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(BrokenConductor
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(BrokenConductor
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

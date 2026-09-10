juce_add_plugin(SignalParasite
    COMPANY_NAME "PFL"
    BUNDLE_ID "com.pfl.signalparasite"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${PFL_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE PflG
    PLUGIN_CODE Sig1
    FORMATS AU VST3
    PRODUCT_NAME "PFL Signal Parasite"
    DESCRIPTION "Deterministic audio-reactive generative collaborator"
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_SANDBOX_SAFE TRUE
)

target_sources(SignalParasite
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
)

target_include_directories(SignalParasite
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(SignalParasite
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(SignalParasite
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

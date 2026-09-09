add_executable(pfl_dsp_smoke
    dsp_smoke.cpp
)
target_include_directories(pfl_dsp_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_dsp_smoke PRIVATE cxx_std_20)
add_test(NAME dsp_smoke COMMAND pfl_dsp_smoke)

add_executable(pfl_audio_engine_smoke
    audio_engine_smoke.cpp
)
target_include_directories(pfl_audio_engine_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_audio_engine_smoke PRIVATE cxx_std_20)
add_test(NAME audio_engine_smoke COMMAND pfl_audio_engine_smoke)

add_executable(pfl_offline_render
    offline_render.cpp
)
target_include_directories(pfl_offline_render PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_offline_render PRIVATE cxx_std_20)
target_compile_definitions(pfl_offline_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(pfl_offline_render
    PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_events
)

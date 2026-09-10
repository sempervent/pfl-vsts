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

add_executable(pfl_composer_tests
    composer_tests.cpp
)
target_include_directories(pfl_composer_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_composer_tests PRIVATE cxx_std_20)
add_test(NAME composer_tests COMMAND pfl_composer_tests)

add_executable(pfl_phrase_tests
    phrase_tests.cpp
)
target_include_directories(pfl_phrase_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_phrase_tests PRIVATE cxx_std_20)
add_test(NAME phrase_tests COMMAND pfl_phrase_tests)

add_executable(pfl_performance_tests
    performance_tests.cpp
)
target_include_directories(pfl_performance_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_performance_tests PRIVATE cxx_std_20)
add_test(NAME performance_tests COMMAND pfl_performance_tests)

add_executable(pfl_broken_conductor_tests
    broken_conductor_tests.cpp
)
target_include_directories(pfl_broken_conductor_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_broken_conductor_tests PRIVATE cxx_std_20)
add_test(NAME broken_conductor_tests COMMAND pfl_broken_conductor_tests)

add_executable(pfl_broken_conductor_render
    broken_conductor_render.cpp
)
target_include_directories(pfl_broken_conductor_render PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_broken_conductor_render PRIVATE cxx_std_20)

add_executable(pfl_ruin_engine_tests
    ruin_engine_tests.cpp
)
target_include_directories(pfl_ruin_engine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_ruin_engine_tests PRIVATE cxx_std_20)
add_test(NAME ruin_engine_tests COMMAND pfl_ruin_engine_tests)

add_executable(pfl_ruin_engine_render
    ruin_engine_render.cpp
)
target_include_directories(pfl_ruin_engine_render PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_ruin_engine_render PRIVATE cxx_std_20)
target_compile_definitions(pfl_ruin_engine_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(pfl_ruin_engine_render
    PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_events
)

add_executable(pfl_plugin_identity_tests
    plugin_identity_tests.cpp
)
target_include_directories(pfl_plugin_identity_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_plugin_identity_tests PRIVATE cxx_std_20)
target_compile_definitions(pfl_plugin_identity_tests PRIVATE
    PFL_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME plugin_identity_tests COMMAND pfl_plugin_identity_tests)

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

add_executable(pfl_performance_render
    performance_render.cpp
)
target_include_directories(pfl_performance_render PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(pfl_performance_render PRIVATE cxx_std_20)
target_compile_definitions(pfl_performance_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(pfl_performance_render
    PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_events
)

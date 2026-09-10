cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures")

project(PFLGenerativeInstruments VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PFL_BUILD_TESTS "Build unit tests" ON)
option(PFL_COPY_PLUGIN_AFTER_BUILD "Install plugins to user Library after build" ON)

# Pinned JUCE version — see docs/DECISIONS.md
set(PFL_JUCE_GIT_TAG "8.0.15" CACHE STRING "Pinned JUCE git tag")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${PFL_JUCE_GIT_TAG}
    GIT_SHALLOW TRUE
)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(juce)

enable_testing()

add_subdirectory(src/plugins/DroneOrganism)
add_subdirectory(src/plugins/BrokenConductor)
add_subdirectory(src/plugins/RuinEngine)

if(PFL_BUILD_TESTS)
    add_subdirectory(tests)
endif()

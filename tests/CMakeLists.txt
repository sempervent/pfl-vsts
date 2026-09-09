# Phase 0: sanity tests for safety DSP (header-only units).
# Generative/determinism tests arrive in Phase 2.

add_executable(pfl_dsp_smoke
    dsp_smoke.cpp
)

target_include_directories(pfl_dsp_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_features(pfl_dsp_smoke PRIVATE cxx_std_20)

add_test(NAME dsp_smoke COMMAND pfl_dsp_smoke)

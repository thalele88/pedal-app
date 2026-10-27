cmake_minimum_required(VERSION 3.24)

project(PedalApp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

include(FetchContent)

# Dependencies are pinned to tags so every build gets the same code.
# To use a local checkout instead, pass -DFETCHCONTENT_SOURCE_DIR_<NAME>=/path.
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)

# NeuralAmpModelerCore has no library target, so we only download it
# (SOURCE_SUBDIR points nowhere) and compile its sources below.
# Its Eigen submodule lives on gitlab.com, which often refuses clones under
# load, so we skip submodules and fetch the same Eigen commit from a GitHub
# mirror instead. The full commit hash pins the exact content.
FetchContent_Declare(nam
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG v0.6.0
    GIT_SHALLOW ON
    GIT_SUBMODULES ""
    SOURCE_SUBDIR do-not-add)

FetchContent_Declare(eigen
    GIT_REPOSITORY https://github.com/eigen-mirror/eigen.git
    GIT_TAG bc3b39870ecb690a623a3f49149a358b95c5781d # NAM v0.6.0's submodule pin
    SOURCE_SUBDIR do-not-add)

FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 1.4.0
    GIT_SHALLOW ON)

FetchContent_MakeAvailable(juce nam eigen signalsmith_stretch)

# --- NAM core -----------------------------------------------------------------
file(GLOB NAM_SOURCES "${nam_SOURCE_DIR}/NAM/*.cpp" "${nam_SOURCE_DIR}/NAM/*/*.cpp")
add_library(nam_core STATIC ${NAM_SOURCES})
# SYSTEM keeps NAM's own warnings out of our build output
target_include_directories(nam_core SYSTEM PUBLIC
    "${nam_SOURCE_DIR}"
    "${nam_SOURCE_DIR}/NAM"
    "${eigen_SOURCE_DIR}"
    "${nam_SOURCE_DIR}/Dependencies/nlohmann")
# float samples so NAM reads JUCE buffers directly, A2 fast path for TONE3000 captures
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
if (WIN32)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(nam_core PUBLIC stdc++fs)
endif()
set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Shared DSP (used by the plugin and the tests) ----------------------------
add_library(pedal_dsp STATIC
    Source/dsp/AmpModel.cpp
    Source/dsp/DropTuner.cpp
    Source/dsp/SignalChain.cpp)
target_include_directories(pedal_dsp PUBLIC Source)
# NAM registers its model architectures from static initialisers, so the whole
# archive must be linked or the linker drops them and every model fails to load.
target_link_libraries(pedal_dsp
    PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>" signalsmith-stretch
           juce::juce_dsp juce::juce_audio_formats
    PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(pedal_dsp PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
set_target_properties(pedal_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Plugin + standalone app --------------------------------------------------
set(PEDAL_FORMATS Standalone VST3)
if (APPLE)
    list(APPEND PEDAL_FORMATS AU)
endif()

juce_add_plugin(PedalApp
    COMPANY_NAME "Thales"
    BUNDLE_ID "com.thalele88.pedalapp"
    PLUGIN_MANUFACTURER_CODE Thls
    PLUGIN_CODE Pdl1
    PRODUCT_NAME "Pedal App"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "O Pedal App precisa da entrada de áudio para processar a guitarra ou o baixo."
    FORMATS ${PEDAL_FORMATS})

target_sources(PedalApp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PedalApp PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PedalApp
    PRIVATE pedal_dsp
            juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# --- Tests --------------------------------------------------------------------
option(PEDAL_BUILD_TESTS "Build the engine smoke test" ON)
if (PEDAL_BUILD_TESTS)
    juce_add_console_app(EngineSmokeTest PRODUCT_NAME "EngineSmokeTest")
    target_sources(EngineSmokeTest PRIVATE Tests/EngineSmokeTest.cpp)
    target_compile_definitions(EngineSmokeTest PRIVATE
        PEDAL_EXAMPLE_MODELS_DIR="${nam_SOURCE_DIR}/example_models")
    target_link_libraries(EngineSmokeTest PRIVATE pedal_dsp
        juce::juce_recommended_config_flags)
    enable_testing()
    add_test(NAME EngineSmokeTest COMMAND EngineSmokeTest)
endif()

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
    Source/dsp/SignalChain.cpp
    Source/dsp/TestInputPlayer.cpp)
target_include_directories(pedal_dsp PUBLIC Source)
# NAM registers its model architectures from static initialisers, so the whole
# archive must be linked or the linker drops them and every model fails to load.
target_link_libraries(pedal_dsp
    PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>" signalsmith-stretch
           juce::juce_dsp juce::juce_audio_formats
    PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(pedal_dsp PUBLIC JUCE_USE_CURL=0)
set_target_properties(pedal_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- TONE3000 connection ------------------------------------------------------
# Publishable key (t3k_pub_...) of the app's TONE3000 developer account. It is
# public by design and ships inside the app (OAuth PKCE, no secret), so it can
# live here. Never put the secret key (t3k_cs_...) anywhere in this repo.
# To use another key: -DPEDAL_TONE3000_CLIENT_ID=... or env TONE3000_CLIENT_ID.
set(PEDAL_TONE3000_DEFAULT_CLIENT_ID "t3k_pub_HpEU5hrxJgkk6GXD_vPrRVYLt-sjZwTI")
if (DEFINED ENV{TONE3000_CLIENT_ID})
    set(PEDAL_TONE3000_DEFAULT_CLIENT_ID "$ENV{TONE3000_CLIENT_ID}")
endif()
set(PEDAL_TONE3000_CLIENT_ID "${PEDAL_TONE3000_DEFAULT_CLIENT_ID}" CACHE STRING "TONE3000 publishable key (t3k_pub_...)")
if (NOT PEDAL_TONE3000_CLIENT_ID)
    message(STATUS "PEDAL_TONE3000_CLIENT_ID not set: the TONE3000 button will explain how to add it")
endif()

add_library(pedal_tone3000 STATIC
    Source/tone3000/LoopbackServer.cpp
    Source/tone3000/ModelCache.cpp
    Source/tone3000/OAuth.cpp
    Source/tone3000/RateLimiter.cpp
    Source/tone3000/Tone3000Service.cpp
    Source/tone3000/TokenStore.cpp)
target_include_directories(pedal_tone3000 PUBLIC Source)
target_link_libraries(pedal_tone3000
    PUBLIC juce::juce_events juce::juce_cryptography
    PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(pedal_tone3000
    PUBLIC JUCE_USE_CURL=0
    PRIVATE PEDAL_TONE3000_CLIENT_ID="${PEDAL_TONE3000_CLIENT_ID}")
if (APPLE)
    target_link_libraries(pedal_tone3000 PUBLIC "-framework Security")  # Keychain
elseif (WIN32)
    target_link_libraries(pedal_tone3000 PUBLIC crypt32)                # DPAPI
    target_compile_definitions(pedal_tone3000 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
set_target_properties(pedal_tone3000 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Plugin + standalone app --------------------------------------------------
set(PEDAL_FORMATS Standalone VST3)
if (APPLE)
    list(APPEND PEDAL_FORMATS AU)
endif()

# The TONE3000 catalog opens inside the app in a WebView: WKWebView on macOS
# and Edge WebView2 on Windows. JUCE links the WebView2 SDK statically; the
# SDK is a NuGet package, downloaded here so no NuGet tool is needed. The
# WebView2 *runtime* ships with Windows 11 and almost every Windows 10.
# Linux (CI only) builds without a WebView and uses the system browser.
set(PEDAL_WEBVIEW ON)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PEDAL_WEBVIEW OFF)
endif()

set(PEDAL_NEEDS_WEBVIEW2 FALSE)
if (WIN32 AND PEDAL_WEBVIEW)
    set(PEDAL_NEEDS_WEBVIEW2 TRUE)
    set(PEDAL_WEBVIEW2_VERSION "1.0.1901.177")
    set(webview2_root "${CMAKE_BINARY_DIR}/webview2")
    set(webview2_pkg "${webview2_root}/Microsoft.Web.WebView2.${PEDAL_WEBVIEW2_VERSION}")
    if (NOT EXISTS "${webview2_pkg}/build/native/include/WebView2.h")
        message(STATUS "Downloading the Microsoft.Web.WebView2 ${PEDAL_WEBVIEW2_VERSION} SDK")
        file(DOWNLOAD
            "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${PEDAL_WEBVIEW2_VERSION}"
            "${webview2_root}/webview2.nupkg"
            STATUS webview2_status)
        list(GET webview2_status 0 webview2_code)
        if (NOT webview2_code EQUAL 0)
            message(FATAL_ERROR "WebView2 SDK download failed (${webview2_status}). "
                "Pass -DJUCE_WEBVIEW2_PACKAGE_LOCATION=<folder with Microsoft.Web.WebView2.*>.")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${webview2_root}/webview2.nupkg" DESTINATION "${webview2_pkg}")
    endif()
    if (NOT JUCE_WEBVIEW2_PACKAGE_LOCATION)
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${webview2_root}")
    endif()
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
    NEEDS_WEBVIEW2 ${PEDAL_NEEDS_WEBVIEW2}
    FORMATS ${PEDAL_FORMATS})

target_sources(PedalApp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/tone3000/Tone3000Panel.cpp)

target_compile_definitions(PedalApp PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WEB_BROWSER=$<BOOL:${PEDAL_WEBVIEW}>
    $<$<BOOL:${PEDAL_NEEDS_WEBVIEW2}>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>)

target_link_libraries(PedalApp
    PRIVATE pedal_dsp
            pedal_tone3000
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
        PEDAL_EXAMPLE_MODELS_DIR="${nam_SOURCE_DIR}/example_models"
        PEDAL_EXAMPLE_AUDIO_DIR="${nam_SOURCE_DIR}/example_audio")
    target_link_libraries(EngineSmokeTest PRIVATE pedal_dsp
        juce::juce_recommended_config_flags)
    enable_testing()
    add_test(NAME EngineSmokeTest COMMAND EngineSmokeTest)

    # TONE3000 pieces that work without network or an account.
    juce_add_console_app(Tone3000Test PRODUCT_NAME "Tone3000Test")
    target_sources(Tone3000Test PRIVATE Tests/Tone3000Test.cpp)
    target_link_libraries(Tone3000Test PRIVATE pedal_tone3000
        juce::juce_recommended_config_flags)
    add_test(NAME Tone3000Test COMMAND Tone3000Test)
endif()

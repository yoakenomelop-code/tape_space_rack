cmake_minimum_required (VERSION 3.22)
project (TapeSpaceRack VERSION 1.0.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
# Universal binary (Apple Silicon + Intel) on macOS
set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set (CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# --- JUCE: use a local checkout if given (-DTS_JUCE_PATH=...), otherwise download JUCE 8.0.4
set (TS_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout (optional)")
option (TS_COPY_AFTER_BUILD "Install the plugin into the system plug-in folders after building" OFF)

if (TS_JUCE_PATH)
    add_subdirectory (${TS_JUCE_PATH} JUCE)
else()
    include (FetchContent)
    FetchContent_Declare (JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable (JUCE)
endif()

# factory presets (shared with the web version)
juce_add_binary_data (TSData HEADER_NAME BinaryData.h NAMESPACE TSData SOURCES Resources/presets.json)

juce_add_plugin (TapeSpaceRack
    COMPANY_NAME "Vintage Rack"
    PRODUCT_NAME "Tape Space Rack"
    BUNDLE_ID "com.vintagerack.TapeSpaceRack"
    PLUGIN_MANUFACTURER_CODE VtRk
    PLUGIN_CODE TSRk
    FORMATS AU VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${TS_COPY_AFTER_BUILD})

target_sources (TapeSpaceRack PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions (TapeSpaceRack PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
if (MSVC)
    target_compile_options (TapeSpaceRack PRIVATE /utf-8)   # Japanese UI strings are UTF-8
endif()
target_link_libraries (TapeSpaceRack
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        TSData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offscreen test (render UI + run audio). Not needed for normal builds.
option (TS_BUILD_TESTS "Build the offscreen snapshot test" OFF)
if (TS_BUILD_TESTS)
    juce_add_console_app (TSSnapshot PRODUCT_NAME "TSSnapshot")
    target_sources (TSSnapshot PRIVATE tests/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions (TSSnapshot PRIVATE JucePlugin_Name="Tape Space Rack" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries (TSSnapshot PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_audio_processors TSData PUBLIC juce::juce_recommended_config_flags)
endif()

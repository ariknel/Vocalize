cmake_minimum_required(VERSION 3.22)
project(MicInputVST VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ── JUCE via FetchContent ─────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(MicInputVST
    COMPANY_NAME                "OpenMic"
    PLUGIN_MANUFACTURER_CODE    Omic
    PLUGIN_CODE                 Mi01
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "MicInput"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Instrument|Generator"
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

juce_generate_juce_header(MicInputVST)

target_sources(MicInputVST PRIVATE
    src/audio/WasapiCapture.cpp
    src/plugin/PluginProcessor.cpp
    src/gui/LookAndFeel.cpp
    src/gui/PluginEditor.cpp
    src/gui/components/StatCard.cpp
    src/gui/components/LevelMeter.cpp
    src/gui/components/ModeSelector.cpp
)

target_include_directories(MicInputVST PRIVATE src)

target_compile_definitions(MicInputVST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(MicInputVST PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    mmdevapi avrt ole32 winmm
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

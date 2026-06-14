cmake_minimum_required(VERSION 3.22)
project(k2000 VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Speed up Windows builds; harmless on Linux
set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

add_subdirectory(third_party/JUCE)

juce_add_plugin(k2000
    COMPANY_NAME "k2000"
    BUNDLE_ID com.k2000.k2000
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE K2vs
    PLUGIN_CODE K2vs
    FORMATS VST3 Standalone
    PRODUCT_NAME "k2000")

juce_generate_juce_header(k2000)

target_sources(k2000 PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Voice.cpp
    src/VoiceManager.cpp
    src/Layer.cpp
    src/Program.cpp
    src/params/Parameters.cpp
    src/dsp/Oscillator.cpp
    src/dsp/Envelope.cpp
    src/dsp/blocks/SVFFilter.cpp
    src/dsp/blocks/Waveshaper.cpp
    src/dsp/AlgorithmLibrary.cpp)

target_compile_definitions(k2000 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(k2000
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(BUILD_TESTING "Build the unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

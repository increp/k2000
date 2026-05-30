add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    OscillatorTests.cpp
    EnvelopeTests.cpp
    SVFFilterTests.cpp
    WaveshaperTests.cpp
    VoiceTests.cpp
    VoiceManagerTests.cpp
    PluginLifecycleTests.cpp
    ../src/PluginProcessor.cpp
    ../src/Voice.cpp
    ../src/VoiceManager.cpp
    ../src/params/Parameters.cpp
    ../src/dsp/Oscillator.cpp
    ../src/dsp/Envelope.cpp
    ../src/dsp/blocks/SVFFilter.cpp
    ../src/dsp/blocks/Waveshaper.cpp)

target_link_libraries(k2000_tests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(k2000_tests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    K2000_TESTING=1)

set_target_properties(k2000_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

add_test(NAME k2000_tests COMMAND k2000_tests)

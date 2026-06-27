add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    OscillatorTests.cpp
    EnvelopeTests.cpp
    WaveshaperTests.cpp
    VoiceTests.cpp
    VoiceManagerTests.cpp
    PluginLifecycleTests.cpp
    AlgorithmTests.cpp
    AlgorithmNameTests.cpp
    SpineFilterTests.cpp
    HuggettNonlinearTests.cpp
    HuggettSeparationTests.cpp
    HuggettSeparationClickTests.cpp
    HuggettDriveOnsetTests.cpp
    OverdriveDiagnosticTests.cpp
    FilterModelLibraryTests.cpp
    InPlaceStateTests.cpp
    ModelHotSwapTests.cpp
    LayerTests.cpp
    AlgorithmRoutingTests.cpp
    LayerRoutingTests.cpp
    MultiLayerTests.cpp
    ParamBinderTests.cpp
    HpPreFilterTests.cpp
    SafetyLimiterTests.cpp
    Halfband2xTests.cpp
    VoiceOversamplerTests.cpp
    CmajorSvfAdapterTests.cpp
    CmajorSvfEquivalenceTests.cpp
    CmajorFilterModelTests.cpp
    CmajorSvfPerfTests.cpp
    NlSvfEquivalenceTests.cpp
    AsymDriveEquivalenceTests.cpp
    WtOscTests.cpp
    NlSvfPerfTests.cpp
    NlSvfFusedPerfTests.cpp
    TestDspSelfTests.cpp
    MoogPipelineTests.cpp
    MoogLadderTests.cpp
    fixtures/SpineShaperHarnessTests.cpp
    fixtures/SpineHuggettHarnessTests.cpp
    fixtures/SpineNlSvfHarnessTests.cpp
    fixtures/SpineDcBlockerHarnessTests.cpp
    fixtures/SpineHpStageHarnessTests.cpp
    ../src/PluginProcessor.cpp
    ../src/Voice.cpp
    ../src/VoiceManager.cpp
    ../src/Layer.cpp
    ../src/Program.cpp
    ../src/params/Parameters.cpp
    ../src/gui/ParamBinder.cpp
    ../src/dsp/Oscillator.cpp
    ../src/dsp/Envelope.cpp
    ../src/dsp/blocks/Waveshaper.cpp
    ../src/dsp/AlgorithmLibrary.cpp
    ../src/dsp/spine/HuggettFilter.cpp
    ../src/dsp/spine/FilterModelLibrary.cpp
    ../src/dsp/spine/SpineFilterSlot.cpp
    ../src/dsp/spine/HuggettHpStage.cpp
    ../src/dsp/spine/cmajor/SvfLinearAdapter.cpp
    ../src/dsp/spine/cmajor/CmajorSvfFilter.cpp
    ../src/dsp/spine/cmajor/NlSvfAdapter.cpp
    ../src/dsp/spine/cmajor/AsymDriveAdapter.cpp
    ../src/dsp/spine/cmajor/WtOscAdapter.cpp
    ../src/dsp/spine/cmajor/NlSvfLeanAdapter.cpp
    ../src/dsp/spine/cmajor/NlSvfDriveLeanAdapter.cpp
    ../src/dsp/spine/cmajor/MoogLadderAdapter.cpp
    ../src/dsp/spine/MoogLadder.cpp)

target_include_directories(k2000_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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
    K2000_TESTING=1
    BERNIE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

set_target_properties(k2000_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

add_test(NAME k2000_tests COMMAND k2000_tests)

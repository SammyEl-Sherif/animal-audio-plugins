# JUCE-dependent, non-UI plugin plumbing shared by every plugin: curve storage in the APVTS state,
# transport reading, the standard shaper parameters and a processor base class.
# INTERFACE library: sources compile into each consuming target (see shared/ui for why).
add_library(animalaudio_core INTERFACE)
add_library(animalaudio::core ALIAS animalaudio_core)
target_include_directories(animalaudio_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_sources(animalaudio_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BuildStamp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CurveState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CurveShaperProcessor.cpp)
target_link_libraries(animalaudio_core INTERFACE
    animalaudio::dsp
    juce::juce_audio_processors
    juce::juce_data_structures
    juce::juce_events)
target_compile_features(animalaudio_core INTERFACE cxx_std_17)

if(AA_BUILD_TESTS)
    add_executable(animalaudio_core_tests tests/CurveStateTests.cpp tests/TextFormatTests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/CurveState.cpp)
    target_link_libraries(animalaudio_core_tests PRIVATE
        animalaudio::dsp
        juce::juce_data_structures
        juce::juce_events
        juce::juce_recommended_config_flags
        Catch2::Catch2WithMain)
    target_compile_definitions(animalaudio_core_tests PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_include_directories(animalaudio_core_tests PRIVATE include)
    catch_discover_tests(animalaudio_core_tests TEST_PREFIX "core.")
endif()

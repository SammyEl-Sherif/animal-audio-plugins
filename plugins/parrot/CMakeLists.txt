set(PARROT_SOURCES
    Source/ParrotEditor.cpp
    Source/ParrotProcessor.cpp
    Source/dsp/ParrotEngine.cpp
    Source/dsp/VoiceShifter.cpp
    Source/ui/ParrotMotif.cpp
    Source/ui/ParrotTheme.cpp)

aa_add_plugin(Parrot
    PRODUCT_NAME "Parrot"
    PLUGIN_CODE Parr
    DESCRIPTION "Pitch and formant voice bender with snap, robot and tube drive"
    NEEDS_MIDI_INPUT TRUE
    SOURCES ${PARROT_SOURCES})

target_include_directories(Parrot PRIVATE Source)

if(AA_BUILD_TESTS)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _parrot_version)
    string(STRIP "${_parrot_version}" _parrot_version)
    add_executable(ParrotTests tests/ParrotTests.cpp ${PARROT_SOURCES})
    target_include_directories(ParrotTests PRIVATE Source)
    aa_attach_plugin_build_info(ParrotTests "${_parrot_version}")
    target_link_libraries(ParrotTests PRIVATE
        animalaudio::testing
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    catch_discover_tests(ParrotTests TEST_PREFIX "parrot." PROPERTIES RESOURCE_LOCK native_display)
endif()

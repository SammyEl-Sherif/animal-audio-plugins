set(MONKEYDYNE_SOURCES
    Source/MonkeydyneEditor.cpp
    Source/MonkeydyneProcessor.cpp
    Source/dsp/Analysis.cpp
    Source/dsp/EditPlan.cpp
    Source/dsp/Edits.cpp
    Source/dsp/Model.cpp
    Source/dsp/Renderer.cpp
    Source/dsp/SegmentCache.cpp
    Source/engine/Session.cpp
    Source/engine/StateCodec.cpp
    Source/engine/Worker.cpp
    Source/ui/EditController.cpp
    Source/ui/Inspector.cpp
    Source/ui/NoteCanvas.cpp
    Source/ui/Overview.cpp
    Source/ui/MonkeydyneMotif.cpp
    Source/ui/MonkeydyneTheme.cpp)

aa_add_plugin(Monkeydyne
    PRODUCT_NAME "Monkeydyne"
    PLUGIN_CODE Mkdy
    DESCRIPTION "Note-based vocal editor: move, snap, stretch and glide every note"
    SOURCES ${MONKEYDYNE_SOURCES})

target_include_directories(Monkeydyne PRIVATE Source)

if(AA_BUILD_TESTS)
    # Set AA_SCREENSHOT_DIR to also write the headless UI renders as PNGs.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _monkeydyne_version)
    string(STRIP "${_monkeydyne_version}" _monkeydyne_version)
    add_executable(MonkeydyneTests tests/MonkeydyneTests.cpp tests/MonkeydyneDspTests.cpp tests/MonkeydyneEditorTests.cpp ${MONKEYDYNE_SOURCES})
    target_include_directories(MonkeydyneTests PRIVATE Source)
    aa_attach_plugin_build_info(MonkeydyneTests "${_monkeydyne_version}")
    target_link_libraries(MonkeydyneTests PRIVATE
        animalaudio::testing
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    catch_discover_tests(MonkeydyneTests TEST_PREFIX "monkeydyne." PROPERTIES RESOURCE_LOCK native_display)
endif()

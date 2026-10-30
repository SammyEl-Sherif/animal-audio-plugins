set(WHALE_SOURCES
    Source/WhaleEditor.cpp
    Source/WhaleProcessor.cpp
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
    Source/ui/WhaleMotif.cpp
    Source/ui/WhaleTheme.cpp)

aa_add_plugin(Whale
    PRODUCT_NAME "Whale"
    PLUGIN_CODE Whal
    DESCRIPTION "Note-based vocal editor: move, snap, stretch and glide every note"
    SOURCES ${WHALE_SOURCES})

target_include_directories(Whale PRIVATE Source)

if(AA_BUILD_TESTS)
    # Set AA_SCREENSHOT_DIR to also write the headless UI renders as PNGs.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _whale_version)
    string(STRIP "${_whale_version}" _whale_version)
    add_executable(WhaleTests tests/WhaleTests.cpp tests/WhaleDspTests.cpp tests/WhaleEditorTests.cpp ${WHALE_SOURCES})
    target_include_directories(WhaleTests PRIVATE Source)
    aa_attach_plugin_build_info(WhaleTests "${_whale_version}")
    target_link_libraries(WhaleTests PRIVATE
        animalaudio::testing
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    catch_discover_tests(WhaleTests TEST_PREFIX "whale." PROPERTIES RESOURCE_LOCK native_display)
endif()

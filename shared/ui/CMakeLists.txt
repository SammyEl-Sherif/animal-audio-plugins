# Shared Animal Audio UI kit: theme + motif hooks, the "shaper" editor (Duck's layout language) and the
# widgets it is built from (brand badge, pill buttons, big knob, choice pill row, curve editor, preset panel).
# INTERFACE library: sources are compiled into each plugin that links it, so JUCE modules are never
# compiled twice into one binary.
add_library(animalaudio_ui INTERFACE)
add_library(animalaudio::ui ALIAS animalaudio_ui)
target_include_directories(animalaudio_ui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_sources(animalaudio_ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AccentKnob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BrandBadge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ChoicePillRow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CurveEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CurvePresetPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Drawing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Panels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PillButton.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RotaryKnob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ShaperEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Theme.cpp)
target_link_libraries(animalaudio_ui INTERFACE
    animalaudio::core
    juce::juce_gui_basics)
target_compile_features(animalaudio_ui INTERFACE cxx_std_17)

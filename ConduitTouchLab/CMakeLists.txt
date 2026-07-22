cmake_minimum_required(VERSION 3.24)

# Eigenständiges Wegwerf-Harness — komplett getrennt vom Conduit-Build.
# Kein /WX, keine Conduit-Sources, kein Catch2/DSP/OSC/Link. Eigener
# JUCE-FetchContent (gleicher Tag wie Conduit, damit Peer/HWND/MouseEvent-
# Verhalten identisch ist).
project(ConduitTouchLab VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(TouchLab
    PRODUCT_NAME "Conduit Touch Lab"
    COMPANY_NAME "Conduit")

target_sources(TouchLab PRIVATE
    Source/Main.cpp
    Source/NativeTouchSource.cpp
    Source/RawPointerSource.cpp
    Source/TraceView.cpp
    Source/MetricsPanel.cpp
    Source/BlindAbMode.cpp
    Source/TouchProbeComponent.cpp)

target_compile_definitions(TouchLab PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

# MSVC behandelt Quell-Literale sonst als System-Codepage — UTF-8-Umlaute in
# den UI-Strings würden zerbrechen / JUCEs CharPointer-Assertion auslösen.
target_compile_options(TouchLab PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(TouchLab
    PRIVATE
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# WM_POINTER-Subclass braucht SetWindowSubclass/DefSubclassProc (Comctl32).
if(WIN32)
    target_link_libraries(TouchLab PRIVATE Comctl32)
endif()

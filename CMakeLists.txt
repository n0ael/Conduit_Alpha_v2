cmake_minimum_required(VERSION 3.24)

project(ConduitAlphaV2
    VERSION 2.0.0
    LANGUAGES C CXX
)

# Strikt C++20 (CLAUDE.md 13.1)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Abhängigkeiten via FetchContent — kein Submodule, kein System-Install
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# App-Target (eigenständige GUI-App, kein Plugin)
# ---------------------------------------------------------------------------
juce_add_gui_app(Conduit
    PRODUCT_NAME "Conduit Alpha v2"
    COMPANY_NAME "Conduit"
    BUNDLE_ID    "com.conduit.alpha"
    MICROPHONE_PERMISSION_ENABLED TRUE  # CV-Eingänge (ES-6) laufen über Audio-Input
)

set(CONDUIT_CPP_SOURCES
    Source/Core/Main.cpp
    Source/Core/EngineProcessor.cpp
    Source/Core/EngineEditor.cpp
    Source/Core/GraphFader.cpp
    Source/Core/GraphManager.cpp
    Source/Modules/ConduitModule.cpp
)

target_sources(Conduit PRIVATE
    ${CONDUIT_CPP_SOURCES}
    Source/Core/EngineProcessor.h
    Source/Core/EngineEditor.h
    Source/Core/GraphFader.h
    Source/Core/GraphManager.h
    Source/Modules/ConduitModule.h
)

target_include_directories(Conduit PRIVATE Source)

# Warnungen als Fehler NUR für Conduit-eigene Sources (CLAUDE.md 13.1) —
# die JUCE-Modul-Sources werden im selben Target mitkompiliert und dürfen
# den Build nicht brechen.
if(MSVC)
    set_source_files_properties(${CONDUIT_CPP_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4;/WX")
else()
    set_source_files_properties(${CONDUIT_CPP_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()

# ---------------------------------------------------------------------------
# Preprocessor Defines — RT Safety Guardrails (CLAUDE.md 13.2)
# ---------------------------------------------------------------------------
target_compile_definitions(Conduit PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=0    # verhindert blockierende Modal-Loops im Message Thread
    JUCE_WEB_BROWSER=0              # keine unnötigen Abhängigkeiten
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Conduit,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Conduit,JUCE_VERSION>"
)

# Plattform-conditional — NICHT global setzen (CLAUDE.md 13.2)
if(APPLE)
    target_compile_definitions(Conduit PUBLIC JUCE_USE_CORE_AUDIO=1)
elseif(WIN32)
    # ASIO erfordert das Steinberg ASIO SDK (separater Download + Lizenz, nicht in JUCE!)
    if(DEFINED JUCE_ASIO_SDK_PATH AND EXISTS "${JUCE_ASIO_SDK_PATH}")
        target_include_directories(Conduit PRIVATE "${JUCE_ASIO_SDK_PATH}/common")
        target_compile_definitions(Conduit PUBLIC JUCE_ASIO=1)
        message(STATUS "Conduit: ASIO aktiviert (${JUCE_ASIO_SDK_PATH})")
    else()
        message(STATUS "Conduit: JUCE_ASIO_SDK_PATH nicht gesetzt — Build ohne ASIO (WASAPI/DirectSound)")
    endif()
endif()

target_link_libraries(Conduit
    PRIVATE
        juce::juce_audio_utils
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Tests — Catch2 v3 via FetchContent (CLAUDE.md 13.4)
# Laufen ohne Audio-Device — kein ASIO nötig.
# ---------------------------------------------------------------------------
option(CONDUIT_BUILD_TESTS "ConduitTests-Target bauen" ON)

if(CONDUIT_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    juce_add_console_app(ConduitTests PRODUCT_NAME "ConduitTests")

    set(CONDUIT_TEST_CPP_SOURCES
        Tests/Core/GraphFaderTests.cpp
        Tests/Core/GraphManagerTests.cpp
    )

    target_sources(ConduitTests PRIVATE
        ${CONDUIT_TEST_CPP_SOURCES}
        Source/Core/GraphFader.cpp
        Source/Core/GraphManager.cpp
    )

    target_include_directories(ConduitTests PRIVATE Source)

    if(MSVC)
        set_source_files_properties(${CONDUIT_TEST_CPP_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4;/WX")
    else()
        set_source_files_properties(${CONDUIT_TEST_CPP_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
    endif()

    target_compile_definitions(ConduitTests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(ConduitTests
        PRIVATE
            Catch2::Catch2WithMain
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    enable_testing()
    add_test(NAME ConduitTests COMMAND ConduitTests)
endif()

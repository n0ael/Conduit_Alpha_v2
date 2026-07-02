# ---------------------------------------------------------------------------
# ConduitAirwindows — portierte Airwindows-DSP (Chris Johnson / Airwindows,
# MIT) als eigenständige Static Library.
#
# Einbindung ins Root-Projekt erfolgt manuell via
#     add_subdirectory(Source/DSP/Airwindows)
# NACH FetchContent(JUCE) — dieses Target erwartet ein bereits konfiguriertes
# juce::juce_audio_basics (ScopedNoDenormals, Leak-Detektor) im umgebenden
# Projekt. Die Root-CMakeLists bleibt unangetastet (Session-Scope).
#
# Das Test-Target ConduitAirwindowsTests entsteht nur, wenn das umgebende
# Projekt Catch2 bereitstellt (Muster: CONDUIT_BUILD_TESTS im Root).
# ---------------------------------------------------------------------------

set(CONDUIT_AIRWINDOWS_CPP_SOURCES
    AirwindowsPlugin.cpp
    AirwindowsRegistry.cpp
    Plugins/Density.cpp
    Plugins/Slew.cpp
    Plugins/Spiral.cpp
)

add_library(ConduitAirwindows STATIC
    ${CONDUIT_AIRWINDOWS_CPP_SOURCES}
    AirwindowsPlugin.h
    AirwindowsRegistry.h
    Plugins/Density.h
    Plugins/Slew.h
    Plugins/Spiral.h
)

# Include-Root ist Source/ — Konsumenten und die Lib selbst inkludieren im
# Stil des Hauptprojekts: "DSP/Airwindows/AirwindowsPlugin.h"
target_include_directories(ConduitAirwindows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_compile_features(ConduitAirwindows PUBLIC cxx_std_20)

# PUBLIC, damit die System-Abhängigkeiten der JUCE-Module (Linux: pthread/dl,
# Windows: via #pragma comment) zu den Konsumenten propagieren.
target_link_libraries(ConduitAirwindows
    PUBLIC
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
)

# Warnungen als Fehler NUR für eigene Sources (Muster Root-CMakeLists) —
# die JUCE-Modul-Sources werden im selben Target mitkompiliert und dürfen
# den Build nicht brechen.
if(MSVC)
    set_source_files_properties(${CONDUIT_AIRWINDOWS_CPP_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4;/WX")
else()
    set_source_files_properties(${CONDUIT_AIRWINDOWS_CPP_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()

# ---------------------------------------------------------------------------
# Tests — Quellen liegen in Tests/DSP/Airwindows/ (Spiegelstruktur 13.5);
# das Target wird hier definiert, damit Tests/ keine eigene CMake-Änderung
# braucht.
# ---------------------------------------------------------------------------
if(TARGET Catch2::Catch2WithMain)
    set(CONDUIT_AIRWINDOWS_TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../Tests/DSP/Airwindows)

    set(CONDUIT_AIRWINDOWS_TEST_SOURCES
        ${CONDUIT_AIRWINDOWS_TESTS_DIR}/AirwindowsRegistryTests.cpp
        ${CONDUIT_AIRWINDOWS_TESTS_DIR}/DensityTests.cpp
        ${CONDUIT_AIRWINDOWS_TESTS_DIR}/SlewTests.cpp
        ${CONDUIT_AIRWINDOWS_TESTS_DIR}/SpiralTests.cpp
    )

    add_executable(ConduitAirwindowsTests
        ${CONDUIT_AIRWINDOWS_TEST_SOURCES}
        ${CONDUIT_AIRWINDOWS_TESTS_DIR}/AirwindowsTestHelpers.h
    )

    target_link_libraries(ConduitAirwindowsTests
        PRIVATE
            ConduitAirwindows
            Catch2::Catch2WithMain
    )

    if(MSVC)
        set_source_files_properties(${CONDUIT_AIRWINDOWS_TEST_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4;/WX")
    else()
        set_source_files_properties(${CONDUIT_AIRWINDOWS_TEST_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
    endif()

    add_test(NAME ConduitAirwindowsTests COMMAND ConduitAirwindowsTests)
endif()

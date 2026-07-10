cmake_minimum_required(VERSION 3.24)

project(ConduitAlphaV3
    VERSION 3.0.0
    LANGUAGES C CXX
)

# Strikt C++20 (CLAUDE.md 13.1)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Sanitizer-Configs (CLAUDE.md 13.4) — via CMakePresets: 'tsan' / 'asan'.
# Gelten global (inkl. JUCE-Module), Builds laufen mit Dummy-Audio-Device —
# kein ASIO nötig. TSan ist unter Windows nicht verfügbar → WSL/Linux/macOS.
# ---------------------------------------------------------------------------
option(CONDUIT_ENABLE_TSAN "ThreadSanitizer aktivieren (nur Clang, nicht Windows)" OFF)
option(CONDUIT_ENABLE_ASAN "AddressSanitizer aktivieren" OFF)

if(CONDUIT_ENABLE_TSAN AND CONDUIT_ENABLE_ASAN)
    message(FATAL_ERROR "TSan und ASan schließen sich gegenseitig aus")
endif()

if(CONDUIT_ENABLE_TSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "TSan erfordert Clang (CLAUDE.md 13.4) — Preset 'tsan' nutzen")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

if(CONDUIT_ENABLE_ASAN)
    if(MSVC)
        # /RTC1 (CMake-Default für Debug) ist mit /fsanitize=address inkompatibel
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address)
    endif()
endif()

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

# Ableton Link (header-only, CLAUDE.md 13.1). SOURCE_SUBDIR zeigt bewusst auf
# ein Verzeichnis ohne CMakeLists.txt — Links eigene Beispiel-/Test-Targets
# werden nicht konfiguriert; das Interface-Target kommt aus der Config-Datei.
FetchContent_Declare(AbletonLink
    GIT_REPOSITORY https://github.com/Ableton/link.git
    GIT_TAG        Link-4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake_include
)
FetchContent_MakeAvailable(AbletonLink)
include(${abletonlink_SOURCE_DIR}/AbletonLinkConfig.cmake)

# Links Config setzt global CMAKE_CXX_STANDARD auf 17 zurück — wir bauen
# strikt C++20 (CLAUDE.md 13.1), Link selbst ist damit kompatibel
set(CMAKE_CXX_STANDARD 20)

# ---------------------------------------------------------------------------
# Eingebettete Assets — Jost-Font (Google Fonts, OFL-Lizenz in Assets/Fonts)
# für das Push-3-Design (CLAUDE.md 10)
# ---------------------------------------------------------------------------
juce_add_binary_data(ConduitAssets
    SOURCES
        Assets/Fonts/Jost-Regular.ttf
        Assets/Fonts/Jost-Medium.ttf
)

# ---------------------------------------------------------------------------
# App-Target (eigenständige GUI-App, kein Plugin)
# ---------------------------------------------------------------------------
juce_add_gui_app(Conduit
    PRODUCT_NAME "Conduit Alpha v3"
    COMPANY_NAME "Conduit"
    BUNDLE_ID    "com.conduit.alpha"
    MICROPHONE_PERMISSION_ENABLED TRUE  # CV-Eingänge (ES-6) laufen über Audio-Input
)

set(CONDUIT_CPP_SOURCES
    Source/Core/Main.cpp
    Source/Core/AudioDeviceController.cpp
    Source/Core/CcControlModel.cpp
    Source/Core/ChannelNames.cpp
    Source/Core/ChordMemory.cpp
    Source/Core/CurveEditInteraction.cpp
    Source/Core/EngineProcessor.cpp
    Source/Core/EngineEditor.cpp
    Source/Core/ExpressionAxis.cpp
    Source/Core/GraphFader.cpp
    Source/Core/GraphManager.cpp
    Source/Core/GridPanelSettings.cpp
    Source/Core/GridVoiceEngine.cpp
    Source/Core/InputLinkSend.cpp
    Source/Core/LinkClock.cpp
    Source/Core/LinkReceiveStream.cpp
    Source/Core/LinkSendTaps.cpp
    Source/Core/MeterSettings.cpp
    Source/Core/Metronome.cpp
    Source/Core/MidiDeviceTarget.cpp
    Source/Core/ModuleUiDefaults.cpp
    Source/Core/MpeEncoder.cpp
    Source/Core/MpeMidiSink.cpp
    Source/Core/NodeUiRegistry.cpp
    Source/Core/OscController.cpp
    Source/Core/OscSendService.cpp
    Source/Core/OscSendSettings.cpp
    Source/Core/PadGridLayout.cpp
    Source/Core/RemoteModuleBinder.cpp
    Source/Core/ResponseCurve.cpp
    Source/Core/RingTouchModel.cpp
    Source/Core/SourceNameResolver.cpp
    Source/Core/TransportSettings.cpp
    Source/Core/UiSettings.cpp
    Source/Core/VoiceAllocator.cpp
    Source/Core/Browser/BrowserContextProvider.cpp
    Source/Core/Browser/BrowserModel.cpp
    Source/Core/Browser/BrowserFileScanner.cpp
    Source/Core/Browser/BrowserPaths.cpp
    Source/Core/Browser/BrowserSearchIndex.cpp
    Source/Core/Capture/BufferPool.cpp
    Source/Core/Capture/CaptureChannel.cpp
    Source/Core/Capture/CaptureService.cpp
    Source/Core/LooperSettings.cpp
    Source/UI/LooperClipControlsRow.cpp
    Source/UI/LooperPanel.cpp
    Source/UI/LooperSettingsMenu.cpp
    Source/UI/LooperTrackStrip.cpp
    Source/Core/Looper/LooperBank.cpp
    Source/Core/Looper/LooperClipExporter.cpp
    Source/Core/Looper/LooperSessionModel.cpp
    Source/Core/Looper/LooperWaveformTap.cpp
    Source/Core/Capture/CaptureSettings.cpp
    Source/Core/Capture/CaptureWriter.cpp
    Source/Core/Capture/InputMeter.cpp
    Source/Core/Capture/LevelMeter.cpp
    Source/TouchLive/LiveSetModel.cpp
    Source/TouchLive/LiveSpectrumTap.cpp
    Source/TouchLive/TouchLiveClient.cpp
    Source/TouchLive/TouchLiveSettings.cpp
    Source/UI/TouchLivePage/TouchLiveBrowserView.cpp
    Source/UI/TouchLivePage/TouchLiveDeviceView.cpp
    Source/UI/TouchLivePage/TouchLiveEq8Panel.cpp
    Source/UI/TouchLivePage/TouchLiveFader.cpp
    Source/UI/TouchLivePage/TouchLiveGridView.cpp
    Source/UI/TouchLivePage/TouchLiveMixerView.cpp
    Source/UI/TouchLivePage/TouchLivePage.cpp
    Source/Modules/ChassisSchema.cpp
    Source/Modules/ConduitModule.cpp
    Source/Modules/ModuleFactory.cpp
    Source/Modules/ProcessorModule.cpp
    Source/Modules/AirwindowsProcessorModule.cpp
    Source/Modules/AirwindowsDensityModule.cpp
    Source/Modules/AirwindowsSlewModule.cpp
    Source/Modules/AirwindowsSpiralModule.cpp
    Source/Modules/AirwindowsAir4Module.cpp
    Source/Modules/AirwindowsCansModule.cpp
    Source/Modules/AirwindowsCansAWModule.cpp
    Source/Modules/AirwindowsConsole0BussModule.cpp
    Source/Modules/AirwindowsConsole0ChannelModule.cpp
    Source/Modules/AirwindowsConsoleLABussModule.cpp
    Source/Modules/AirwindowsConsoleMCBussModule.cpp
    Source/Modules/AirwindowsDeBessModule.cpp
    Source/Modules/AirwindowsDeBezModule.cpp
    Source/Modules/AirwindowsDeRez3Module.cpp
    Source/Modules/AirwindowsDigitalBlackModule.cpp
    Source/Modules/AirwindowsDiscontapeityModule.cpp
    Source/Modules/AirwindowsDistance3Module.cpp
    Source/Modules/AirwindowsDubSub2Module.cpp
    Source/Modules/AirwindowsDubly3Module.cpp
    Source/Modules/AirwindowsFatEQModule.cpp
    Source/Modules/AirwindowsFlutter2Module.cpp
    Source/Modules/AirwindowsGatelopeModule.cpp
    Source/Modules/AirwindowsGlitchShifterModule.cpp
    Source/Modules/AirwindowsHypersoftModule.cpp
    Source/Modules/AirwindowsInflamerModule.cpp
    Source/Modules/AirwindowsIsolator3Module.cpp
    Source/Modules/AirwindowsMackityModule.cpp
    Source/Modules/AirwindowsOneCornerClipModule.cpp
    Source/Modules/AirwindowsParametricModule.cpp
    Source/Modules/AirwindowsPearEQModule.cpp
    Source/Modules/AirwindowsPockey2Module.cpp
    Source/Modules/AirwindowsPointyGuitarModule.cpp
    Source/Modules/AirwindowsPop2Module.cpp
    Source/Modules/AirwindowsSilkenModule.cpp
    Source/Modules/AirwindowsSingleEndedTriodeModule.cpp
    Source/Modules/AirwindowsSmoothModule.cpp
    Source/Modules/AirwindowsSmoothEQ3Module.cpp
    Source/Modules/AirwindowsSoftGateModule.cpp
    Source/Modules/AirwindowsStoneFireCompModule.cpp
    Source/Modules/AirwindowsStonefireModule.cpp
    Source/Modules/AirwindowsSweetenModule.cpp
    Source/Modules/AirwindowsTakeCareModule.cpp
    Source/Modules/AirwindowsTapeDelay2Module.cpp
    Source/Modules/AirwindowsTapeDustModule.cpp
    Source/Modules/AirwindowsTapeHack2Module.cpp
    Source/Modules/AirwindowsToneSlantModule.cpp
    Source/Modules/AirwindowsTremoSquareModule.cpp
    Source/Modules/AirwindowsTrianglizerModule.cpp
    Source/Modules/AirwindowsTube2Module.cpp
    Source/Modules/AirwindowsVibratoModule.cpp
    Source/Modules/AirwindowsWeightModule.cpp
    Source/Modules/AirwindowsWiderModule.cpp
    Source/Modules/AirwindowsChamberModule.cpp
    Source/Modules/AirwindowsGalacticModule.cpp
    Source/Modules/AirwindowsVerbTinyModule.cpp
    Source/Modules/AirwindowsKBeyondModule.cpp
    Source/Modules/AirwindowsKCathedral5Module.cpp
    Source/Modules/AirwindowsKWoodRoomModule.cpp
    Source/Modules/AttenuatorModule.cpp
    Source/Modules/CaptureTapModule.cpp
    Source/Modules/LfoModule.cpp
    Source/Modules/LinkAudioReceiveModule.cpp
    Source/Modules/LinkAudioSendModule.cpp
    Source/Modules/ScopeModule.cpp
    Source/Modules/StepSequencerModule.cpp
    Source/UI/AudioSettingsComponent.cpp
    Source/UI/Browser/BrowserListRow.cpp
    Source/UI/Browser/BrowserPanel.cpp
    Source/UI/Browser/TouchKeyboard.cpp
    Source/UI/CaptureAllButton.cpp
    Source/UI/CapturePanel.cpp
    Source/UI/CaptureSettingsComponent.cpp
    Source/UI/CaptureToast.cpp
    Source/UI/CcControlLayer.cpp
    Source/UI/CcPanel.cpp
    Source/UI/ChannelAttributePanel.cpp
    Source/UI/ChordMemoryStrip.cpp
    Source/UI/ColourSwatchStrip.cpp
    Source/UI/ConduitColorPicker.cpp
    Source/UI/CurveEditor.cpp
    Source/UI/EditorDockPanel.cpp
    Source/UI/ExpressionRibbon.cpp
    Source/UI/FxModulePanel.cpp
    Source/UI/GainFaderMeter.cpp
    Source/UI/GridKeyboardComponent.cpp
    Source/UI/GridPage.cpp
    Source/UI/InputSendButton.cpp
    Source/UI/LevelMeterBar.cpp
    Source/UI/LinkAudioReceivePanel.cpp
    Source/UI/LinkAudioSendPanel.cpp
    Source/UI/LinkAudioStatusBadge.cpp
    Source/UI/LinkMenuPanel.cpp
    Source/UI/LinkSendCreateDialog.cpp
    Source/UI/LockToggle.cpp
    Source/UI/MpeShapingView.cpp
    Source/UI/NodeCanvas.cpp
    Source/UI/NodeComponent.cpp
    Source/UI/NoteCircleFadeTracker.cpp
    Source/UI/OscSettingsComponent.cpp
    Source/UI/LooperPage.cpp
    Source/UI/LooperWaveformStrip.cpp
    Source/UI/NodeAttributePanel.cpp
    Source/UI/NodeColourDot.cpp
    Source/UI/PageHost.cpp
    Source/UI/ParameterPanel.cpp
    Source/UI/PortComponent.cpp
    Source/UI/PushIcons.cpp
    Source/UI/PushLookAndFeel.cpp
    Source/UI/PushTiles.cpp
    Source/UI/ScopeDisplay.cpp
    Source/UI/TapMenuPanel.cpp
    Source/UI/TransportBar.cpp
    Source/UI/DevPanel.cpp
    Source/UI/SequencerControlPanel.cpp
    Source/UI/SettingsWindow.cpp
    Source/UI/StepGridDisplay.cpp
    Source/UI/UiSettingsComponent.cpp
    Source/Util/RtAllocationGuard.cpp
)

target_sources(Conduit PRIVATE
    ${CONDUIT_CPP_SOURCES}
    Source/Core/AudioDeviceController.h
    Source/Core/CcControlModel.h
    Source/Core/ChannelNames.h
    Source/Core/ChordMemory.h
    Source/Core/CurveEditInteraction.h
    Source/Core/EngineProcessor.h
    Source/Core/EngineEditor.h
    Source/Core/ExpressionAxis.h
    Source/Core/GraphFader.h
    Source/Core/GraphManager.h
    Source/Core/GridPanelSettings.h
    Source/Core/GridVoiceEngine.h
    Source/Core/InputLinkSend.h
    Source/Core/LinkClock.h
    Source/Core/LinkSendTaps.h
    Source/Core/Metronome.h
    Source/Core/MidiDeviceTarget.h
    Source/Core/ModuleUiDefaults.h
    Source/Core/MpeEncoder.h
    Source/Core/MpeMidiSink.h
    Source/Core/NodeUiRegistry.h
    Source/Core/OscAddress.h
    Source/Core/OscController.h
    Source/Core/OscSendService.h
    Source/Core/OscSendSettings.h
    Source/Core/PadGridLayout.h
    Source/Core/RemoteModuleBinder.h
    Source/Core/ResponseCurve.h
    Source/Core/RingTouchModel.h
    Source/Core/SourceNameResolver.h
    Source/Core/TransportSettings.h
    Source/Core/VoiceAllocator.h
    Source/Core/Browser/BrowserContextProvider.h
    Source/Core/Browser/BrowserModel.h
    Source/Core/Browser/BrowserFileScanner.h
    Source/Core/Browser/BrowserPaths.h
    Source/Core/Browser/BrowserSearchIndex.h
    Source/Core/Capture/BufferPool.h
    Source/Core/Capture/CaptureChannel.h
    Source/Core/Capture/CaptureGate.h
    Source/Core/Capture/CaptureRingBuffer.h
    Source/Core/Capture/CaptureService.h
    Source/Core/Capture/CaptureSettings.h
    Source/Core/Capture/CaptureWriter.h
    Source/Core/Capture/InputMeter.h
    Source/Core/Capture/PreRollBuffer.h
    Source/Core/Capture/SampleClock.h
    Source/TouchLive/IRemoteTransport.h
    Source/TouchLive/LiveFaderScale.h
    Source/TouchLive/LiveSetModel.h
    Source/TouchLive/LiveSpectrumTap.h
    Source/TouchLive/TouchLiveClient.h
    Source/TouchLive/TouchLiveMeterBus.h
    Source/TouchLive/TouchLiveSettings.h
    Source/UI/TouchLivePage/TouchLiveBespokePanel.h
    Source/UI/TouchLivePage/TouchLiveBrowserView.h
    Source/UI/TouchLivePage/TouchLiveDeviceView.h
    Source/UI/TouchLivePage/TouchLiveEq8Panel.h
    Source/UI/TouchLivePage/TouchLiveFader.h
    Source/UI/TouchLivePage/TouchLiveGridView.h
    Source/UI/TouchLivePage/TouchLiveMixerView.h
    Source/UI/TouchLivePage/TouchLivePage.h
    Source/Interfaces/ICaptureTapClient.h
    Source/Interfaces/IClockSlave.h
    Source/Interfaces/IClockSource.h
    Source/Interfaces/ILinkAudioClient.h
    Source/Interfaces/IMidiOutputTarget.h
    Source/Interfaces/ISendConfigClient.h
    Source/Interfaces/IStochastic.h
    Source/Interfaces/IVoiceSink.h
    Source/Modules/AnalysisModule.h
    Source/Modules/ChassisSchema.h
    Source/Modules/ConduitModule.h
    Source/Modules/GeneratorModule.h
    Source/Modules/IOModule.h
    Source/Modules/ModuleFactory.h
    Source/Modules/NetworkIOModule.h
    Source/Modules/ProcessorModule.h
    Source/Modules/UtilityModule.h
    Source/Modules/AirwindowsProcessorModule.h
    Source/Modules/AirwindowsDensityModule.h
    Source/Modules/AirwindowsSlewModule.h
    Source/Modules/AirwindowsSpiralModule.h
    Source/Modules/AirwindowsAir4Module.h
    Source/Modules/AirwindowsCansModule.h
    Source/Modules/AirwindowsCansAWModule.h
    Source/Modules/AirwindowsConsole0BussModule.h
    Source/Modules/AirwindowsConsole0ChannelModule.h
    Source/Modules/AirwindowsConsoleLABussModule.h
    Source/Modules/AirwindowsConsoleMCBussModule.h
    Source/Modules/AirwindowsDeBessModule.h
    Source/Modules/AirwindowsDeBezModule.h
    Source/Modules/AirwindowsDeRez3Module.h
    Source/Modules/AirwindowsDigitalBlackModule.h
    Source/Modules/AirwindowsDiscontapeityModule.h
    Source/Modules/AirwindowsDistance3Module.h
    Source/Modules/AirwindowsDubSub2Module.h
    Source/Modules/AirwindowsDubly3Module.h
    Source/Modules/AirwindowsFatEQModule.h
    Source/Modules/AirwindowsFlutter2Module.h
    Source/Modules/AirwindowsGatelopeModule.h
    Source/Modules/AirwindowsGlitchShifterModule.h
    Source/Modules/AirwindowsHypersoftModule.h
    Source/Modules/AirwindowsInflamerModule.h
    Source/Modules/AirwindowsIsolator3Module.h
    Source/Modules/AirwindowsMackityModule.h
    Source/Modules/AirwindowsOneCornerClipModule.h
    Source/Modules/AirwindowsParametricModule.h
    Source/Modules/AirwindowsPearEQModule.h
    Source/Modules/AirwindowsPockey2Module.h
    Source/Modules/AirwindowsPointyGuitarModule.h
    Source/Modules/AirwindowsPop2Module.h
    Source/Modules/AirwindowsSilkenModule.h
    Source/Modules/AirwindowsSingleEndedTriodeModule.h
    Source/Modules/AirwindowsSmoothModule.h
    Source/Modules/AirwindowsSmoothEQ3Module.h
    Source/Modules/AirwindowsSoftGateModule.h
    Source/Modules/AirwindowsStoneFireCompModule.h
    Source/Modules/AirwindowsStonefireModule.h
    Source/Modules/AirwindowsSweetenModule.h
    Source/Modules/AirwindowsTakeCareModule.h
    Source/Modules/AirwindowsTapeDelay2Module.h
    Source/Modules/AirwindowsTapeDustModule.h
    Source/Modules/AirwindowsTapeHack2Module.h
    Source/Modules/AirwindowsToneSlantModule.h
    Source/Modules/AirwindowsTremoSquareModule.h
    Source/Modules/AirwindowsTrianglizerModule.h
    Source/Modules/AirwindowsTube2Module.h
    Source/Modules/AirwindowsVibratoModule.h
    Source/Modules/AirwindowsWeightModule.h
    Source/Modules/AirwindowsWiderModule.h
    Source/Modules/AirwindowsChamberModule.h
    Source/Modules/AirwindowsGalacticModule.h
    Source/Modules/AirwindowsVerbTinyModule.h
    Source/Modules/AirwindowsKBeyondModule.h
    Source/Modules/AirwindowsKCathedral5Module.h
    Source/Modules/AirwindowsKWoodRoomModule.h
    Source/Modules/AttenuatorModule.h
    Source/Modules/CaptureTapModule.h
    Source/Modules/LfoModule.h
    Source/Modules/LinkAudioSendModule.h
    Source/Modules/ScopeModule.h
    Source/Modules/StepSequencerModule.h
    Source/UI/AnimatedValue.h
    Source/UI/AudioSettingsComponent.h
    Source/UI/Browser/BrowserDragPayload.h
    Source/UI/Browser/BrowserListRow.h
    Source/UI/Browser/BrowserPanel.h
    Source/UI/Browser/TouchKeyboard.h
    Source/UI/CaptureAllButton.h
    Source/UI/CapturePanel.h
    Source/UI/CaptureToast.h
    Source/UI/CcControlLayer.h
    Source/UI/CcPanel.h
    Source/UI/ChannelAttributePanel.h
    Source/UI/ChordMemoryStrip.h
    Source/UI/ColourSwatchStrip.h
    Source/UI/ConduitColorPicker.h
    Source/UI/CurveEditor.h
    Source/UI/CurvedSlider.h
    Source/UI/EditorDockPanel.h
    Source/UI/ExpressionRibbon.h
    Source/UI/FxModulePanel.h
    Source/UI/GainFaderMeter.h
    Source/UI/GridKeyboardComponent.h
    Source/UI/GridPage.h
    Source/UI/InputSendButton.h
    Source/UI/LinkAudioReceivePanel.h
    Source/UI/LinkAudioSendPanel.h
    Source/UI/LinkAudioStatusBadge.h
    Source/UI/LinkMenuPanel.h
    Source/UI/LinkSendCreateDialog.h
    Source/UI/LockToggle.h
    Source/UI/MpeShapingView.h
    Source/UI/NodeAttributePanel.h
    Source/UI/NodeCanvas.h
    Source/UI/NodeColourDot.h
    Source/UI/NodeComponent.h
    Source/UI/NoteCircleFadeTracker.h
    Source/UI/OscSettingsComponent.h
    Source/UI/PageHost.h
    Source/UI/ParameterPanel.h
    Source/UI/PortComponent.h
    Source/UI/PushIcons.h
    Source/UI/PushLookAndFeel.h
    Source/UI/PushTiles.h
    Source/UI/ScopeDisplay.h
    Source/UI/TapMenuPanel.h
    Source/UI/TransportBar.h
    Source/UI/SequencerControlPanel.h
    Source/UI/StepGridDisplay.h
    Source/Util/RtAllocationGuard.h
    Source/Util/ScaleQuantizer.h
    Source/Util/SpscQueue.h
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
    # RT-Audit (Util/RtAllocationGuard): Debug-Builds ersetzen operator
    # new/delete und zählen Allokationen in Echtzeit-Abschnitten
    $<$<CONFIG:Debug>:CONDUIT_RT_ALLOCATION_CHECKS=1>
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
        ConduitAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_osc
        Ableton::Link
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Links asio-standalone braucht unter Windows WinSock explizit
if(WIN32)
    target_link_libraries(Conduit PRIVATE ws2_32 iphlpapi)
endif()

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
        Tests/Core/AirwindowsModuleTests.cpp
        Tests/Core/AudioDeviceControllerTests.cpp
        Tests/Core/Browser/BrowserContextProviderTests.cpp
        Tests/Core/Browser/BrowserModelTests.cpp
        Tests/Core/Browser/BrowserFileScannerTests.cpp
        Tests/Core/Browser/BrowserSearchIndexTests.cpp
        Tests/Core/CallbackTimingMonitorTests.cpp
        Tests/Core/CaptureBufferTests.cpp
        Tests/Core/CaptureGateTests.cpp
        Tests/Core/CaptureMeterTests.cpp
        Tests/Core/CaptureSettingsTests.cpp
        Tests/Core/CaptureStressTests.cpp
        Tests/Core/CaptureTapTests.cpp
        Tests/Core/CaptureWriterTests.cpp
        Tests/Core/CcControlModelTests.cpp
        Tests/Core/ChannelNamesTests.cpp
        Tests/Core/ChordMemoryTests.cpp
        Tests/Core/ClockTests.cpp
        Tests/Core/CurveEditInteractionTests.cpp
        Tests/Core/EngineIOChannelTests.cpp
        Tests/Core/ExpressionAxisTests.cpp
        Tests/Core/GraphFaderTests.cpp
        Tests/Core/NamedIdTests.cpp
        Tests/Core/GraphManagerTests.cpp
        Tests/Core/GridVoiceEngineTests.cpp
        Tests/Core/PadGridLayoutTests.cpp
        Tests/Core/RingTouchModelTests.cpp
        Tests/Core/LevelMeterTests.cpp
        Tests/Core/InputLinkSendTests.cpp
        Tests/Core/LinkAudioReceiveTests.cpp
        Tests/Core/LinkAudioSendTests.cpp
        Tests/Core/LinkReceiveStreamTests.cpp
        Tests/Core/LinkSendTapsTests.cpp
        Tests/Core/LaunchQuantizationTests.cpp
        Tests/Core/LooperMathTests.cpp
        Tests/Core/LooperClipMathTests.cpp
        Tests/Core/BarSampleAnchorsTests.cpp
        Tests/Core/LooperSourceTests.cpp
        Tests/Core/LooperWaveformTapTests.cpp
        Tests/Core/LooperBankTests.cpp
        Tests/Core/LooperClipExporterTests.cpp
        Tests/Core/LooperSessionModelTests.cpp
        Tests/Core/LooperSettingsTests.cpp
        Tests/Core/MasterTapTests.cpp
        Tests/Core/MeterSettingsTests.cpp
        Tests/Core/MetronomeTests.cpp
        Tests/Core/MpeEncoderTests.cpp
        Tests/Core/MpeMidiSinkTests.cpp
        Tests/Core/OscControllerTests.cpp
        Tests/Core/OscSendServiceTests.cpp
        Tests/Core/PatchingTests.cpp
        Tests/Core/ProcessorChassisTests.cpp
        Tests/Core/RemoteModuleBinderTests.cpp
        Tests/Core/ResponseCurveTests.cpp
        Tests/Core/PresetTests.cpp
        Tests/Core/ScopeModuleTests.cpp
        Tests/Core/SequencerTests.cpp
        Tests/Core/SourceNameResolverTests.cpp
        Tests/Core/ThreadingStressTests.cpp
        Tests/Core/TransportLinkTests.cpp
        Tests/Core/UiSettingsTests.cpp
        Tests/Core/VoiceAllocatorTests.cpp
        Tests/TouchLive/LiveFaderScaleTests.cpp
        Tests/TouchLive/LiveSetModelTests.cpp
        Tests/TouchLive/LiveSpectrumTapTests.cpp
        Tests/TouchLive/TouchLiveClientTests.cpp
        Tests/UI/TouchLivePageTests.cpp
        Tests/UI/BrowserPanelTests.cpp
        Tests/UI/ConduitColorPickerTests.cpp
        Tests/UI/EditorDockPanelTests.cpp
        Tests/UI/NoteCircleFadeTrackerTests.cpp
        Tests/UI/TouchKeyboardTests.cpp
        Tests/UI/GridKeyboardTests.cpp
        Tests/UI/FxModulePanelTests.cpp
        Tests/UI/NodeCanvasTests.cpp
        Tests/UI/LevelMeterBarTests.cpp
        Tests/UI/LinkAudioReceivePanelTests.cpp
        Tests/UI/LinkAudioSendPanelTests.cpp
        Tests/UI/OscSettingsComponentTests.cpp
        Tests/UI/LooperPageTests.cpp
        Tests/UI/LooperWaveformStripTests.cpp
        Tests/UI/PageHostTests.cpp
        Tests/UI/PushDesignTests.cpp
        Tests/UI/SequencerControlPanelTests.cpp
        Tests/UI/TransportBarTests.cpp
        Tests/Util/ScaleQuantizerTests.cpp
        Tests/Util/SpscQueueTests.cpp
    )

    target_sources(ConduitTests PRIVATE
        ${CONDUIT_TEST_CPP_SOURCES}
        Source/Core/AudioDeviceController.cpp
        Source/Core/CcControlModel.cpp
        Source/Core/ChannelNames.cpp
        Source/Core/ChordMemory.cpp
        Source/Core/CurveEditInteraction.cpp
        Source/Core/EngineEditor.cpp
        Source/Core/EngineProcessor.cpp
        Source/Core/ExpressionAxis.cpp
        Source/Core/GraphFader.cpp
        Source/Core/GraphManager.cpp
        Source/Core/GridPanelSettings.cpp
        Source/Core/GridVoiceEngine.cpp
        Source/Core/InputLinkSend.cpp
        Source/Core/LinkClock.cpp
        Source/Core/LinkReceiveStream.cpp
        Source/Core/LinkSendTaps.cpp
        Source/Core/MeterSettings.cpp
        Source/Core/Metronome.cpp
        Source/Core/MidiDeviceTarget.cpp
        Source/Core/ModuleUiDefaults.cpp
        Source/Core/MpeEncoder.cpp
        Source/Core/MpeMidiSink.cpp
        Source/Core/NodeUiRegistry.cpp
        Source/Core/OscController.cpp
        Source/Core/OscSendService.cpp
        Source/Core/OscSendSettings.cpp
        Source/Core/PadGridLayout.cpp
        Source/Core/RemoteModuleBinder.cpp
        Source/Core/ResponseCurve.cpp
        Source/Core/RingTouchModel.cpp
        Source/Core/SourceNameResolver.cpp
        Source/Core/TransportSettings.cpp
        Source/Core/UiSettings.cpp
        Source/Core/VoiceAllocator.cpp
        Source/Core/Browser/BrowserContextProvider.cpp
        Source/Core/Browser/BrowserModel.cpp
        Source/Core/Browser/BrowserFileScanner.cpp
    Source/Core/Browser/BrowserPaths.cpp
    Source/Core/Browser/BrowserSearchIndex.cpp
        Source/Core/Capture/BufferPool.cpp
        Source/Core/Capture/CaptureChannel.cpp
        Source/Core/Capture/CaptureService.cpp
        Source/Core/LooperSettings.cpp
        Source/UI/LooperClipControlsRow.cpp
        Source/UI/LooperPanel.cpp
        Source/UI/LooperSettingsMenu.cpp
        Source/UI/LooperTrackStrip.cpp
        Source/Core/Looper/LooperBank.cpp
        Source/Core/Looper/LooperClipExporter.cpp
        Source/Core/Looper/LooperSessionModel.cpp
        Source/Core/Looper/LooperWaveformTap.cpp
        Source/Core/Capture/CaptureSettings.cpp
        Source/Core/Capture/CaptureWriter.cpp
        Source/Core/Capture/InputMeter.cpp
        Source/Core/Capture/LevelMeter.cpp
        Source/TouchLive/LiveSetModel.cpp
        Source/TouchLive/LiveSpectrumTap.cpp
        Source/TouchLive/TouchLiveClient.cpp
        Source/TouchLive/TouchLiveSettings.cpp
        Source/UI/TouchLivePage/TouchLiveBrowserView.cpp
        Source/UI/TouchLivePage/TouchLiveDeviceView.cpp
        Source/UI/TouchLivePage/TouchLiveEq8Panel.cpp
        Source/UI/TouchLivePage/TouchLiveFader.cpp
        Source/UI/TouchLivePage/TouchLiveGridView.cpp
        Source/UI/TouchLivePage/TouchLiveMixerView.cpp
        Source/UI/TouchLivePage/TouchLivePage.cpp
        Source/Modules/ChassisSchema.cpp
        Source/Modules/ConduitModule.cpp
        Source/Modules/ModuleFactory.cpp
        Source/Modules/ProcessorModule.cpp
        Source/Modules/AirwindowsProcessorModule.cpp
        Source/Modules/AirwindowsDensityModule.cpp
        Source/Modules/AirwindowsSlewModule.cpp
        Source/Modules/AirwindowsSpiralModule.cpp
        Source/Modules/AirwindowsAir4Module.cpp
        Source/Modules/AirwindowsCansModule.cpp
        Source/Modules/AirwindowsCansAWModule.cpp
        Source/Modules/AirwindowsConsole0BussModule.cpp
        Source/Modules/AirwindowsConsole0ChannelModule.cpp
        Source/Modules/AirwindowsConsoleLABussModule.cpp
        Source/Modules/AirwindowsConsoleMCBussModule.cpp
        Source/Modules/AirwindowsDeBessModule.cpp
        Source/Modules/AirwindowsDeBezModule.cpp
        Source/Modules/AirwindowsDeRez3Module.cpp
        Source/Modules/AirwindowsDigitalBlackModule.cpp
        Source/Modules/AirwindowsDiscontapeityModule.cpp
        Source/Modules/AirwindowsDistance3Module.cpp
        Source/Modules/AirwindowsDubSub2Module.cpp
        Source/Modules/AirwindowsDubly3Module.cpp
        Source/Modules/AirwindowsFatEQModule.cpp
        Source/Modules/AirwindowsFlutter2Module.cpp
        Source/Modules/AirwindowsGatelopeModule.cpp
        Source/Modules/AirwindowsGlitchShifterModule.cpp
        Source/Modules/AirwindowsHypersoftModule.cpp
        Source/Modules/AirwindowsInflamerModule.cpp
        Source/Modules/AirwindowsIsolator3Module.cpp
        Source/Modules/AirwindowsMackityModule.cpp
        Source/Modules/AirwindowsOneCornerClipModule.cpp
        Source/Modules/AirwindowsParametricModule.cpp
        Source/Modules/AirwindowsPearEQModule.cpp
        Source/Modules/AirwindowsPockey2Module.cpp
        Source/Modules/AirwindowsPointyGuitarModule.cpp
        Source/Modules/AirwindowsPop2Module.cpp
        Source/Modules/AirwindowsSilkenModule.cpp
        Source/Modules/AirwindowsSingleEndedTriodeModule.cpp
        Source/Modules/AirwindowsSmoothModule.cpp
        Source/Modules/AirwindowsSmoothEQ3Module.cpp
        Source/Modules/AirwindowsSoftGateModule.cpp
        Source/Modules/AirwindowsStoneFireCompModule.cpp
        Source/Modules/AirwindowsStonefireModule.cpp
        Source/Modules/AirwindowsSweetenModule.cpp
        Source/Modules/AirwindowsTakeCareModule.cpp
        Source/Modules/AirwindowsTapeDelay2Module.cpp
        Source/Modules/AirwindowsTapeDustModule.cpp
        Source/Modules/AirwindowsTapeHack2Module.cpp
        Source/Modules/AirwindowsToneSlantModule.cpp
        Source/Modules/AirwindowsTremoSquareModule.cpp
        Source/Modules/AirwindowsTrianglizerModule.cpp
        Source/Modules/AirwindowsTube2Module.cpp
        Source/Modules/AirwindowsVibratoModule.cpp
        Source/Modules/AirwindowsWeightModule.cpp
        Source/Modules/AirwindowsWiderModule.cpp
        Source/Modules/AirwindowsChamberModule.cpp
        Source/Modules/AirwindowsGalacticModule.cpp
        Source/Modules/AirwindowsVerbTinyModule.cpp
        Source/Modules/AirwindowsKBeyondModule.cpp
        Source/Modules/AirwindowsKCathedral5Module.cpp
        Source/Modules/AirwindowsKWoodRoomModule.cpp
        Source/Modules/AttenuatorModule.cpp
        Source/Modules/CaptureTapModule.cpp
        Source/Modules/LfoModule.cpp
        Source/Modules/LinkAudioReceiveModule.cpp
        Source/Modules/LinkAudioSendModule.cpp
        Source/Modules/ScopeModule.cpp
        Source/Modules/StepSequencerModule.cpp
        Source/UI/AudioSettingsComponent.cpp
        Source/UI/Browser/BrowserListRow.cpp
        Source/UI/Browser/BrowserPanel.cpp
        Source/UI/Browser/TouchKeyboard.cpp
        Source/UI/CaptureAllButton.cpp
        Source/UI/CapturePanel.cpp
        Source/UI/CaptureSettingsComponent.cpp
        Source/UI/CaptureToast.cpp
        Source/UI/CcControlLayer.cpp
        Source/UI/CcPanel.cpp
        Source/UI/ChannelAttributePanel.cpp
        Source/UI/ChordMemoryStrip.cpp
        Source/UI/ColourSwatchStrip.cpp
        Source/UI/ConduitColorPicker.cpp
        Source/UI/CurveEditor.cpp
        Source/UI/EditorDockPanel.cpp
        Source/UI/ExpressionRibbon.cpp
        Source/UI/FxModulePanel.cpp
        Source/UI/GainFaderMeter.cpp
        Source/UI/GridKeyboardComponent.cpp
        Source/UI/GridPage.cpp
        Source/UI/InputSendButton.cpp
        Source/UI/LevelMeterBar.cpp
        Source/UI/LinkAudioReceivePanel.cpp
        Source/UI/LinkAudioSendPanel.cpp
        Source/UI/LinkAudioStatusBadge.cpp
        Source/UI/LinkMenuPanel.cpp
        Source/UI/LinkSendCreateDialog.cpp
        Source/UI/LockToggle.cpp
        Source/UI/MpeShapingView.cpp
        Source/UI/NodeCanvas.cpp
        Source/UI/NodeComponent.cpp
        Source/UI/NoteCircleFadeTracker.cpp
        Source/UI/OscSettingsComponent.cpp
        Source/UI/LooperPage.cpp
        Source/UI/LooperWaveformStrip.cpp
        Source/UI/NodeAttributePanel.cpp
        Source/UI/NodeColourDot.cpp
        Source/UI/PageHost.cpp
        Source/UI/ParameterPanel.cpp
        Source/UI/PortComponent.cpp
        Source/UI/PushIcons.cpp
        Source/UI/PushLookAndFeel.cpp
        Source/UI/PushTiles.cpp
        Source/UI/ScopeDisplay.cpp
        Source/UI/TapMenuPanel.cpp
        Source/UI/TransportBar.cpp
        Source/UI/DevPanel.cpp
        Source/UI/SequencerControlPanel.cpp
        Source/UI/SettingsWindow.cpp
        Source/UI/StepGridDisplay.cpp
        Source/UI/UiSettingsComponent.cpp
        Source/Util/RtAllocationGuard.cpp
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
        # RT-Audit aktiv: die Stress-Tests auditieren den Input-Tap-Pfad
        $<$<CONFIG:Debug>:CONDUIT_RT_ALLOCATION_CHECKS=1>
    )

    target_link_libraries(ConduitTests
        PRIVATE
            ConduitAssets
            Catch2::Catch2WithMain
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_osc
            Ableton::Link
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    if(WIN32)
        target_link_libraries(ConduitTests PRIVATE ws2_32 iphlpapi)
    endif()

    enable_testing()
    add_test(NAME ConduitTests COMMAND ConduitTests)
endif()

# ---------------------------------------------------------------------------
# ConduitAirwindows — portierte Airwindows-DSP (Density/Slew/Spiral).
# Muss NACH der Catch2-FetchContent stehen (siehe CONDUIT_BUILD_TESTS oben),
# sonst baut Source/DSP/Airwindows/CMakeLists.txt sein konditionales
# ConduitAirwindowsTests-Target nicht mit (if(TARGET Catch2::Catch2WithMain)).
# ---------------------------------------------------------------------------
add_subdirectory(Source/DSP/Airwindows)
target_link_libraries(Conduit PRIVATE ConduitAirwindows)

if(CONDUIT_BUILD_TESTS)
    target_link_libraries(ConduitTests PRIVATE ConduitAirwindows)
endif()

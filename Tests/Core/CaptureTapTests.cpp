#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include "Core/Capture/CaptureService.h"
#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/CaptureTapModule.h"
#include "Modules/ModuleFactory.h"

using conduit::CaptureChannel;
using conduit::CaptureService;
using conduit::CaptureSettings;
using conduit::CaptureTapModule;

namespace
{

/** Eindeutiger Sample-Wert pro absoluter Position — exakt als float
    darstellbar (Werte < 2^24), Vergleich via juce::exactlyEqual. */
float rampValue (std::uint64_t position) noexcept
{
    return static_cast<float> (position % 1'000'003ull);
}

/** Unter der Gate-Schwelle (≤ −60 dBFS), Werte bleiben exakt vergleichbar —
    Begründung in CaptureBufferTests. */
constexpr float quietRampScale = 1.0f / static_cast<float> (1 << 30);

void fillRamp (float* dest, int numSamples, std::uint64_t startPosition, float scale)
{
    for (int i = 0; i < numSamples; ++i)
        dest[i] = scale * rampValue (startPosition + static_cast<std::uint64_t> (i));
}

int countRampMismatches (const CaptureChannel& channel, std::uint64_t from, std::uint64_t to,
                         float scale)
{
    const auto length = static_cast<int> (to - from);
    std::vector<float> samples (static_cast<size_t> (length));

    if (! channel.read (from, samples.data(), length))
        return -1;

    int mismatches = 0;
    for (int i = 0; i < length; ++i)
        if (! juce::exactlyEqual (samples[static_cast<size_t> (i)],
                                  scale * rampValue (from + static_cast<std::uint64_t> (i))))
            ++mismatches;
    return mismatches;
}

/** Settings-Persistenz in ein Temp-Verzeichnis statt in die echte
    Conduit.settings des Users — Verzeichnis wird im Dtor gelöscht. */
struct TempCaptureSettings
{
    TempCaptureSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitCaptureTapTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitCaptureTapTests";
        options.filenameSuffix  = ".settings";
        options.folderName      = folder.getFullPathName();
        settings = std::make_unique<CaptureSettings> (options);
    }

    ~TempCaptureSettings()
    {
        settings.reset();
        folder.deleteRecursively();
    }

    juce::File folder;
    std::unique_ptr<CaptureSettings> settings;
};

/** Prüfstand: spielt den Audio-Callback nach — erst der Input-Tap
    (Hardware-Kanäle, leise Rampe), DANACH die Tap-Schreibzugriffe
    desselben Blocks (dieselbe Reihenfolge wie EngineProcessor:
    processInputTap → graph.processBlock). */
struct TapHarness
{
    TapHarness (CaptureService& serviceToUse, int numHardwareChannels, int blockSize)
        : service (serviceToUse), buffer (numHardwareChannels, blockSize)
    {
        tapBlock.resize (static_cast<size_t> (blockSize));
    }

    /** Ein Block: Hardware leise Rampe; jedem übergebenen Handle wird
        dieselbe (leise) Rampe geschrieben. */
    void feedBlock (std::initializer_list<CaptureService::VirtualChannelHandle> handles = {})
    {
        const auto numSamples = buffer.getNumSamples();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            fillRamp (buffer.getWritePointer (ch), numSamples, position, quietRampScale);

        service.processInputTap (buffer, buffer.getNumChannels());

        fillRamp (tapBlock.data(), numSamples, position, quietRampScale);
        for (const auto handle : handles)
            service.writeVirtualChannel (handle, tapBlock.data(), numSamples);

        position += static_cast<std::uint64_t> (numSamples);
    }

    void feedBlocks (int count, std::initializer_list<CaptureService::VirtualChannelHandle> handles = {})
    {
        for (int i = 0; i < count; ++i)
            feedBlock (handles);
    }

    CaptureService& service;
    juce::AudioBuffer<float> buffer;
    std::vector<float> tapBlock;
    std::uint64_t position = 0;
};

juce::ValueTree makeRootTree()
{
    juce::ValueTree root (conduit::id::root);
    root.appendChild (juce::ValueTree (conduit::id::nodes),               nullptr);
    root.appendChild (juce::ValueTree (conduit::id::connections),         nullptr);
    root.appendChild (juce::ValueTree (conduit::id::calibrationProfiles), nullptr);
    return root;
}

} // namespace

//==============================================================================
TEST_CASE ("Virtuelle Kanäle: Registrierung, Grenzen, Slot-Reuse", "[capturetap]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service (*temp.settings);

    service.prepare (1000.0, 32, 2);  // niedrige Samplerate hält den Test klein
    REQUIRE (service.getRingNumChannels() == 2);
    REQUIRE (service.getChannel (2) == nullptr);  // noch keine Taps registriert

    // Alle Registry-Slots vergeben — der Satz wächst still mit (kein Kanal aktiv)
    std::vector<CaptureService::VirtualChannelHandle> handles;
    for (int s = 0; s < CaptureService::MAX_VIRTUAL_CHANNELS; ++s)
    {
        auto handle = service.registerVirtualChannel ("tap_" + juce::String (s));
        REQUIRE (handle.isValid());
        REQUIRE (handle.slot == s);
        handles.push_back (handle);
    }

    REQUIRE_FALSE (service.registerVirtualChannel ("ueberzaehlig").isValid());

    // Satz trägt alle Slots: Hardware [0,2), Taps [2, 10)
    REQUIRE (service.getChannel (2 + CaptureService::MAX_VIRTUAL_CHANNELS - 1) != nullptr);
    REQUIRE (service.getChannel (2 + CaptureService::MAX_VIRTUAL_CHANNELS) == nullptr);

    const auto info = service.getVirtualChannelUiInfo (3);
    REQUIRE (info.inUse);
    REQUIRE (info.name == "tap_3");
    REQUIRE (info.captureIndex == 5);

    // Idle-Slot ist nach Deregistrierung sofort wiederverwendbar
    service.unregisterVirtualChannel (handles[3]);
    REQUIRE_FALSE (handles[3].isValid());
    REQUIRE_FALSE (service.getVirtualChannelUiInfo (3).inUse);  // kein Material gebunden

    const auto reused = service.registerVirtualChannel ("tap_neu");
    REQUIRE (reused.slot == 3);
    REQUIRE (service.getVirtualChannelUiInfo (3).name == "tap_neu");

    // Rename folgt live
    service.setVirtualChannelName (reused, "tap_umbenannt");
    REQUIRE (service.getVirtualChannelUiInfo (3).name == "tap_umbenannt");

    // Geteilte Obergrenze MAX_CAPTURE_CHANNELS (pur)
    REQUIRE (CaptureService::clampedVirtualSlots (60, CaptureService::MAX_VIRTUAL_CHANNELS) == 4);
    REQUIRE (CaptureService::clampedVirtualSlots (64, CaptureService::MAX_VIRTUAL_CHANNELS) == 0);
    REQUIRE (CaptureService::clampedVirtualSlots (0, 3) == 3);
}

//==============================================================================
TEST_CASE ("Tap-Schreibpfad: Aufnahme, Deregistrierung → held, keine neuen Daten", "[capturetap]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service (*temp.settings);

    service.prepare (1000.0, 32, 1);

    auto handle = service.registerVirtualChannel ("tap_a");
    REQUIRE (handle.isValid());
    const auto tapIndex = service.getVirtualChannelUiInfo (handle.slot).captureIndex;
    REQUIRE (tapIndex == 1);

    TapHarness harness (service, 1, 32);
    harness.feedBlocks (100, { handle });  // Pre-Roll-Vorlauf, Gates bleiben zu

    // Gate über die Test-Seam öffnen, Pool bedienen → Aufnahme läuft an
    service.openGate (tapIndex);
    harness.feedBlocks (1, { handle });
    service.runRamGuard();
    harness.feedBlocks (1, { handle });

    const auto* channel = service.getChannel (tapIndex);
    REQUIRE (channel != nullptr);
    REQUIRE (channel->getState() == CaptureChannel::State::recording);

    int guard = 0;
    while (! channel->isTakeoverComplete() && guard++ < 200)
        harness.feedBlocks (1, { handle });
    REQUIRE (channel->isTakeoverComplete());

    // Tap-Daten liegen sample-genau im Ring (gleiche Rampe wie die Hardware)
    const auto range = channel->getReadableRange();
    REQUIRE (range.to == harness.position);
    REQUIRE (countRampMismatches (*channel, range.from, range.to, quietRampScale) == 0);

    // Deregistrierung (Delete Phase 1): Gate schließt im nächsten Block,
    // Material bleibt als held erhalten
    const CaptureService::VirtualChannelHandle stale { handle.slot };
    service.unregisterVirtualChannel (handle);
    REQUIRE_FALSE (handle.isValid());

    harness.feedBlocks (1);
    REQUIRE (channel->getState() == CaptureChannel::State::held);
    REQUIRE (service.isAnyChannelActive());
    REQUIRE (service.getVirtualChannelUiInfo (stale.slot).inUse);  // held bleibt sichtbar

    // Schreibversuche mit dem alten Handle prallen ab — keine neuen Daten
    const auto endBefore = channel->getEndPosition();
    harness.feedBlocks (5, { stale });
    REQUIRE (channel->getEndPosition() == endBefore);

    // Slot mit gehaltenem Material wird NICHT wiedervergeben
    const auto second = service.registerVirtualChannel ("tap_b");
    REQUIRE (second.isValid());
    REQUIRE (second.slot != stale.slot);

    // Nach der Freigabe (Export-Workflow) ist der Slot wieder frei
    service.releaseExportedHeldChannels ({ tapIndex });
    harness.feedBlocks (1);
    REQUIRE (channel->getState() == CaptureChannel::State::idle);

    const auto third = service.registerVirtualChannel ("tap_c");
    REQUIRE (third.slot == stale.slot);
}

//==============================================================================
TEST_CASE ("Tap-Slot-Erweiterung wartet auf inaktive Kanäle (kein Materialverlust)", "[capturetap]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service (*temp.settings);

    service.prepare (1000.0, 32, 1);
    const auto ringBefore = service.getRingCapacitySamples();

    TapHarness harness (service, 1, 32);
    harness.feedBlocks (50);

    // Hardware-Aufnahme läuft → die Registrierung darf den Satz NICHT tauschen
    service.openGate (0);
    harness.feedBlocks (1);
    service.runRamGuard();
    harness.feedBlocks (1);
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::recording);

    auto handle = service.registerVirtualChannel ("tap_warte");
    REQUIRE (handle.isValid());
    REQUIRE (service.getVirtualChannelUiInfo (handle.slot).captureIndex == -1);  // noch kein Puffer
    REQUIRE (service.getRingCapacitySamples() == ringBefore);                    // kein Swap

    // Schreibzugriffe sind bis zur Erweiterung stille No-ops
    harness.feedBlocks (5, { handle });
    REQUIRE (service.getChannel (0)->getState() == CaptureChannel::State::recording);

    // Aufnahme beenden und freigeben → der Guard-Tick holt die Erweiterung nach
    service.closeGate (0);
    harness.feedBlocks (1);
    service.releaseExportedHeldChannels ({ 0 });
    harness.feedBlocks (1);
    REQUIRE_FALSE (service.isAnyChannelActive());

    service.runRamGuard();
    REQUIRE (service.getVirtualChannelUiInfo (handle.slot).captureIndex == 1);

    // Audio übernimmt den erweiterten Satz, der Tap nimmt jetzt auf
    harness.feedBlocks (50, { handle });
    service.openGate (1);
    harness.feedBlocks (1, { handle });
    service.runRamGuard();
    harness.feedBlocks (1, { handle });
    REQUIRE (service.getChannel (1)->getState() == CaptureChannel::State::recording);
}

//==============================================================================
TEST_CASE ("Alignment: Hardware- und Tap-Impuls landen sample-identisch im Export", "[capturetap]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service (*temp.settings);

    const auto exportDir = temp.folder.getChildFile ("export");
    REQUIRE (exportDir.createDirectory());
    temp.settings->setExportDirectory (exportDir);
    temp.settings->setExportBitDepth (32);  // bitexakter Roundtrip

    constexpr double sampleRate = 8000.0;
    constexpr int blockSize = 64;
    service.prepare (sampleRate, blockSize, 1);

    auto handle = service.registerVirtualChannel ("tap_imp");
    REQUIRE (handle.isValid());

    juce::AudioBuffer<float> buffer (1, blockSize);
    std::vector<float> tapBlock (static_cast<size_t> (blockSize), 0.0f);
    std::uint64_t position = 0;

    const auto feedBlock = [&] (bool impulse)
    {
        buffer.clear();
        std::fill (tapBlock.begin(), tapBlock.end(), 0.0f);

        if (impulse)
        {
            buffer.setSample (0, 10, 0.5f);
            tapBlock[10] = 0.5f;
        }

        service.processInputTap (buffer, 1);
        service.writeVirtualChannel (handle, tapBlock.data(), blockSize);
        position += static_cast<std::uint64_t> (blockSize);
    };

    // Vorlauf (Pre-Roll), dann derselbe Impuls in beide Pfade im selben
    // Callback — die Gate-Detektion öffnet beide Gates in diesem Block
    for (int i = 0; i < 100; ++i)
        feedBlock (false);

    const auto impulsePosition = position + 10;
    feedBlock (true);

    const auto* hardware = service.getChannel (0);
    const auto* tap      = service.getChannel (1);
    REQUIRE (hardware != nullptr);
    REQUIRE (tap != nullptr);

    // Aufnahme anlaufen lassen, Übernahme abschließen
    int guard = 0;
    while ((hardware->getState() != CaptureChannel::State::recording
            || tap->getState() != CaptureChannel::State::recording
            || ! hardware->isTakeoverComplete() || ! tap->isTakeoverComplete())
           && guard++ < 500)
    {
        service.runRamGuard();
        feedBlock (false);
    }
    REQUIRE (hardware->isTakeoverComplete());
    REQUIRE (tap->isTakeoverComplete());

    REQUIRE (service.exportAll() == 2);

    for (int i = 0; i < 1000 && service.isExportBusy(); ++i)
        juce::Thread::sleep (10);
    REQUIRE_FALSE (service.isExportBusy());

    const auto files = exportDir.findChildFiles (juce::File::findFiles, false, "*.wav");
    REQUIRE (files.size() == 2);

    // Positionsvergleich: der Impuls muss in beiden Dateien am selben
    // Sample-Index liegen (Padding auf gemeinsamen exportStart, BWF)
    juce::WavAudioFormat wavFormat;
    std::int64_t hardwareImpulse = -1, tapImpulse = -1;
    bool sawHardwareName = false, sawTapName = false;

    for (const auto& file : files)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (
            wavFormat.createReaderFor (file.createInputStream().release(), true));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> contents (1, static_cast<int> (reader->lengthInSamples));
        REQUIRE (reader->read (&contents, 0, contents.getNumSamples(), 0, true, false));

        std::int64_t impulseIndex = -1;
        for (int i = 0; i < contents.getNumSamples(); ++i)
        {
            if (std::abs (contents.getSample (0, i)) > 0.25f)
            {
                impulseIndex = i;
                break;
            }
        }
        REQUIRE (impulseIndex >= 0);

        if (file.getFileName().contains ("in1"))
        {
            hardwareImpulse = impulseIndex;
            sawHardwareName = true;
        }
        else if (file.getFileName().contains ("tap_imp"))  // Dateiname = moduleId
        {
            tapImpulse = impulseIndex;
            sawTapName = true;
        }
    }

    REQUIRE (sawHardwareName);
    REQUIRE (sawTapName);
    REQUIRE (hardwareImpulse == tapImpulse);

    // Beide Pfade ankern bei Position 0 (Pre-Roll-Bestand) — der Impuls
    // liegt damit exakt an seiner absoluten SampleClock-Position
    REQUIRE (hardwareImpulse == static_cast<std::int64_t> (impulsePosition));
}

//==============================================================================
TEST_CASE ("BufferPool gemischt: Hardware + Taps teilen RAM-Budget und Segmente", "[capturetap]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;

    REQUIRE (temp.settings->setBufferMinutes (30) == CaptureSettings::ResizeOutcome::applied);
    temp.settings->setRamLimitGb (1);

    CaptureService service (*temp.settings);
    service.prepare (48000.0, 480, 2);

    auto tapOne = service.registerVirtualChannel ("tap_one");
    auto tapTwo = service.registerVirtualChannel ("tap_two");
    REQUIRE (tapOne.isValid());
    REQUIRE (tapTwo.isValid());

    // Geteiltes RAM-Budget: die Ring-Dimensionierung rechnet mit
    // Hardware + Taps (4 Kanäle), nicht nur mit der Hardware
    REQUIRE (service.getRingCapacitySamples()
             == CaptureService::computeRingCapacitySamples (30, 48000.0, 4, 1));

    const auto segmentBytes = static_cast<std::int64_t> (service.getRingCapacitySamples())
                            * static_cast<std::int64_t> (sizeof (float));
    const auto preRollBytes = 4 * static_cast<std::int64_t> (
                                  CaptureService::computePreRollCapacitySamples (60, 48000.0))
                            * static_cast<std::int64_t> (sizeof (float));
    const auto maxSegments = CaptureService::computeMaxSegments (1, segmentBytes,
                                                                 preRollBytes, 4);
    REQUIRE (maxSegments >= 2);
    REQUIRE (maxSegments < 4);  // sonst testet der Fall nichts

    TapHarness harness (service, 2, 480);
    harness.feedBlocks (10, { tapOne, tapTwo });

    // Kanäle gemischt öffnen (Hardware 0, Tap 1, dann Hardware 1, Tap 2 …)
    // bis alle Segmente vergeben sind — Indizes 0,1 Hardware, 2,3 Taps
    const int mixedOrder[] = { 0, 2, 1, 3 };
    for (int i = 0; i < maxSegments; ++i)
    {
        const auto index = mixedOrder[i];
        service.openGate (index);
        harness.feedBlocks (2, { tapOne, tapTwo });
        service.runRamGuard();
        harness.feedBlocks (2, { tapOne, tapTwo });
        REQUIRE (service.getChannel (index)->getState() == CaptureChannel::State::recording);
    }
    REQUIRE_FALSE (service.isRamWarningActive());

    // Ein weiterer Kanal hungert den Pool aus → Warnung
    const auto starvedIndex = mixedOrder[maxSegments];
    service.openGate (starvedIndex);
    harness.feedBlocks (1, { tapOne, tapTwo });
    service.runRamGuard();
    REQUIRE (service.isRamWarningActive());
    REQUIRE (service.getChannel (starvedIndex)->getState() == CaptureChannel::State::awaitingSegment);

    // Erster Kanal schließt → held → der Wächter gibt ihn frei, das
    // recycelte Segment bedient den wartenden Kanal (gemischter Kreislauf)
    service.closeGate (mixedOrder[0]);
    harness.feedBlocks (1, { tapOne, tapTwo });
    REQUIRE (service.getChannel (mixedOrder[0])->getState() == CaptureChannel::State::held);

    int guard = 0;
    while (service.getChannel (starvedIndex)->getState() != CaptureChannel::State::recording
           && guard++ < 50)
    {
        service.runRamGuard();
        harness.feedBlocks (2, { tapOne, tapTwo });
    }
    REQUIRE (service.getChannel (starvedIndex)->getState() == CaptureChannel::State::recording);
}

//==============================================================================
TEST_CASE ("CaptureTapModule: Pass-Through, Lifecycle über den GraphManager", "[capturetap]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };

    conduit::registerDefaultModules (factory);

    TempCaptureSettings temp;
    CaptureService service (*temp.settings);
    service.prepare (1000.0, 32, 1);
    manager.setCaptureService (&service);

    // Materialisierung: Kontext-Injektion VOR prepareForGraph, Spurname == moduleId
    const auto nodeTree = manager.addModuleNode (CaptureTapModule::staticModuleId, { 100, 100 });
    REQUIRE (nodeTree.isValid());
    const auto nodeUuid = nodeTree.getProperty (conduit::id::nodeId).toString();
    manager.flushPendingTopologyUpdate();

    auto* module = dynamic_cast<CaptureTapModule*> (manager.getModuleFor (nodeUuid));
    REQUIRE (module != nullptr);
    REQUIRE (module->isTapRegistered());

    const auto moduleId = nodeTree.getProperty (conduit::id::moduleId).toString();
    REQUIRE (service.getVirtualChannelUiInfo (0).name
             == CaptureTapModule::channelNameFor (moduleId, 0));
    REQUIRE (service.getVirtualChannelUiInfo (1).name
             == CaptureTapModule::channelNameFor (moduleId, 1));

    // Pass-Through: der Buffer bleibt bitidentisch
    juce::AudioBuffer<float> hardwareBuffer (1, 32);
    hardwareBuffer.clear();
    service.processInputTap (hardwareBuffer, 1);  // SampleClock muss ticken

    juce::AudioBuffer<float> moduleBuffer (2, 32);
    for (int ch = 0; ch < 2; ++ch)
        fillRamp (moduleBuffer.getWritePointer (ch), 32,
                  static_cast<std::uint64_t> (ch) * 1000, quietRampScale);

    juce::AudioBuffer<float> expected;
    expected.makeCopyOf (moduleBuffer);

    juce::MidiBuffer midi;
    module->processBlock (moduleBuffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 32; ++i)
            REQUIRE (juce::exactlyEqual (moduleBuffer.getSample (ch, i),
                                         expected.getSample (ch, i)));

    // Rename (auch via Undo-Pfad): Spurnamen folgen live
    REQUIRE (manager.renameNode (nodeUuid, "Drum Bus"));
    REQUIRE (service.getVirtualChannelUiInfo (0).name == "drum_bus_l");
    REQUIRE (service.getVirtualChannelUiInfo (1).name == "drum_bus_r");

    // Delete Phase 1: Kanäle sofort deregistriert (Pattern OscController)
    REQUIRE (manager.requestNodeDelete (nodeUuid));
    REQUIRE_FALSE (module->isTapRegistered());
    REQUIRE_FALSE (service.getVirtualChannelUiInfo (0).inUse);  // kein Material gebunden
    REQUIRE_FALSE (service.getVirtualChannelUiInfo (1).inUse);

    // Phase 2 räumt den Node ab — die Slots sind wieder vergebbar
    manager.flushPendingTopologyUpdate();
    REQUIRE (manager.getModuleFor (nodeUuid) == nullptr);

    const auto reused = service.registerVirtualChannel ("frei");
    REQUIRE (reused.slot == 0);
}

//==============================================================================
TEST_CASE ("CaptureTapModule: volle Registry → nodeError, Destruktor räumt ohne Phase 1", "[capturetap]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempCaptureSettings temp;
    CaptureService service (*temp.settings);
    service.prepare (1000.0, 32, 1);

    // Destruktor ohne Phase 1 (Preset-Load/Shutdown): Slots werden frei
    {
        CaptureTapModule module;
        module.setCaptureTapContext (&service, "kurzlebig");
        REQUIRE (module.prepareForGraph (1000.0, 32).wasOk());
        REQUIRE (module.isTapRegistered());

        // Idempotent (5.2 Schritt 1): erneutes Prepare registriert nicht doppelt
        REQUIRE (module.prepareForGraph (1000.0, 32).wasOk());
        REQUIRE (service.getVirtualChannelUiInfo (2).inUse == false);
    }
    REQUIRE_FALSE (service.getVirtualChannelUiInfo (0).inUse);
    REQUIRE_FALSE (service.getVirtualChannelUiInfo (1).inUse);

    // Registry komplett belegen → prepareForGraph schlägt sauber fehl
    std::vector<CaptureService::VirtualChannelHandle> blockers;
    for (int s = 0; s < CaptureService::MAX_VIRTUAL_CHANNELS; ++s)
        blockers.push_back (service.registerVirtualChannel ("blocker_" + juce::String (s)));

    CaptureTapModule overflow;
    overflow.setCaptureTapContext (&service, "zu_viel");
    const auto result = overflow.prepareForGraph (1000.0, 32);
    REQUIRE (result.failed());
    REQUIRE_FALSE (overflow.isTapRegistered());

    // Ohne Service-Kontext (Tests/kein Capture): reines Pass-Through, kein Fehler
    CaptureTapModule detached;
    detached.setCaptureTapContext (nullptr, "ohne_service");
    REQUIRE (detached.prepareForGraph (1000.0, 32).wasOk());
    REQUIRE_FALSE (detached.isTapRegistered());
}

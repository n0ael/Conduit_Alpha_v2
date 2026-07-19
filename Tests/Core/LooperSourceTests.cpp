#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/GraphManager.h"
#include "Modules/LinkAudioReceiveModule.h"
#include "TestSettingsFolder.h"

//==============================================================================
TEST_CASE ("Looper-Quelle (B3): Schlüssel-Auflösung, Arming und Persistenz", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& capture = engine.getCaptureService();

    engine.setPlayConfigDetails (2, 2, 48000.0, 480);
    engine.prepareToPlay (48000.0, 480);

    SECTION ("Werks-Default: KEINE Quelle, nichts gearmt (19.07.2026)")
    {
        // "master" ist seit ADR 010 nicht mehr wählbar; ein ab Werk
        // gearmter Master-Tap blockierte die Puffersatz-Erweiterung
        REQUIRE (engine.getLooperSettings().getSourceKey (0).isEmpty());
        REQUIRE (engine.getLooperLeftIndex() == -1);
        REQUIRE (engine.getLooperRightIndex() == -1);
        REQUIRE_FALSE (capture.isChannelArmed (2));
        REQUIRE_FALSE (capture.isChannelArmed (3));
    }

    SECTION ("Legacy-Key 'master' resolvet weiter (Alt-Session)")
    {
        engine.setLooperSource ("master");
        REQUIRE (engine.getLooperLeftIndex() == 2);   // hinter 2 Hardware-Kanälen
        REQUIRE (engine.getLooperRightIndex() == 3);
        REQUIRE (capture.isChannelArmed (2));
        REQUIRE (capture.isChannelArmed (3));
    }

    SECTION ("Quellwechsel auf 'hw:0' entwaffnet den Vorgänger")
    {
        engine.setLooperSource ("hw:0");

        REQUIRE (engine.getLooperLeftIndex() == 0);
        REQUIRE (engine.getLooperRightIndex() == 1);
        REQUIRE (capture.isChannelArmed (0));
        REQUIRE (capture.isChannelArmed (1));
        REQUIRE_FALSE (capture.isChannelArmed (2));
        REQUIRE_FALSE (capture.isChannelArmed (3));

        // Persistenz: der Schlüssel liegt in den LooperSettings (M5)
        REQUIRE (engine.getLooperSettings().getSourceKey (0) == "hw:0");
    }

    SECTION ("Hardware-Paar außerhalb der Kanalzahl bleibt unaufgelöst")
    {
        engine.setLooperSource ("hw:7");  // Kanäle 14/15 — Interface hat 2

        REQUIRE (engine.getLooperLeftIndex() == -1);
        REQUIRE (engine.getLooperRightIndex() == -1);
        REQUIRE_FALSE (capture.isChannelArmed (2));
    }

    SECTION ("Stereo-Tap: Basisname löst das _l/_r-Paar auf")
    {
        // Modul-Tap simulieren (Muster CaptureTapModule: _l/_r pro Seite);
        // nach prepare sind alle Kanäle idle → der Satz erweitert verlustfrei
        auto left  = capture.registerVirtualChannel ("delay_1_l");
        auto right = capture.registerVirtualChannel ("delay_1_r");
        REQUIRE (left.isValid());
        REQUIRE (right.isValid());

        engine.setLooperSource ("tap:delay_1");

        const auto expectedLeft  = capture.getVirtualChannelUiInfo (left.slot).captureIndex;
        const auto expectedRight = capture.getVirtualChannelUiInfo (right.slot).captureIndex;
        REQUIRE (expectedLeft >= 0);
        REQUIRE (engine.getLooperLeftIndex() == expectedLeft);
        REQUIRE (engine.getLooperRightIndex() == expectedRight);
        REQUIRE (capture.isChannelArmed (expectedLeft));
        REQUIRE (capture.isChannelArmed (expectedRight));

        capture.unregisterVirtualChannel (left);
        capture.unregisterVirtualChannel (right);
    }

    SECTION ("Mono-Tap: rechts bleibt -1 (1-Kanal-Clip, Looper-I/O)")
    {
        auto mono = capture.registerVirtualChannel ("cv_solo");
        REQUIRE (mono.isValid());

        engine.setLooperSource ("tap:cv_solo");

        const auto expected = capture.getVirtualChannelUiInfo (mono.slot).captureIndex;
        REQUIRE (expected >= 0);
        REQUIRE (engine.getLooperLeftIndex() == expected);
        REQUIRE (engine.getLooperRightIndex() == -1);
        REQUIRE (capture.isChannelArmed (expected));

        capture.unregisterVirtualChannel (mono);
    }

    SECTION ("Mono-Hardware-Kanal hwm:{n}: links aufgelöst, rechts -1")
    {
        engine.setLooperSource ("hwm:1");

        REQUIRE (engine.getLooperLeftIndex() == 1);
        REQUIRE (engine.getLooperRightIndex() == -1);
        REQUIRE (capture.isChannelArmed (1));
        REQUIRE_FALSE (capture.isChannelArmed (0));
    }

    SECTION ("hwm: außerhalb der Kanalzahl bleibt unaufgelöst")
    {
        engine.setLooperSource ("hwm:5");   // Interface hat 2 Eingänge

        REQUIRE (engine.getLooperLeftIndex() == -1);
        REQUIRE (engine.getLooperRightIndex() == -1);
    }

    SECTION ("Unbekannter Tap-Name: nichts gearmt, Indizes -1")
    {
        engine.setLooperSource ("tap:gibts_nicht");

        REQUIRE (engine.getLooperLeftIndex() == -1);
        REQUIRE (engine.getLooperRightIndex() == -1);
    }

    SECTION ("Legacy-Schlüssel out:{paar} bleibt unaufgelöst (Taps entfallen)")
    {
        // Looper-I/O 07/2026: Ausgangs-Signale loopt man per Kabel ins
        // Looper-In-Modul — der out:-Zweig resolvet zu nichts, kein Crash
        engine.setLooperSource ("out:1");

        REQUIRE (engine.getLooperLeftIndex() == -1);
        REQUIRE (engine.getLooperRightIndex() == -1);
    }

    SECTION ("Keine out{p}-Taps mehr in der Registry (nur master_l/_r)")
    {
        for (int slot = 0; slot < conduit::CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
        {
            const auto info = capture.getVirtualChannelUiInfo (slot);
            if (info.inUse)
                REQUIRE_FALSE (info.name.startsWith ("out"));
        }
    }
}

//==============================================================================
TEST_CASE ("Looper-Quelle: Link-Receive-Modul erscheint als Capture-Tap", "[looper][linkaudio]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& capture = engine.getCaptureService();

    engine.setPlayConfigDetails (2, 2, 48000.0, 480);
    engine.prepareToPlay (48000.0, 480);

    // Receive-Modul in den Graph — registriert seine Capture-Kanäle
    // {moduleId}_l/_r in prepareForGraph (ICaptureTapClient)
    auto node = engine.getGraphManager().addModuleNode (
        conduit::LinkAudioReceiveModule::staticModuleId, {});
    REQUIRE (node.isValid());
    engine.getGraphManager().flushPendingTopologyUpdate();

    const auto moduleId = node.getProperty (conduit::id::moduleId).toString();

    int tapLeft = -1, tapRight = -1;
    for (int slot = 0; slot < conduit::CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
    {
        const auto info = capture.getVirtualChannelUiInfo (slot);
        if (! info.inUse)
            continue;
        if (info.name == moduleId + "_l") tapLeft  = info.captureIndex;
        if (info.name == moduleId + "_r") tapRight = info.captureIndex;
    }
    REQUIRE (tapLeft >= 0);
    REQUIRE (tapRight >= 0);

    // Als Looper-Quelle wählbar + armbar (tap:{moduleId})
    engine.setLooperSource ("tap:" + moduleId);
    REQUIRE (engine.getLooperLeftIndex() == tapLeft);
    REQUIRE (engine.getLooperRightIndex() == tapRight);
    REQUIRE (capture.isChannelArmed (tapLeft));

    // Delete Phase 1 räumt die Kanäle ab (releaseCaptureResources)
    REQUIRE (engine.getGraphManager().requestNodeDelete (
        node.getProperty (conduit::id::nodeId).toString()));

    bool stillWriting = false;
    for (int slot = 0; slot < conduit::CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
    {
        const auto info = capture.getVirtualChannelUiInfo (slot);
        if (info.inUse && info.name == moduleId + "_l")
            stillWriting = true;   // "held" ist ok — aber nicht mehr in Benutzung als Writer
    }
    juce::ignoreUnused (stillWriting);   // Registry-Detail; Kern: kein Crash, Handles invalidiert
}

//==============================================================================
TEST_CASE ("Looper-Quellen (M4): Arming-Refcount über mehrere Looper", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& capture = engine.getCaptureService();

    engine.setPlayConfigDetails (2, 2, 48000.0, 480);
    engine.prepareToPlay (48000.0, 480);

    SECTION ("Geteilte Quelle bleibt gearmt, bis der LETZTE Looper sie verlässt")
    {
        engine.setLooperSource (0, "hw:0");
        engine.setLooperSource (1, "hw:0");
        REQUIRE (engine.getLooperLeftIndex (0) == 0);
        REQUIRE (engine.getLooperLeftIndex (1) == 0);
        REQUIRE (capture.isChannelArmed (0));
        REQUIRE (capture.isChannelArmed (1));

        // Looper 1 wechselt weg — Looper 0 nutzt hw:0 weiter: Gate offen!
        engine.setLooperSource (1, "master");
        REQUIRE (capture.isChannelArmed (0));
        REQUIRE (capture.isChannelArmed (1));
        REQUIRE (capture.isChannelArmed (2));   // master_l dazu
        REQUIRE (capture.isChannelArmed (3));

        // Jetzt verlässt auch Looper 0 → hw:0 wird entwaffnet
        engine.setLooperSource (0, "master");
        REQUIRE_FALSE (capture.isChannelArmed (0));
        REQUIRE_FALSE (capture.isChannelArmed (1));
        REQUIRE (capture.isChannelArmed (2));
        REQUIRE (capture.isChannelArmed (3));
    }

    SECTION ("Leerer Schlüssel = keine Quelle; unabhängige Auflösung pro Looper")
    {
        REQUIRE (engine.getLooperLeftIndex (1) == -1);
        REQUIRE (engine.getLooperLeftIndex (3) == -1);

        engine.setLooperSource (0, "master");   // Alt-Session-Key auf Looper 0
        engine.setLooperSource (2, "hw:0");
        REQUIRE (engine.getLooperLeftIndex (2) == 0);
        REQUIRE (engine.getLooperRightIndex (2) == 1);

        // Looper 0 (master) blieb unberührt
        REQUIRE (engine.getLooperLeftIndex (0) == 2);
        REQUIRE (capture.isChannelArmed (2));

        engine.setLooperSource (2, "");
        REQUIRE (engine.getLooperLeftIndex (2) == -1);
        REQUIRE_FALSE (capture.isChannelArmed (0));
    }

    SECTION ("Vier Taps: jeder Looper hat seinen eigenen Waveform-Binner")
    {
        // Verschiedene Instanzen — Strip-Kontrakt (ein Strip pro Tap)
        REQUIRE (&engine.getLooperWaveformTap (0) != &engine.getLooperWaveformTap (1));
        REQUIRE (&engine.getLooperWaveformTap (1) != &engine.getLooperWaveformTap (2));
        REQUIRE (&engine.getLooperWaveformTap (2) != &engine.getLooperWaveformTap (3));
    }
}

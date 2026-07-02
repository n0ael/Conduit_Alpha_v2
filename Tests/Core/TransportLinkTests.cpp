#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/LinkClock.h"
#include "Core/TransportSettings.h"

namespace
{

struct TempTransportSettings
{
    TempTransportSettings()
    {
        folder.createDirectory();
    }

    ~TempTransportSettings() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ConduitTransportSettingsTests";
        o.filenameSuffix  = ".settings";
        o.folderName      = folder.getFullPathName();  // absoluter Pfad
        return o;
    }

    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ConduitTransportSettingsTests");
};

} // namespace

//==============================================================================
TEST_CASE ("TransportSettings: Defaults, Clamping und Persistenz-Roundtrip", "[transport]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempTransportSettings temp;

    {
        conduit::TransportSettings settings (temp.options());

        // Defaults
        REQUIRE (settings.isStartStopSyncEnabled());
        REQUIRE (settings.getClockOffsetMs() == Catch::Approx (0.0));
        REQUIRE_FALSE (settings.isAutomateEnabled());
        REQUIRE_FALSE (settings.isFixedLengthEnabled());

        settings.setStartStopSyncEnabled (false);
        settings.setClockOffsetMs (12.5);
        settings.setAutomateEnabled (true);
        settings.setFixedLengthEnabled (true);

        // Clamping auf ±maxClockOffsetMs
        settings.setClockOffsetMs (5000.0);
        REQUIRE (settings.getClockOffsetMs()
                 == Catch::Approx (conduit::TransportSettings::maxClockOffsetMs));
        settings.setClockOffsetMs (-31.0);
    }

    // Zweite Instanz liest dieselbe Datei
    conduit::TransportSettings reloaded (temp.options());
    REQUIRE_FALSE (reloaded.isStartStopSyncEnabled());
    REQUIRE (reloaded.getClockOffsetMs() == Catch::Approx (-31.0));
    REQUIRE (reloaded.isAutomateEnabled());
    REQUIRE (reloaded.isFixedLengthEnabled());
}

TEST_CASE ("TransportSettings: ChangeBroadcast bei jeder Änderung", "[transport]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempTransportSettings temp;
    conduit::TransportSettings settings (temp.options());

    struct Counter : juce::ChangeListener
    {
        int count = 0;
        void changeListenerCallback (juce::ChangeBroadcaster*) override { ++count; }
    } counter;

    settings.addChangeListener (&counter);
    settings.setClockOffsetMs (3.0);
    settings.setClockOffsetMs (3.0);  // idempotent — kein zweiter Broadcast
    settings.dispatchPendingMessages();
    settings.removeChangeListener (&counter);

    REQUIRE (counter.count == 1);
}

//==============================================================================
TEST_CASE ("LinkClock: Clock-Offset verschiebt die Beat-Ablesung", "[transport][link]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTransportTest");
    clock.prepare (48000.0);

    const auto before = clock.captureClockState (64);
    clock.setClockOffsetMs (100.0);
    const auto after = clock.captureClockState (64);

    // Erwartete Verschiebung: 100 ms in Beats beim aktuellen Session-Tempo;
    // zwischen den Captures vergeht zusätzlich echte (kleine) Zeit
    const auto offsetBeats = 0.1 * before.bpm / 60.0;
    const auto delta = after.beatAtBlockStart - before.beatAtBlockStart;

    REQUIRE (delta >= offsetBeats - 0.02);
    REQUIRE (delta <= offsetBeats + 0.25);

    // Clamping ±100 ms
    clock.setClockOffsetMs (999.0);
    REQUIRE (clock.getClockOffsetMs() == Catch::Approx (100.0));
    clock.setClockOffsetMs (0.0);
}

TEST_CASE ("LinkClock: Start/Stop lokal (Sync deaktiviert — Peers bleiben unberührt)",
           "[transport][link]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTransportTest");
    clock.prepare (48000.0);

    // Sync AUS, damit der Test nie den Transport einer echten Session
    // (z. B. Ableton des Users) startet
    clock.setStartStopSyncEnabled (false);
    REQUIRE_FALSE (clock.isStartStopSyncEnabled());

    const auto initial = clock.isPlaying();

    clock.requestIsPlaying (true);
    REQUIRE (clock.isPlaying());
    REQUIRE (clock.captureClockState (64).isPlaying);

    clock.requestIsPlaying (false);
    REQUIRE_FALSE (clock.isPlaying());
    REQUIRE_FALSE (clock.captureClockState (64).isPlaying);

    // Ursprungszustand herstellen, DANN Sync wieder aktivieren
    clock.requestIsPlaying (initial);
    clock.setStartStopSyncEnabled (true);
    REQUIRE (clock.isStartStopSyncEnabled());
}

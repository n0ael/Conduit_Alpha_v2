#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/LinkClock.h"
#include "Core/TapTempo.h"
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
        REQUIRE_FALSE (settings.isTapAutoCommitEnabled());
        REQUIRE (settings.getTapResetHoldSeconds() == Catch::Approx (1.0));

        settings.setStartStopSyncEnabled (false);
        settings.setClockOffsetMs (12.5);
        settings.setAutomateEnabled (true);
        settings.setFixedLengthEnabled (true);
        settings.setTapAutoCommitEnabled (true);

        // Clamping auf ±maxClockOffsetMs
        settings.setClockOffsetMs (5000.0);
        REQUIRE (settings.getClockOffsetMs()
                 == Catch::Approx (conduit::TransportSettings::maxClockOffsetMs));
        settings.setClockOffsetMs (-31.0);

        // Reset-Haltedauer: Clamp 0.3..3.0
        settings.setTapResetHoldSeconds (99.0);
        REQUIRE (settings.getTapResetHoldSeconds() == Catch::Approx (3.0));
        settings.setTapResetHoldSeconds (0.5);
    }

    // Zweite Instanz liest dieselbe Datei
    conduit::TransportSettings reloaded (temp.options());
    REQUIRE_FALSE (reloaded.isStartStopSyncEnabled());
    REQUIRE (reloaded.getClockOffsetMs() == Catch::Approx (-31.0));
    REQUIRE (reloaded.isAutomateEnabled());
    REQUIRE (reloaded.isFixedLengthEnabled());
    REQUIRE (reloaded.isTapAutoCommitEnabled());
    REQUIRE (reloaded.getTapResetHoldSeconds() == Catch::Approx (0.5));
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
TEST_CASE ("TapTempo: endloses Tappen — Preview ab Tap 2, ohne Auto-Commit nie committed", "[transport]")
{
    conduit::TapTempo tap;

    auto result = tap.tap (10.0);
    REQUIRE_FALSE (result.hasPreview);

    // 120 BPM (0,5-s-Raster): Preview ab dem ersten Intervall, Session-Commit
    // ist NICHT Sache der Messung (M4L-Modell: Set-Klick committet)
    for (int i = 1; i <= 20; ++i)
    {
        result = tap.tap (10.0 + i * 0.5);
        REQUIRE (result.hasPreview);
        REQUIRE_FALSE (result.committed);
    }

    REQUIRE (result.previewBpm == Catch::Approx (120.0));
    REQUIRE (tap.hasPreview());
    REQUIRE (tap.getPreviewBpm() == Catch::Approx (120.0));
}

TEST_CASE ("TapTempo: Pause verwirft nur das Riesen-Intervall, die Messung bleibt", "[transport]")
{
    conduit::TapTempo tap;

    // Messung bei 120 BPM …
    tap.tap (0.0);
    tap.tap (0.5);
    tap.tap (1.0);
    REQUIRE (tap.getPreviewBpm() == Catch::Approx (120.0));

    // … lange Pause (> maxIntervalSeconds): kein Reset, Intervall wird verworfen
    const auto afterPause = tap.tap (30.0);
    REQUIRE (afterPause.hasPreview);
    REQUIRE (afterPause.previewBpm == Catch::Approx (120.0));

    // Weitertappen verfeinert die bestehende Messung
    const auto next = tap.tap (30.5);
    REQUIRE (next.previewBpm == Catch::Approx (120.0));
}

TEST_CASE ("TapTempo: rollierendes Fenster folgt einem Tempowechsel", "[transport]")
{
    conduit::TapTempo tap;

    // 8 Intervalle bei 120 BPM füllen das Fenster
    for (int i = 0; i <= 8; ++i)
        tap.tap (i * 0.5);

    REQUIRE (tap.getPreviewBpm() == Catch::Approx (120.0));

    // 8 schnellere Intervalle (0,4 s = 150 BPM) verdrängen die alten komplett
    auto time = 4.0;
    for (int i = 0; i < 8; ++i)
    {
        time += 0.4;
        tap.tap (time);
    }

    REQUIRE (tap.getPreviewBpm() == Catch::Approx (150.0));
}

TEST_CASE ("TapTempo: Median ist robust gegen einen verrissenen Tap", "[transport]")
{
    conduit::TapTempo tap;

    tap.tap (0.0);
    tap.tap (0.5);
    tap.tap (1.0);
    tap.tap (1.65);              // verrissen (0,65 s statt 0,5 s)
    const auto result = tap.tap (2.15);

    // Median der Intervalle {0.5, 0.5, 0.65, 0.5} = 0.5 → 120 BPM
    REQUIRE (result.previewBpm == Catch::Approx (120.0));
}

TEST_CASE ("TapTempo: Auto-Commit committet ab Tap n jeden weiteren Tap", "[transport]")
{
    conduit::TapTempo tap;
    tap.setAutoCommit (true, 4);

    // Taps 1–3: nur Preview
    REQUIRE_FALSE (tap.tap (0.0).committed);
    REQUIRE_FALSE (tap.tap (0.5).committed);
    REQUIRE_FALSE (tap.tap (1.0).committed);

    // Ab Tap 4 committet jeder Tap das verfeinerte Tempo
    auto result = tap.tap (1.5);
    REQUIRE (result.committed);
    REQUIRE (result.previewBpm == Catch::Approx (120.0));

    result = tap.tap (2.0);
    REQUIRE (result.committed);
}

TEST_CASE ("TapTempo: reset leert die Messung", "[transport]")
{
    conduit::TapTempo tap;

    tap.tap (0.0);
    tap.tap (0.5);
    REQUIRE (tap.hasPreview());

    tap.reset();
    REQUIRE_FALSE (tap.hasPreview());
    REQUIRE (tap.getPreviewBpm() == Catch::Approx (0.0));

    // Erster Tap nach dem Reset startet eine frische Messung
    REQUIRE_FALSE (tap.tap (10.0).hasPreview);
    REQUIRE (tap.tap (10.6).previewBpm == Catch::Approx (100.0));
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

//==============================================================================
TEST_CASE ("TransportSettings: Looper-Quelle und -Anker (B3) — Roundtrip + Clamp", "[transport][looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempTransportSettings temp;

    {
        conduit::TransportSettings settings (temp.options());

        // Defaults
        REQUIRE (settings.getLooperSource() == "master");
        REQUIRE (settings.getLooperAnchor() == 0);

        settings.setLooperSource ("tap:delay_1");
        settings.setLooperAnchor (3);

        // Leerer Schlüssel fällt definiert auf "master" zurück
        settings.setLooperSource ("");
        REQUIRE (settings.getLooperSource() == "master");
        settings.setLooperSource ("hw:1");

        // Anker-Clamp 0..31
        settings.setLooperAnchor (99);
        REQUIRE (settings.getLooperAnchor() == 31);
        settings.setLooperAnchor (2);
    }

    // Zweite Instanz liest dieselbe Datei
    conduit::TransportSettings reloaded (temp.options());
    REQUIRE (reloaded.getLooperSource() == "hw:1");
    REQUIRE (reloaded.getLooperAnchor() == 2);
}

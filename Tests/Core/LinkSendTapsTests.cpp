#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/LinkSendTaps.h"

//==============================================================================
TEST_CASE ("LinkSendTaps: Tap-Lifecycle — Refcount, Retire-Handshake, Pool-Reuse", "[linkaudio][sendtaps]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    conduit::LinkSendTaps taps;
    taps.setLinkClock (&clock);
    taps.prepare (32);

    REQUIRE_FALSE (clock.isAudioEnabled());

    //==========================================================================
    // Erster Tap aktiviert den enableAudio-Refcount, Kanal-Name announcet
    auto* tapA = taps.createTap ("audio_in/kick", 1);
    REQUIRE (tapA != nullptr);
    REQUIRE (tapA->isActive());
    REQUIRE (tapA->getSinkName() == "audio_in/kick");
    REQUIRE (tapA->getStatus() == conduit::LinkSendTaps::Status::announced);
    REQUIRE (clock.isAudioEnabled());
    REQUIRE (taps.getNumActiveTaps() == 1);

    auto* tapB = taps.createTap ("audio_in/pad", 2);
    REQUIRE (tapB != nullptr);
    REQUIRE (tapB != tapA);
    REQUIRE (taps.getNumActiveTaps() == 2);

    // Rename am lebenden Sink — Kanal-Identität bleibt (Kernanforderung 7.2)
    tapA->setName ("audio_in/kick_left");
    REQUIRE (tapA->getSinkName() == "audio_in/kick_left");
    REQUIRE (tapA->isActive());

    //==========================================================================
    // Retire Phase 1: Audio sofort getrennt, Sink in die Retire-Liste;
    // Audio bleibt an (tapB lebt)
    taps.retireTap (tapA);
    REQUIRE_FALSE (tapA->isActive());
    REQUIRE (tapA->getStatus() == conduit::LinkSendTaps::Status::offline);
    REQUIRE (taps.isRetirePending());
    REQUIRE (clock.isAudioEnabled());
    REQUIRE (taps.getNumActiveTaps() == 1);

    // Audio-Thread-Surrogat: ein Block nach dem Store → Handshake erfüllt
    taps.noteBlockBegin();
    taps.flushPendingRetirement();
    REQUIRE_FALSE (taps.isRetirePending());

    // commit/noteIdle auf dem retirten Tap sind harmlos (rtSink == nullptr)
    std::vector<float> silence (32, 0.0f);
    const float* chans[2] = { silence.data(), silence.data() };
    tapA->commit (chans, 32, {});
    tapA->noteIdle();
    REQUIRE (tapA->getStatus() == conduit::LinkSendTaps::Status::offline);

    //==========================================================================
    // Pool-Reuse: der inaktive Eintrag wird reaktiviert (stabile Adresse)
    auto* tapC = taps.createTap ("audio_in/snare", 1);
    REQUIRE (tapC == tapA);
    REQUIRE (tapC->isActive());
    REQUIRE (tapC->getSinkName() == "audio_in/snare");
    REQUIRE (taps.getNumActiveTaps() == 2);

    //==========================================================================
    // retireAll: letzter aktiver Tap gibt den Refcount frei
    taps.retireAll();
    REQUIRE (taps.getNumActiveTaps() == 0);
    REQUIRE_FALSE (clock.isAudioEnabled());

    taps.noteBlockBegin();
    taps.flushPendingRetirement();
    REQUIRE_FALSE (taps.isRetirePending());
}

//==============================================================================
TEST_CASE ("LinkSendTaps: Breiten-Umschaltung am LEBENDEN Sink (mono ↔ stereo)", "[linkaudio][sendtaps]")
{
    // Kernanforderung: Mono/Stereo-Wechsel darf den Sink NICHT neu anlegen —
    // die Kapazität (block × 2) trägt beides, commit nimmt numChannels pro
    // Commit (LinkAudio.hpp).
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    conduit::LinkSendTaps taps;
    taps.setLinkClock (&clock);
    taps.prepare (32);

    auto* tap = taps.createTap ("audio_in/main", 1);
    REQUIRE (tap != nullptr);
    REQUIRE (tap->getWidth() == 1);
    REQUIRE (tap->getSinkCapacity() == 64);   // block × 2 SAMPLES — trägt stereo

    std::vector<float> left  (32,  0.25f);
    std::vector<float> right (32, -0.25f);
    const float* chans[2] = { left.data(), right.data() };

    // Mono-Commit (ohne Subscriber: noBuffer → announced, nie rejected/Crash)
    taps.noteBlockBegin();
    const auto state = clock.captureClockState (32);
    tap->commit (chans, 32, state);
    REQUIRE (tap->getStatus() == conduit::LinkSendTaps::Status::announced);

    // Breitenwechsel am lebenden Sink: gleiche Sink-Identität, kein Retire
    const auto nameBefore = tap->getSinkName();
    tap->setWidth (2);
    REQUIRE (tap->getWidth() == 2);
    REQUIRE (tap->isActive());
    REQUIRE (tap->getSinkName() == nameBefore);
    REQUIRE_FALSE (taps.isRetirePending());

    taps.noteBlockBegin();
    const auto state2 = clock.captureClockState (32);
    tap->commit (chans, 32, state2);   // 64 Samples interleaved — passt exakt
    REQUIRE (tap->getStatus() == conduit::LinkSendTaps::Status::announced);

    // Zurück auf mono — weiterhin derselbe Sink
    tap->setWidth (1);
    REQUIRE (tap->getWidth() == 1);
    REQUIRE (tap->isActive());
    REQUIRE (tap->getSinkName() == nameBefore);

    // Breite wird geklemmt (1..2)
    tap->setWidth (7);
    REQUIRE (tap->getWidth() == 2);
    tap->setWidth (0);
    REQUIRE (tap->getWidth() == 1);
}

//==============================================================================
TEST_CASE ("LinkSendTaps: prepare wächst Kapazitäten (wächst-nur), ohne Clock kein Tap", "[linkaudio][sendtaps]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    // Ohne LinkClock (Tests/Standalone): createTap liefert nullptr, kein Crash
    {
        conduit::LinkSendTaps offline;
        offline.prepare (32);
        REQUIRE (offline.createTap ("nirgendwo", 2) == nullptr);
        REQUIRE (offline.getNumActiveTaps() == 0);
        offline.noteBlockBegin();
        offline.retireAll();   // no-op
        REQUIRE_FALSE (offline.isRetirePending());
    }

    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    conduit::LinkSendTaps taps;
    taps.setLinkClock (&clock);
    taps.prepare (32);

    auto* tap = taps.createTap ("audio_in/grow", 1);
    REQUIRE (tap != nullptr);
    REQUIRE (tap->getSinkCapacity() == 64);

    // Re-Prepare mit größerem Block hebt die Kapazität an …
    taps.prepare (64);
    REQUIRE (tap->getSinkCapacity() == 128);

    // … kleinerer Block schrumpft den Sink nicht (Link-No-op, 7.2)
    taps.prepare (32);
    REQUIRE (tap->getSinkCapacity() == 128);

    // Über-großer Block ohne Re-Prepare: kein Commit (announced), kein Überlauf
    std::vector<float> big (256, 0.1f);
    const float* chans[2] = { big.data(), big.data() };
    taps.noteBlockBegin();
    const auto state = clock.captureClockState (32);
    tap->setWidth (2);
    tap->commit (chans, 256, state);   // 512 Samples > Buffer (64) → announced
    REQUIRE (tap->getStatus() == conduit::LinkSendTaps::Status::announced);
}

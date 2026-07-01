#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Core/InputLinkSend.h"

namespace
{

/** ChannelNames mit Temp-Persistenz (Muster ChannelNamesTests). */
struct TempNames
{
    TempNames()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitInputLinkSendTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitInputLinkSendTests";
        options.filenameSuffix  = ".settings";
        options.folderName      = folder.getFullPathName();
        names = std::make_unique<conduit::ChannelNames> (options);
    }

    ~TempNames()
    {
        names.reset();
        folder.deleteRecursively();
    }

    juce::File folder;
    std::unique_ptr<conduit::ChannelNames> names;
};

using Direction = conduit::ChannelNames::Direction;
using Status    = conduit::LinkSendTaps::Status;

} // namespace

//==============================================================================
TEST_CASE ("InputLinkSend::buildSpecs: Enable + Pairing → Anker-Specs", "[inputsend]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempNames temp;
    auto& names = *temp.names;
    names.setActiveDevice ("TestDev", { "Kick", "Snare", "OH L", "OH R" }, {});

    // Nichts aktiviert → keine Specs
    REQUIRE (conduit::InputLinkSend::buildSpecs (names, 4).empty());

    // Mono-Send auf Kanal 1
    names.setPortLinkSendEnabled (Direction::input, 1, true);
    auto specs = conduit::InputLinkSend::buildSpecs (names, 4);
    REQUIRE (specs.size() == 1);
    REQUIRE (specs[0].anchorPort == 1);
    REQUIRE (specs[0].width == 1);
    REQUIRE (specs[0].channelName == "audio_in/Snare");

    // Stereo-Paar (2,3) mit Send am Anker → EIN Spec, Breite 2, Anker-Label
    names.setPortPairedWithNext (Direction::input, 2, true);
    names.setPortLinkSendEnabled (Direction::input, 2, true);
    specs = conduit::InputLinkSend::buildSpecs (names, 4);
    REQUIRE (specs.size() == 2);
    REQUIRE (specs[1].anchorPort == 2);
    REQUIRE (specs[1].width == 2);
    REQUIRE (specs[1].channelName == "audio_in/OH L");

    // Kanalzahl schrumpft: Anker außerhalb fallen aus den Specs
    specs = conduit::InputLinkSend::buildSpecs (names, 2);
    REQUIRE (specs.size() == 1);
    REQUIRE (specs[0].anchorPort == 1);

    // Paar am letzten Kanal ohne Partner: Send bleibt, aber mono
    specs = conduit::InputLinkSend::buildSpecs (names, 3);
    REQUIRE (specs.size() == 2);
    REQUIRE (specs[1].anchorPort == 2);
    REQUIRE (specs[1].width == 1);
}

//==============================================================================
TEST_CASE ("InputLinkSend: Diff-basiertes applySends — Handle-Identität (7.2)", "[inputsend]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    conduit::InputLinkSend send;
    send.setLinkClock (&clock);
    send.prepare (32);

    //==========================================================================
    // Neuer Anker announcet einen Kanal
    send.applySends ({ { 0, 1, "audio_in/Kick" } });
    auto* tap = send.tapHandleForPort (0);
    REQUIRE (tap != nullptr);
    REQUIRE (tap->isActive());
    REQUIRE (tap->getSinkName() == "audio_in/Kick");
    REQUIRE (send.getNumActiveSends() == 1);
    REQUIRE (clock.isAudioEnabled());

    //==========================================================================
    // Namens-Delta (Label-Rename): DERSELBE Tap, nur setName — kein Retire,
    // der Ableton-Stream reißt nicht ab (Kernanforderung)
    send.applySends ({ { 0, 1, "audio_in/Bassdrum" } });
    REQUIRE (send.tapHandleForPort (0) == tap);
    REQUIRE (tap->getSinkName() == "audio_in/Bassdrum");
    REQUIRE_FALSE (send.isRetirePending());

    //==========================================================================
    // Breiten-Delta (mono → Stereo-Paar am selben Anker): DERSELBE Tap
    send.applySends ({ { 0, 2, "audio_in/Bassdrum" } });
    REQUIRE (send.tapHandleForPort (0) == tap);
    REQUIRE (tap->getWidth() == 2);
    REQUIRE_FALSE (send.isRetirePending());

    // … und zurück auf mono — weiterhin derselbe Kanal
    send.applySends ({ { 0, 1, "audio_in/Bassdrum" } });
    REQUIRE (send.tapHandleForPort (0) == tap);
    REQUIRE (tap->getWidth() == 1);
    REQUIRE_FALSE (send.isRetirePending());

    //==========================================================================
    // Verschwundener Anker (Send aus / Kanal weg): Retire + Refcount-Freigabe
    send.applySends ({});
    REQUIRE (send.tapHandleForPort (0) == nullptr);
    REQUIRE (send.getNumActiveSends() == 0);
    REQUIRE (send.isRetirePending());
    REQUIRE_FALSE (clock.isAudioEnabled());
    REQUIRE (send.statusForPort (0) == Status::offline);

    // Idempotenz: leerer Diff auf leerem Zustand ist ein No-op
    send.applySends ({});
    REQUIRE (send.getNumActiveSends() == 0);
}

//==============================================================================
TEST_CASE ("InputLinkSend: processBlock — Commit nach captureClockState, Bounds → idle", "[inputsend]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    conduit::InputLinkSend send;
    send.setLinkClock (&clock);
    send.prepare (32);
    send.applySends ({ { 0, 2, "audio_in/Main" },
                       { 4, 1, "audio_in/Späterer Kanal" } });

    juce::AudioBuffer<float> buffer (2, 32);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 32; ++i)
            buffer.setSample (channel, i, 0.25f);

    // Commit-Pfad im selben "Callback" nach dem SessionState-Stash: ohne
    // Subscriber → noBuffer → announced (nie rejected/Crash, Netz für die
    // captureClockState-Reihenfolge)
    const auto state = clock.captureClockState (32);
    send.processBlock (buffer, 2, state);

    REQUIRE (send.statusForPort (0) == Status::announced);

    // Anker 4 liegt außerhalb der 2 Buffer-Kanäle: Kanal bleibt announced
    // (idle), bis der Message Thread den Diff nachzieht — kein Zugriff
    // außerhalb des Buffers (ASan-Wächter)
    REQUIRE (send.statusForPort (4) == Status::announced);

    // Kanal ohne Send: offline
    REQUIRE (send.statusForPort (1) == Status::offline);
}

//==============================================================================
TEST_CASE ("InputLinkSend: Retire unter echtem Audio-Thread (TSan-Ziel)", "[inputsend][threading]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::LinkClock clock (120.0, "ConduitTest");
    clock.prepare (48000.0);

    conduit::InputLinkSend send;
    send.setLinkClock (&clock);
    send.prepare (32);
    send.applySends ({ { 0, 2, "audio_in/Main" }, { 2, 1, "audio_in/Aux" } });

    std::atomic<bool> keepRunning { true };
    std::atomic<int> blocksPumped { 0 };

    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> buffer (4, 32);
        buffer.clear();

        while (keepRunning.load())
        {
            const auto state = clock.captureClockState (32);  // Stash + Commit: gleicher Thread
            send.processBlock (buffer, 4, state);
            blocksPumped.fetch_add (1);
            std::this_thread::yield();
        }
    });

    while (blocksPumped.load() < 8)
        std::this_thread::yield();

    // Message Thread zieht den Diff auf leer (Send aus) — die Sink-
    // Destruktion wartet auf den Epoch-Handshake gegen den Audio-Thread
    send.applySends ({});
    REQUIRE (send.isRetirePending());

    while (send.isRetirePending())
    {
        send.flushPendingRetirement();
        std::this_thread::yield();
    }

    keepRunning.store (false);
    audioThread.join();

    REQUIRE_FALSE (clock.isAudioEnabled());
    REQUIRE (send.statusForPort (0) == Status::offline);
    REQUIRE (send.statusForPort (2) == Status::offline);
}

//==============================================================================
TEST_CASE ("InputLinkSend: ohne LinkClock keine Taps, kein Crash", "[inputsend]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::InputLinkSend send;
    send.prepare (32);
    send.applySends ({ { 0, 1, "audio_in/Nirgendwo" } });

    REQUIRE (send.getNumActiveSends() == 0);
    REQUIRE (send.tapHandleForPort (0) == nullptr);
    REQUIRE (send.statusForPort (0) == Status::offline);

    juce::AudioBuffer<float> buffer (2, 32);
    buffer.clear();
    send.processBlock (buffer, 2, {});  // no-op, kein Crash
}

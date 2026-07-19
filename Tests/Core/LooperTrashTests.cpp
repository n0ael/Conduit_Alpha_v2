#include <cmath>
#include <functional>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/GraphManager.h"
#include "Core/Looper/LooperSessionModel.h"
#include "Core/Looper/LooperTrashCan.h"
#include "Modules/LooperPatchOutModule.h"
#include "TestSettingsFolder.h"

using conduit::BarSampleAnchors;
using conduit::CaptureService;
using conduit::CaptureSettings;
using conduit::LooperBank;
using conduit::LooperPatchOutModule;
using conduit::LooperSessionModel;
using conduit::LooperTrashCan;

namespace
{

constexpr double trashSampleRate = 48000.0;
constexpr int    trashBlockSize = 480;

struct TempTrashSettings
{
    TempTrashSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitLooperTrashTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "ConduitLooperTrashTests";
        options.filenameSuffix  = ".settings";
        options.folderName      = folder.getFullPathName();
        settings = std::make_unique<CaptureSettings> (options);
    }

    ~TempTrashSettings()
    {
        settings.reset();
        folder.deleteRecursively();
    }

    juce::File folder;
    std::unique_ptr<CaptureSettings> settings;
};

/** Minimal-Rig (Muster BankRig, LooperBankTests): Capture + Bank + Modell. */
struct TrashRig
{
    TrashRig()
    {
        service.prepare (trashSampleRate, trashBlockSize, 2);
        bank.prepare (trashSampleRate, trashBlockSize);
        service.setChannelArmed (0, true);
        service.setChannelArmed (1, true);
        feedBlocks (3);
        service.runRamGuard();
    }

    void feedBlocks (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            const auto blockStart = service.getSampleClock().now();

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = input.getWritePointer (ch);
                for (int i = 0; i < trashBlockSize; ++i)
                    data[i] = 0.5f;
            }

            service.processInputTap (input, 2);

            conduit::ClockState clock;
            clock.bpm = 120.0;
            clock.beatAtBlockStart = beat;
            clock.sampleRate = trashSampleRate;

            anchors.process (clock, blockStart, trashBlockSize);

            output.clear();
            bank.process (output, 2, clock, blockStart, anchors);

            beat += clock.beatsPerSample() * trashBlockSize;
        }
    }

    void feedBars (double bars)
    {
        feedBlocks ((int) std::lround (bars * 4.0 * 24000.0 / trashBlockSize));
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempTrashSettings temp;
    CaptureService service { *temp.settings };
    BarSampleAnchors anchors;
    LooperBank bank;
    LooperSessionModel session { bank };
    LooperTrashCan trash { bank };
    juce::AudioBuffer<float> input  { 2, trashBlockSize };
    juce::AudioBuffer<float> output { 2, trashBlockSize };
    double beat = 0.0;
};

} // namespace

//==============================================================================
TEST_CASE ("LooperTrashCan: Detach hält das RAM-Konto, Restore reattacht, Expiry gibt frei",
           "[looper]")
{
    TrashRig rig;
    REQUIRE (rig.session.addTrack (0));
    rig.feedBars (2.5);

    conduit::LooperClip* clip = nullptr;
    REQUIRE (rig.bank.commitClip (0, 1, 1, rig.service, 0, 1, rig.anchors, &clip).wasOk());
    REQUIRE (clip != nullptr);
    REQUIRE (rig.session.attachClip (0, 1, 0, clip));
    REQUIRE (rig.session.trackHasClips (0, 1));

    const auto ramWithClip = rig.bank.getRamBytesUsed();
    REQUIRE (ramWithClip > 0);

    int changedEvents = 0, expiredEvents = 0;
    rig.trash.onChanged = [&] { ++changedEvents; };
    rig.trash.onExpired = [&] { ++expiredEvents; };

    // Detach: Slot leer, Clip lebt weiter (Bank bleibt Besitzerin)
    auto* detached = rig.session.detachSlot (0, 1, 0);
    REQUIRE (detached == clip);
    REQUIRE_FALSE (rig.session.trackHasClips (0, 1));
    REQUIRE (rig.bank.getRamBytesUsed() == ramWithClip);

    LooperTrashCan::Entry entry;
    entry.kind = LooperTrashCan::Entry::Kind::track;
    entry.looperIndex = 0;
    entry.trackIndex = 1;
    entry.clips.push_back ({ 1, 0, detached, detached->clipId });
    rig.trash.push (std::move (entry));

    REQUIRE (rig.trash.hasEntries());
    REQUIRE (rig.trash.secondsRemaining() > LooperTrashCan::expirySeconds - 5.0);
    REQUIRE (changedEvents == 1);

    SECTION ("Restore: popLatest + attachClip stellt den Slot wieder her")
    {
        auto restored = rig.trash.popLatest();
        REQUIRE_FALSE (rig.trash.hasEntries());
        REQUIRE (juce::exactlyEqual (rig.trash.secondsRemaining(), 0.0));
        REQUIRE (restored.clips.size() == 1);
        REQUIRE (restored.clips[0].clipId == clip->clipId);

        REQUIRE (rig.session.attachClip (0, 1, 0, restored.clips[0].clip));
        REQUIRE (rig.session.clipAt (0, 1, 0) == clip);
        REQUIRE (rig.bank.getRamBytesUsed() == ramWithClip);
    }

    SECTION ("Expiry: endgültige Freigabe über die Audio-Quittung")
    {
        // Track stoppen und Fade auslaufen lassen, damit keine Voice mehr
        // referenziert (Retire-Protokoll)
        rig.session.stopTrack (0, 1, 0.0);
        rig.feedBlocks (4);

        rig.trash.expireNow();
        REQUIRE_FALSE (rig.trash.hasEntries());
        REQUIRE (expiredEvents == 1);

        // deleteClip ist unterwegs: Audio quittiert, serviceMessageThread
        // gibt frei — RAM-Konto fällt auf 0
        rig.feedBlocks (4);
        rig.bank.serviceMessageThread();
        REQUIRE (rig.bank.getRamBytesUsed() == 0);
    }

    SECTION ("clearWithoutDelete (prepareToPlay-Pfad): kein deleteClip, kein Double-Free")
    {
        rig.trash.clearWithoutDelete();
        REQUIRE_FALSE (rig.trash.hasEntries());
        REQUIRE (rig.bank.getRamBytesUsed() == ramWithClip);

        // prepare gibt den Store selbst frei — exakt der EngineProcessor-Ablauf
        rig.bank.prepare (trashSampleRate, trashBlockSize);
        rig.session.clearAllClips();
        REQUIRE (rig.bank.getRamBytesUsed() == 0);
    }
}

//==============================================================================
TEST_CASE ("Force-Delete + Restore: Big-Out-Kabel wandern durch den Papierkorb", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& manager = engine.getGraphManager();
    auto& session = engine.getLooperSession();
    auto& settings = engine.getLooperSettings();

    engine.setPlayConfigDetails (2, 2, 48000.0, 480);
    engine.prepareToPlay (48000.0, 480);

    juce::AudioBuffer<float> ioBuffer { 2, 480 };
    juce::MidiBuffer midi;
    const auto settleSwap = [&]
    {
        manager.flushPendingTopologyUpdate();
        for (int i = 0; i < 100 && manager.isWaitingForSilence(); ++i)
        {
            ioBuffer.clear();
            engine.processBlock (ioBuffer, midi);
            manager.flushPendingTopologyUpdate();
        }
        REQUIRE_FALSE (manager.isWaitingForSilence());
    };

    // Struktur: 1 Looper / 2 Tracks (Settings-Broadcast synchron zustellen)
    REQUIRE (session.addTrack (0));
    settings.setNumTracks (0, 2);
    settings.dispatchPendingMessages();

    auto node = manager.addModuleNode (LooperPatchOutModule::staticModuleId, {});
    REQUIRE (node.isValid());
    settleSwap();
    REQUIRE (node.getChildWithName (conduit::id::outputs).getNumChildren() == 8);

    const auto patchOutUuid = node.getProperty (conduit::id::nodeId).toString();
    const auto audioOutUuid = engine.getRootState()
        .getChildWithName (conduit::id::nodes)
        .getChildWithProperty (conduit::id::factoryId, juce::String ("audio_output"))
        .getProperty (conduit::id::nodeId).toString();

    // Kabel am Track-Out „Looper 1 · Track 2" (Offset 2)
    REQUIRE (manager.addConnection (patchOutUuid, 2, audioOutUuid, 0));
    settleSwap();

    REQUIRE (manager.hasLooperPatchOutCables (0, 1));         // Track 2 verkabelt
    REQUIRE_FALSE (manager.hasLooperPatchOutCables (0, 0));   // Track 1 nicht
    REQUIRE (manager.hasLooperPatchOutCables (0, -1));        // Looper gesamt

    SECTION ("Force-Delete entfernt Kabel + Track, Restore stellt beides her")
    {
        REQUIRE (engine.forceRemoveLooperTrack (0).wasOk());
        settleSwap();

        REQUIRE (session.getNumTracks (0) == 1);
        REQUIRE (settings.getNumTracks (0) == 1);
        REQUIRE (node.getChildWithName (conduit::id::outputs).getNumChildren() == 7);
        REQUIRE_FALSE (manager.hasLooperPatchOutCables (0, 1));
        REQUIRE (engine.getLooperTrash().hasEntries());

        int skipped = -1;
        REQUIRE (engine.restoreLooperTrash (&skipped).wasOk());
        settleSwap();

        REQUIRE (skipped == 0);
        REQUIRE_FALSE (engine.getLooperTrash().hasEntries());
        REQUIRE (session.getNumTracks (0) == 2);
        REQUIRE (settings.getNumTracks (0) == 2);
        REQUIRE (node.getChildWithName (conduit::id::outputs).getNumChildren() == 8);
        REQUIRE (manager.hasLooperPatchOutCables (0, 1));   // Kabel ist zurück

        // Spec-relativer Kanal: wieder Offset 2 (Track-2-Slot)
        bool found = false;
        const auto connections = engine.getRootState().getChildWithName (conduit::id::connections);
        for (int i = 0; i < connections.getNumChildren(); ++i)
            if (const auto connection = connections.getChild (i);
                connection.getProperty (conduit::id::sourceNodeId).toString() == patchOutUuid)
            {
                REQUIRE ((int) connection.getProperty (conduit::id::sourceChannel) == 2);
                found = true;
            }
        REQUIRE (found);
    }

    SECTION ("Restore scheitert sauber, wenn die Struktur-Position belegt ist")
    {
        REQUIRE (engine.forceRemoveLooperTrack (0).wasOk());
        settleSwap();

        // Position wieder belegen (User hat inzwischen einen Track angelegt)
        REQUIRE (session.addTrack (0));
        settings.setNumTracks (0, 2);
        settings.dispatchPendingMessages();

        REQUIRE (engine.restoreLooperTrash().failed());
        REQUIRE (engine.getLooperTrash().hasEntries());   // Eintrag bleibt erhalten
    }

    SECTION ("Looper-Force-Delete: Bus-Kabel zählt, Snapshot stellt Track-Zahl wieder her")
    {
        // Zweiter Looper mit 2 Tracks + Kabel auf dessen Bus-Out
        REQUIRE (session.addLooper());
        REQUIRE (session.addTrack (1));
        settings.setNumLoopers (2);
        settings.setNumTracks (1, 2);
        settings.dispatchPendingMessages();
        settleSwap();

        // Slots: t(1,1), t(1,2), t(2,1), t(2,2), bus(1), bus(2), sends, master
        // → bus(2)-Offset = 5 Slots × 2 = 10
        REQUIRE (manager.addConnection (patchOutUuid, 10, audioOutUuid, 1));
        REQUIRE (manager.hasLooperPatchOutCables (1, -1));

        REQUIRE (engine.forceRemoveLastLooper().wasOk());
        settleSwap();
        REQUIRE (session.getNumLoopers() == 1);
        REQUIRE_FALSE (manager.hasLooperPatchOutCables (1, -1));

        int skipped = -1;
        REQUIRE (engine.restoreLooperTrash (&skipped).wasOk());
        settleSwap();
        REQUIRE (skipped == 0);
        REQUIRE (session.getNumLoopers() == 2);
        REQUIRE (session.getNumTracks (1) == 2);   // Snapshot, nicht Reset-1
        REQUIRE (settings.getNumTracks (1) == 2);
        REQUIRE (manager.hasLooperPatchOutCables (1, -1));
    }
}

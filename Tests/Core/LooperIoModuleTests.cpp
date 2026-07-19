#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "Core/GraphManager.h"
#include "Modules/LooperInModule.h"
#include "Modules/LooperOutModule.h"
#include "TestSettingsFolder.h"

using conduit::LooperInModule;
using conduit::LooperOutModule;

namespace
{

/** Alle in Benutzung befindlichen Registry-Namen einsammeln. */
juce::StringArray activeVirtualNames (const conduit::CaptureService& capture)
{
    juce::StringArray names;
    for (int slot = 0; slot < conduit::CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
        if (const auto info = capture.getVirtualChannelUiInfo (slot); info.inUse)
            names.add (info.name);
    return names;
}

} // namespace

//==============================================================================
TEST_CASE ("Looper In (looper_in): Slots registrieren Capture-Kanäle, Quelle wählbar", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& capture = engine.getCaptureService();
    auto& manager = engine.getGraphManager();

    engine.setPlayConfigDetails (2, 2, 48000.0, 480);
    engine.prepareToPlay (48000.0, 480);

    // Fade-Swap mit laufendem Audio durchpumpen (Muster PatchingTests):
    // ohne Audio-Blöcke meldet der Fader nie Stille, der Swap bliebe
    // in waitingForSilence hängen
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

    // Modul aus dem Browser: Default = 4× stereo + 4× mono (12 Kanäle,
    // User-Entscheidung 19.07.2026)
    auto node = manager.addModuleNode (LooperInModule::staticModuleId, {});
    REQUIRE (node.isValid());
    settleSwap();

    REQUIRE (node.getChildWithName (conduit::id::inputs).getNumChildren() == 8);
    REQUIRE ((int) node.getProperty (conduit::id::numInputChannels) == 12);

    const auto moduleId = node.getProperty (conduit::id::moduleId).toString();
    const auto base1 = LooperInModule::tapBaseName (moduleId, "In 1");

    auto names = activeVirtualNames (capture);
    REQUIRE (names.contains (base1 + "_l"));
    REQUIRE (names.contains (base1 + "_r"));
    REQUIRE (names.contains (LooperInModule::tapBaseName (moduleId, "In 5")));  // Mono ohne Suffix

    SECTION ("Stereo-Slot als Looper-Quelle: tap:{base} löst _l/_r auf")
    {
        engine.setLooperSource ("tap:" + base1);
        REQUIRE (engine.getLooperLeftIndex() >= 0);
        REQUIRE (engine.getLooperRightIndex() >= 0);
        REQUIRE (engine.getLooperRightIndex() != engine.getLooperLeftIndex());
        REQUIRE (capture.isChannelArmed (engine.getLooperLeftIndex()));
    }

    SECTION ("appendInput (mono) re-materialisiert: neuer Slot + Mono-Auflösung")
    {
        LooperInModule::appendInput (node, LooperInModule::InputMode::mono,
                                     &engine.getUndoManager());
        REQUIRE ((int) node.getProperty (conduit::id::numInputChannels) == 13);
        REQUIRE ((int) node.getProperty (conduit::id::numOutputChannels) == 13);

        settleSwap();
        capture.runRamGuard();   // Guard-Timer-Ersatz: aufgeschobene
                                 // Puffersatz-Erweiterung nachholen (Slot 13
                                 // liegt jenseits der 12er-Reserve)

        const auto base2 = LooperInModule::tapBaseName (moduleId, "In 9");
        names = activeVirtualNames (capture);

        int slot9 = -1, idx9 = -2;
        for (int slot = 0; slot < conduit::CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
            if (const auto info = capture.getVirtualChannelUiInfo (slot);
                info.inUse && info.name == base2)
            {
                slot9 = slot;
                idx9 = info.captureIndex;
            }

        INFO ("nodeError='" << node.getProperty (conduit::id::nodeError).toString()
              << "' slot9=" << slot9 << " idx9=" << idx9
              << " names=" << names.joinIntoString ("|"));
        REQUIRE (names.contains (base2));            // Mono-Kanal ohne Suffix
        REQUIRE (names.contains (base1 + "_l"));     // Bestand neu registriert

        // Kein doppelter Spurname nach der Re-Materialisierung
        for (const auto& name : names)
        {
            int occurrences = 0;
            for (const auto& other : names)
                if (other == name)
                    ++occurrences;
            REQUIRE (occurrences == 1);
        }

        engine.setLooperSource ("tap:" + base2);
        REQUIRE (engine.getLooperLeftIndex() >= 0);
        REQUIRE (engine.getLooperRightIndex() == -1);   // Mono → 1-Kanal-Clip
    }

    SECTION ("removeInput: letzter Slot bleibt stehen")
    {
        for (int i = 0; i < 20; ++i)   // mehr Versuche als Slots — der letzte bleibt
            LooperInModule::removeInput (node, 0, &engine.getUndoManager());

        REQUIRE (node.getChildWithName (conduit::id::inputs).getNumChildren() == 1);
        REQUIRE ((int) node.getProperty (conduit::id::numInputChannels) == 1);   // "In 8" = mono
    }

    SECTION ("Delete Phase 1 räumt die Kanäle ab (releaseCaptureResources)")
    {
        REQUIRE (manager.requestNodeDelete (
            node.getProperty (conduit::id::nodeId).toString()));
        // Kern-Kontrakt wie beim Link-Receive-Tap: kein Crash, Handles
        // invalidiert — "held"-Reste sind Registry-Detail
    }
}

//==============================================================================
TEST_CASE ("Looper In als Sackgasse: Capture läuft ohne weiterführendes Kabel", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& capture = engine.getCaptureService();
    auto& manager = engine.getGraphManager();

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

    auto node = manager.addModuleNode (LooperInModule::staticModuleId, {});
    REQUIRE (node.isValid());
    settleSwap();

    // Interface-Eingang → Looper In, der Pass-Through-Ausgang bleibt
    // UNVERKABELT (Sackgasse) — der Tap muss trotzdem beschrieben werden
    const auto audioInUuid = engine.getRootState()
        .getChildWithName (conduit::id::nodes)
        .getChildWithProperty (conduit::id::factoryId, juce::String ("audio_input"))
        .getProperty (conduit::id::nodeId).toString();
    const auto audioOutUuid = engine.getRootState()
        .getChildWithName (conduit::id::nodes)
        .getChildWithProperty (conduit::id::factoryId, juce::String ("audio_output"))
        .getProperty (conduit::id::nodeId).toString();
    const auto looperInUuid = node.getProperty (conduit::id::nodeId).toString();
    REQUIRE (audioInUuid.isNotEmpty());

    // User-Reihenfolge nachstellen: ERST Quelle wählen (armt den Kanal),
    // ein paar stille Blöcke laufen lassen, DANN erst die Kabel stecken
    const auto moduleIdForKey = node.getProperty (conduit::id::moduleId).toString();
    engine.setLooperSource ("tap:" + LooperInModule::tapBaseName (moduleIdForKey, "In 1"));
    REQUIRE (engine.getLooperLeftIndex() >= 0);
    for (int block = 0; block < 5; ++block)
    {
        ioBuffer.clear();
        engine.processBlock (ioBuffer, midi);
        capture.runRamGuard();
    }

    REQUIRE (manager.addConnection (audioInUuid, 0, looperInUuid, 0));
    REQUIRE (manager.addConnection (audioInUuid, 1, looperInUuid, 1));

    // Fan-out wie im echten Patch: dieselbe Quelle speist parallel den
    // Master-Ausgang (hörbarer Direktpfad) — der Looper-Abzweig ist nur
    // ein zusätzliches Kabel, kein Durchschleifen
    REQUIRE (manager.addConnection (audioInUuid, 0, audioOutUuid, 0));
    REQUIRE (manager.addConnection (audioInUuid, 1, audioOutUuid, 1));
    settleSwap();

    // Re-Materialisierung, WÄHREND die Quelle gearmt ist (User-Flow:
    // Slots nachrüsten): die gearmten alten Kanäle binden ihr Material
    // als held, die neue Instanz registriert auf ANDEREN Slots — die
    // Looper-Indizes müssen der Registry folgen, sonst liest der Looper
    // dauerhaft den toten Kanal (Stille)
    LooperInModule::appendInput (node, LooperInModule::InputMode::mono,
                                 &engine.getUndoManager());
    settleSwap();

    // Signal einspeisen: der Input-Tap liefert die Hardware-Kanäle, der
    // Graph reicht sie via Anker-Kabel an audio_input → Looper In weiter
    for (int block = 0; block < 40; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* dest = ioBuffer.getWritePointer (ch);
            for (int i = 0; i < 480; ++i)
                dest[i] = 0.5f * std::sin (0.05f * (float) (block * 480 + i));
        }
        engine.processBlock (ioBuffer, midi);
        capture.runRamGuard();   // Guard-Timer-Ersatz: Segment-Pool bedienen
    }

    const auto moduleId = node.getProperty (conduit::id::moduleId).toString();
    const auto tapName = LooperInModule::tapBaseName (moduleId, "In 1") + "_l";

    int tapIndex = -1;
    for (int slot = 0; slot < conduit::CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
        if (const auto info = capture.getVirtualChannelUiInfo (slot);
            info.inUse && info.name == tapName)
            tapIndex = info.captureIndex;

    REQUIRE (tapIndex >= 0);
    // Kernkontrakt: der Looper liest GENAU den Kanal, der aktuell unter
    // dem Slot-Namen registriert ist — nie einen stale Index
    REQUIRE (engine.getLooperLeftIndex() == tapIndex);
    const auto* channel = capture.getChannel (tapIndex);
    REQUIRE (channel != nullptr);
    REQUIRE (channel->getState() == conduit::CaptureChannel::State::recording);

    // Arming (forceOpen) öffnet auch bei Stille — deshalb den Ring-Inhalt
    // selbst prüfen: der Sinus muss im Capture-Material stehen
    const auto range = channel->getReadableRange();
    REQUIRE (range.to > range.from + 480);
    std::vector<float> readBack (480, 0.0f);
    REQUIRE (channel->read (range.to - 480, readBack.data(), 480));
    float peak = 0.0f;
    for (const auto sample : readBack)
        peak = juce::jmax (peak, std::abs (sample));
    REQUIRE (peak > 0.1f);
}

//==============================================================================
TEST_CASE ("Looper In Auto-Naming: Slot folgt der verkabelten Quelle, Key migriert", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    auto& capture = engine.getCaptureService();
    auto& manager = engine.getGraphManager();

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

    // Eingangs-Label wie beim User ("mopho" am Interface-Kanal 0)
    engine.getChannelNames().setActiveDevice ("TestDev", { "mopho", "micro freak" }, {});

    auto node = manager.addModuleNode (LooperInModule::staticModuleId, {});
    REQUIRE (node.isValid());
    settleSwap();

    const auto moduleId = node.getProperty (conduit::id::moduleId).toString();
    const auto looperInUuid = node.getProperty (conduit::id::nodeId).toString();
    const auto audioInUuid = engine.getRootState()
        .getChildWithName (conduit::id::nodes)
        .getChildWithProperty (conduit::id::factoryId, juce::String ("audio_input"))
        .getProperty (conduit::id::nodeId).toString();

    // Quelle VOR dem Verkabeln wählen (Default-Slot-Name "In 1")
    engine.setLooperSource ("tap:" + LooperInModule::tapBaseName (moduleId, "In 1"));
    REQUIRE (engine.getLooperLeftIndex() >= 0);

    // Kabel stecken: Slot-Name folgt dem Quell-Label, der gespeicherte
    // Quell-Key wandert mit (Rename-Migration), Auflösung bleibt gültig
    REQUIRE (manager.addConnection (audioInUuid, 0, looperInUuid, 0));

    const auto inputTree = node.getChildWithName (conduit::id::inputs).getChild (0);
    REQUIRE (inputTree.getProperty (conduit::id::inputAutoName).toString() == "mopho");
    REQUIRE (engine.getLooperSettings().getSourceKey (0)
             == "tap:" + LooperInModule::tapBaseName (moduleId, "mopho"));

    const auto names = activeVirtualNames (capture);
    REQUIRE (names.contains (LooperInModule::tapBaseName (moduleId, "mopho") + "_l"));
    REQUIRE_FALSE (names.contains (LooperInModule::tapBaseName (moduleId, "In 1") + "_l"));
    REQUIRE (engine.getLooperLeftIndex() >= 0);

    SECTION ("Gleiche Quelle an zweitem Slot bekommt ein Suffix (kein Namens-Clash)")
    {
        // Slot 2 (Kanäle 2/3) der Default-Bestückung mit derselben Quelle
        REQUIRE (manager.addConnection (audioInUuid, 0, looperInUuid, 2));
        const auto second = node.getChildWithName (conduit::id::inputs).getChild (1);
        REQUIRE (second.getProperty (conduit::id::inputAutoName).toString() == "mopho 2");
    }

    SECTION ("Ketten-Name: Eingang · FX vor dem Looper-In")
    {
        // mopho → FX → Looper-In-Slot 2: der Slot-Name zeigt die Kette,
        // Klangquelle zuerst (User-Regel 19.07.2026)
        auto fxNode = manager.addModuleNode ("airwindows_galactic", {});
        REQUIRE (fxNode.isValid());
        settleSwap();
        const auto fxUuid = fxNode.getProperty (conduit::id::nodeId).toString();
        const auto fxModuleId = fxNode.getProperty (conduit::id::moduleId).toString();

        REQUIRE (manager.addConnection (audioInUuid, 0, fxUuid, 0));
        REQUIRE (manager.addConnection (fxUuid, 0, looperInUuid, 2));

        const auto second = node.getChildWithName (conduit::id::inputs).getChild (1);
        REQUIRE (second.getProperty (conduit::id::inputAutoName).toString()
                 == "mopho" + juce::String::fromUTF8 (" · ") + fxModuleId);
    }

    SECTION ("User-Name gewinnt: Kabel-Stecken überschreibt ihn nicht")
    {
        auto in = node.getChildWithName (conduit::id::inputs).getChild (0);
        in.setProperty (conduit::id::inputUserName, "Bassline", nullptr);
        REQUIRE (engine.getLooperSettings().getSourceKey (0)
                 == "tap:" + LooperInModule::tapBaseName (moduleId, "Bassline"));

        // erneutes Kabel (zweiter Kanal des Stereo-Slots)
        REQUIRE (manager.addConnection (audioInUuid, 1, looperInUuid, 1));
        REQUIRE (in.getProperty (conduit::id::inputUserName).toString() == "Bassline");
        REQUIRE (LooperInModule::effectiveInputName (in, 0) == "Bassline");
    }
}

//==============================================================================
TEST_CASE ("Looper Out (looper_out): Schema-Helfer und Default-Bestückung", "[looper]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    SECTION ("createState: Master + Looper 1–4 stereo = 10 Kanäle")
    {
        LooperOutModule module;
        auto tree = module.createState();

        REQUIRE ((int) tree.getProperty (conduit::id::numOutputChannels) == 10);
        REQUIRE ((int) tree.getProperty (conduit::id::numInputChannels) == 0);

        const auto specs = LooperOutModule::readOutputConfig (tree);
        REQUIRE (specs.size() == 5);
        REQUIRE (specs[0].target == 0);
        REQUIRE (specs[4].target == 4);
        for (const auto& spec : specs)
        {
            REQUIRE (spec.mode == LooperOutModule::Mode::stereo);
            REQUIRE_FALSE (spec.pre);
        }
    }

    SECTION ("append/remove: Kanalzahl folgt den Breiten, letzter Slot bleibt")
    {
        LooperOutModule module;
        auto tree = module.createState();

        LooperOutModule::appendOutput (tree, { 2, LooperOutModule::Mode::sum, true }, nullptr);
        REQUIRE ((int) tree.getProperty (conduit::id::numOutputChannels) == 11);

        const auto specs = LooperOutModule::readOutputConfig (tree);
        REQUIRE (specs.back().target == 2);
        REQUIRE (specs.back().mode == LooperOutModule::Mode::sum);
        REQUIRE (specs.back().pre);

        for (int i = 0; i < 10; ++i)
            LooperOutModule::removeOutput (tree, 0, nullptr);
        REQUIRE (tree.getChildWithName (conduit::id::outputs).getNumChildren() == 1);
    }

    SECTION ("Labels: Master / Looper n · Modus")
    {
        using Out = LooperOutModule;
        REQUIRE (Out::outputLabel ({ 0, Out::Mode::stereo, false }) == "Master");
        REQUIRE (Out::outputLabel ({ 3, Out::Mode::stereo, false }) == "Looper 3");
        REQUIRE (Out::outputLabel ({ 1, Out::Mode::sum, false })
                 == juce::String::fromUTF8 ("Looper 1 · Summe"));
        REQUIRE (Out::outputLabel ({ 2, Out::Mode::left, true })
                 == juce::String::fromUTF8 ("Looper 2 · L"));
    }
}

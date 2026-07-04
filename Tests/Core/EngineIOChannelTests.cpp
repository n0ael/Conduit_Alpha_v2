#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"
#include "TestSettingsFolder.h"

namespace
{

// Ein Layout mit genau einem Ein- und einem Ausgangs-Bus (wie der
// EngineProcessor sie deklariert), diskret in der gewünschten Kanalzahl —
// analog zu AudioProcessorPlayer::NumChannels::toLayout().
juce::AudioProcessor::BusesLayout ioLayout (int ins, int outs)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add  (juce::AudioChannelSet::discreteChannels (ins));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (outs));
    return layout;
}

// Der reservierte I/O-Tree-Node zu einem factoryKey (audio_input/audio_output).
juce::ValueTree ioNode (conduit::EngineProcessor& engine, const char* factoryKey)
{
    return engine.getRootState().getChildWithName (conduit::id::nodes)
                 .getChildWithProperty (conduit::id::factoryId, juce::String (factoryKey));
}

int propInt (const juce::ValueTree& node, const juce::Identifier& prop)
{
    return (int) node.getProperty (prop, -1);
}

juce::ValueTree connectionsOf (conduit::EngineProcessor& engine)
{
    return engine.getRootState().getChildWithName (conduit::id::connections);
}

// Legt ein Kabel direkt im Tree an (ohne UndoManager) — der andere Endpunkt
// darf ein beliebiger Uuid sein, das Pruning inspiziert nur die I/O-Seite.
void addConnection (conduit::EngineProcessor& engine,
                    const juce::String& srcId, int srcCh,
                    const juce::String& dstId, int dstCh)
{
    juce::ValueTree c (conduit::id::connection);
    c.setProperty (conduit::id::sourceNodeId,  srcId, nullptr);
    c.setProperty (conduit::id::sourceChannel, srcCh, nullptr);
    c.setProperty (conduit::id::destNodeId,    dstId, nullptr);
    c.setProperty (conduit::id::destChannel,   dstCh, nullptr);
    connectionsOf (engine).appendChild (c, nullptr);
}

bool hasConnection (conduit::EngineProcessor& engine,
                    const juce::String& srcId, int srcCh,
                    const juce::String& dstId, int dstCh)
{
    const auto conns = connectionsOf (engine);
    for (int i = 0; i < conns.getNumChildren(); ++i)
    {
        const auto c = conns.getChild (i);
        if (c.getProperty (conduit::id::sourceNodeId).toString() == srcId
            && (int) c.getProperty (conduit::id::sourceChannel) == srcCh
            && c.getProperty (conduit::id::destNodeId).toString() == dstId
            && (int) c.getProperty (conduit::id::destChannel) == dstCh)
            return true;
    }
    return false;
}

} // namespace

//==============================================================================
// Schritt A: Der EngineProcessor akzeptiert die echte Device-I/O-Kanalzahl,
// damit der AudioProcessorPlayer sie vor dem Stereo-Default annimmt
// (findMostSuitableLayout) und bis in den Graph durchreicht.
//==============================================================================
TEST_CASE ("EngineProcessor akzeptiert Multichannel-I/O-Layouts", "[engine][io]")
{
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };

    SECTION ("echte Hardware-Kanalzahl (8/8) wird angenommen")
        REQUIRE (engine.checkBusesLayoutSupported (ioLayout (8, 8)));

    SECTION ("asymmetrisches Interface (2 In / 8 Out) wird angenommen")
        REQUIRE (engine.checkBusesLayoutSupported (ioLayout (2, 8)));

    SECTION ("ungerade Kanalzahl (6/6) wird angenommen")
        REQUIRE (engine.checkBusesLayoutSupported (ioLayout (6, 6)));

    SECTION ("Ausgabe-only-Interface (0 Eingänge) ist zulässig")
        REQUIRE (engine.checkBusesLayoutSupported (ioLayout (0, 2)));

    SECTION ("kein Ausgang wird abgelehnt")
        REQUIRE_FALSE (engine.checkBusesLayoutSupported (ioLayout (2, 0)));
}

TEST_CASE ("EngineProcessor reicht die Device-Kanalzahl bis zum Graph durch", "[engine][io]")
{
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };

    // Genau der Aufruf des AudioProcessorPlayer nach findMostSuitableLayout:
    // setPlayConfigDetails wählt das nächstbeste unterstützte Layout. Da wir
    // die echte Kanalzahl akzeptieren, tragen Prozessor und (via prepareToPlay)
    // Graph danach genau diese Zahl.
    engine.setPlayConfigDetails (8, 8, 48000.0, 32);
    REQUIRE (engine.getTotalNumInputChannels()  == 8);
    REQUIRE (engine.getTotalNumOutputChannels() == 8);

    SECTION ("Ausgabe-only: 0 Eingänge, Ausgänge stehen")
    {
        engine.setPlayConfigDetails (0, 2, 48000.0, 32);
        REQUIRE (engine.getTotalNumInputChannels()  == 0);
        REQUIRE (engine.getTotalNumOutputChannels() == 2);
    }
}

//==============================================================================
// Schritt B: syncHardwareIOChannels koppelt die I/O-Tree-Nodes an die echte
// Device-Kanalzahl → die Port-UI (numInputChannels/numOutputChannels) folgt.
//==============================================================================
TEST_CASE ("syncHardwareIOChannels koppelt die I/O-Nodes an die Device-Kanalzahl", "[engine][io]")
{
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };

    const auto in  = ioNode (engine, conduit::audioInputModuleId);
    const auto out = ioNode (engine, conduit::audioOutputModuleId);
    REQUIRE (in.isValid());
    REQUIRE (out.isValid());

    SECTION ("Defaults sind stereo (2 In / 2 Out)")
    {
        REQUIRE (propInt (in,  conduit::id::numOutputChannels) == 2);
        REQUIRE (propInt (out, conduit::id::numInputChannels)  == 2);
    }

    SECTION ("Multichannel-Interface (8 In / 6 Out)")
    {
        engine.syncHardwareIOChannels (8, 6);

        // audio_in liefert Kanäle → Ausgangs-Ports; audio_out nimmt sie → Eingangs-Ports
        REQUIRE (propInt (in,  conduit::id::numOutputChannels) == 8);
        REQUIRE (propInt (out, conduit::id::numInputChannels)  == 6);

        // Die jeweils andere Bank bleibt unangetastet (0)
        REQUIRE (propInt (in,  conduit::id::numInputChannels)  == 0);
        REQUIRE (propInt (out, conduit::id::numOutputChannels) == 0);
    }

    SECTION ("Ausgabe-only-Interface (0 Eingänge)")
    {
        engine.syncHardwareIOChannels (0, 2);
        REQUIRE (propInt (in,  conduit::id::numOutputChannels) == 0);
        REQUIRE (propInt (out, conduit::id::numInputChannels)  == 2);
    }

    SECTION ("Schrumpfen: 8 Out zurück auf 2")
    {
        engine.syncHardwareIOChannels (2, 8);
        REQUIRE (propInt (out, conduit::id::numInputChannels) == 8);

        engine.syncHardwareIOChannels (2, 2);
        REQUIRE (propInt (out, conduit::id::numInputChannels) == 2);
    }

    SECTION ("negative Werte werden auf 0 geklemmt")
    {
        engine.syncHardwareIOChannels (-1, -5);
        REQUIRE (propInt (in,  conduit::id::numOutputChannels) == 0);
        REQUIRE (propInt (out, conduit::id::numInputChannels)  == 0);
    }
}

//==============================================================================
// Schritt C: Beim Schrumpfen der Kanalzahl werden Kabel auf verschwundene
// I/O-Kanäle gekappt (Phantom-Connection-Schutz), gültige bleiben stehen.
//==============================================================================
TEST_CASE ("syncHardwareIOChannels kappt Kabel auf verschwundene I/O-Kanäle", "[engine][io]")
{
    conduit::test::ScopedSettingsFolder settingsFolder;
    conduit::EngineProcessor engine { settingsFolder.folder };
    engine.syncHardwareIOChannels (8, 8);  // Multichannel-Interface

    const auto inId  = ioNode (engine, conduit::audioInputModuleId).getProperty (conduit::id::nodeId).toString();
    const auto outId = ioNode (engine, conduit::audioOutputModuleId).getProperty (conduit::id::nodeId).toString();
    const juce::String moduleId = juce::Uuid().toString();  // Gegenstelle (beliebig)

    // audio_in ist Quelle: Kanäle 1 und 6 in ein Modul
    addConnection (engine, inId, 1, moduleId, 0);
    addConnection (engine, inId, 6, moduleId, 1);
    // audio_out ist Ziel: Modul in die Kanäle 0 und 7
    addConnection (engine, moduleId, 0, outId, 0);
    addConnection (engine, moduleId, 1, outId, 7);
    REQUIRE (connectionsOf (engine).getNumChildren() == 4);

    SECTION ("Schrumpfen auf 2/2 entfernt genau die out-of-range Kabel")
    {
        engine.syncHardwareIOChannels (2, 2);

        REQUIRE (connectionsOf (engine).getNumChildren() == 2);
        REQUIRE      (hasConnection (engine, inId, 1, moduleId, 0));      // Kanal 1 < 2 bleibt
        REQUIRE_FALSE (hasConnection (engine, inId, 6, moduleId, 1));     // Kanal 6 weg
        REQUIRE      (hasConnection (engine, moduleId, 0, outId, 0));     // Kanal 0 < 2 bleibt
        REQUIRE_FALSE (hasConnection (engine, moduleId, 1, outId, 7));    // Kanal 7 weg
    }

    SECTION ("gleiche Kanalzahl lässt alle Kabel stehen")
    {
        engine.syncHardwareIOChannels (8, 8);
        REQUIRE (connectionsOf (engine).getNumChildren() == 4);
    }

    SECTION ("Ausstecken (0/0) kappt alle I/O-Kabel")
    {
        engine.syncHardwareIOChannels (0, 0);
        REQUIRE (connectionsOf (engine).getNumChildren() == 0);
    }

    SECTION ("fremde Kabel (kein I/O-Endpunkt) bleiben unangetastet")
    {
        const juce::String otherA = juce::Uuid().toString();
        const juce::String otherB = juce::Uuid().toString();
        addConnection (engine, otherA, 5, otherB, 9);  // hohe Kanäle, aber kein I/O

        engine.syncHardwareIOChannels (0, 0);
        REQUIRE (hasConnection (engine, otherA, 5, otherB, 9));
    }
}

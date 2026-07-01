#include <catch2/catch_test_macros.hpp>

#include "Core/EngineProcessor.h"

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

} // namespace

//==============================================================================
// Schritt A: Der EngineProcessor akzeptiert die echte Device-I/O-Kanalzahl,
// damit der AudioProcessorPlayer sie vor dem Stereo-Default annimmt
// (findMostSuitableLayout) und bis in den Graph durchreicht.
//==============================================================================
TEST_CASE ("EngineProcessor akzeptiert Multichannel-I/O-Layouts", "[engine][io]")
{
    conduit::EngineProcessor engine;

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
    conduit::EngineProcessor engine;

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
    conduit::EngineProcessor engine;

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

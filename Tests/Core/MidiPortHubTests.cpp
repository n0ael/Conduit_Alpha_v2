#include <map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/MidiPortHub.h"

using conduit::MidiPortHub;
using conduit::MidiRigSettings;
using conduit::RigDevice;
using conduit::RigDeviceKind;
using conduit::midirig::resolvePortName;

namespace
{

struct TempSettings
{
    TempSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitMidiPortHubTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempSettings() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options result;
        result.applicationName = "MidiPortHubTests";
        result.filenameSuffix  = ".settings";
        result.folderName      = folder.getFullPathName();
        return result;
    }

    juce::File folder;
};

//==============================================================================
/** Fake-Port-Rig: liefert Device-Listen und fängt die Öffnungen ab. Der
    Test spielt Messages direkt in den registrierten MidiInputCallback ein
    (Produzenten-Pfad) und sammelt gesendete Out-Messages. */
struct FakePortRig
{
    juce::Array<juce::MidiDeviceInfo> inputs;
    juce::Array<juce::MidiDeviceInfo> outputs;

    // identifier -> zuletzt registrierter Callback (offener Port)
    std::map<juce::String, juce::MidiInputCallback*> openInputCallbacks;
    std::vector<std::pair<juce::String, juce::MidiMessage>> sentMessages;
    int inputOpenCount = 0;

    struct FakeInputHandle final : conduit::midirig::InputPortHandle
    {
        FakePortRig& rig;
        juce::String identifier;

        FakeInputHandle (FakePortRig& rigToUse, juce::String identifierToUse)
            : rig (rigToUse), identifier (std::move (identifierToUse)) {}

        ~FakeInputHandle() override { rig.openInputCallbacks.erase (identifier); }
    };

    struct FakeOutputHandle final : conduit::midirig::OutputPortHandle
    {
        FakePortRig& rig;
        juce::String identifier;

        FakeOutputHandle (FakePortRig& rigToUse, juce::String identifierToUse)
            : rig (rigToUse), identifier (std::move (identifierToUse)) {}

        void send (const juce::MidiMessage& message) override
        {
            rig.sentMessages.emplace_back (identifier, message);
        }
    };

    [[nodiscard]] MidiPortHub::DeviceListProvider inputProvider()
    {
        return [this] { return inputs; };
    }
    [[nodiscard]] MidiPortHub::DeviceListProvider outputProvider()
    {
        return [this] { return outputs; };
    }
    [[nodiscard]] conduit::midirig::InputPortOpener inputOpener()
    {
        return [this] (const juce::String& identifier, juce::MidiInputCallback& callback)
                   -> std::unique_ptr<conduit::midirig::InputPortHandle>
        {
            ++inputOpenCount;
            openInputCallbacks[identifier] = &callback;
            return std::make_unique<FakeInputHandle> (*this, identifier);
        };
    }
    [[nodiscard]] conduit::midirig::OutputPortOpener outputOpener()
    {
        return [this] (const juce::String& identifier)
                   -> std::unique_ptr<conduit::midirig::OutputPortHandle>
        {
            return std::make_unique<FakeOutputHandle> (*this, identifier);
        };
    }

    void feedCc (const juce::String& identifier, int channel, int cc, int value)
    {
        REQUIRE (openInputCallbacks.count (identifier) == 1);
        openInputCallbacks[identifier]->handleIncomingMidiMessage (
            nullptr, juce::MidiMessage::controllerEvent (channel, cc, value));
    }

    void feedNoteOn (const juce::String& identifier, int note, int velocity)
    {
        REQUIRE (openInputCallbacks.count (identifier) == 1);
        openInputCallbacks[identifier]->handleIncomingMidiMessage (
            nullptr, juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity));
    }
};

} // namespace

//==============================================================================
TEST_CASE ("resolvePortName: exakter Treffer", "[midirig]")
{
    const juce::StringArray available { "ES-3", "LCXL In" };
    REQUIRE (resolvePortName ("ES-3", available) == "ES-3");
}

TEST_CASE ("resolvePortName: Prefix-Treffer -- registrierter Name traegt den Suffix", "[midirig]")
{
    const juce::StringArray available { "ES-3", "LCXL In" };
    REQUIRE (resolvePortName ("ES-3 (2)", available) == "ES-3");
}

TEST_CASE ("resolvePortName: Prefix-Treffer -- verfuegbarer Name traegt den Suffix", "[midirig]")
{
    const juce::StringArray available { "ES-3 (2)", "LCXL In" };
    REQUIRE (resolvePortName ("ES-3", available) == "ES-3 (2)");
}

TEST_CASE ("resolvePortName: kein Treffer", "[midirig]")
{
    const juce::StringArray available { "ES-3", "LCXL In" };
    REQUIRE (resolvePortName ("Digitakt", available).isEmpty());
}

TEST_CASE ("resolvePortName: leerer registrierter Name -> kein Treffer", "[midirig]")
{
    const juce::StringArray available { "ES-3" };
    REQUIRE (resolvePortName ("", available).isEmpty());
}

TEST_CASE ("resolvePortName: Klammer-Suffix ohne Ziffern zaehlt nicht als Duplikat-Suffix", "[midirig]")
{
    const juce::StringArray available { "Digitakt (USB)" };
    REQUIRE (resolvePortName ("Digitakt", available).isEmpty());
}

//==============================================================================
TEST_CASE ("MidiPortHub: zwei Eingaenge liefern parallel an die richtigen Abonnenten", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };
    FakePortRig rig;
    rig.inputs = { juce::MidiDeviceInfo ("Ctrl A", "id-a"),
                   juce::MidiDeviceInfo ("Ctrl B", "id-b") };

    const auto deviceA = settings.addDevice ("A", RigDeviceKind::controller);
    const auto deviceB = settings.addDevice ("B", RigDeviceKind::controller);
    settings.setMidiInName (deviceA, "Ctrl A");
    settings.setMidiInName (deviceB, "Ctrl B");

    MidiPortHub hub { settings, rig.inputProvider(), rig.outputProvider(),
                      rig.inputOpener(), rig.outputOpener() };
    hub.syncFromRegistry();

    REQUIRE (hub.isInputConnected (deviceA));
    REQUIRE (hub.isInputConnected (deviceB));

    std::vector<int> valuesA, valuesB;
    hub.subscribeController (deviceA, [&valuesA] (const conduit::midi::ControllerEvent& e)
                             { valuesA.push_back (e.value); });
    hub.subscribeController (deviceB, [&valuesB] (const conduit::midi::ControllerEvent& e)
                             { valuesB.push_back (e.value); });

    rig.feedCc ("id-a", 1, 74, 10);
    rig.feedCc ("id-b", 1, 74, 20);
    rig.feedCc ("id-a", 1, 74, 11);
    hub.drainNow();

    REQUIRE (valuesA == std::vector<int> { 10, 11 });
    REQUIRE (valuesB == std::vector<int> { 20 });
}

TEST_CASE ("MidiPortHub: Noten-Abo liefert Note-On/Off des richtigen Geraets", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };
    FakePortRig rig;
    rig.inputs = { juce::MidiDeviceInfo ("Echo In", "id-echo") };

    const auto device = settings.addDevice ("Grid-Ausgang", RigDeviceKind::soundGenerator);
    settings.setMidiInName (device, "Echo In");

    MidiPortHub hub { settings, rig.inputProvider(), rig.outputProvider(),
                      rig.inputOpener(), rig.outputOpener() };
    hub.syncFromRegistry();

    std::vector<std::pair<int, bool>> notes;
    hub.subscribeNotes (device, [&notes] (const conduit::midi::NoteEvent& e)
                        { notes.emplace_back (e.note, e.isOn); });

    rig.feedNoteOn ("id-echo", 60, 100);
    rig.feedNoteOn ("id-echo", 60, 0);   // NoteOn mit Velocity 0 == NoteOff
    hub.drainNow();

    REQUIRE (notes.size() == 2);
    REQUIRE (notes[0] == std::pair<int, bool> (60, true));
    REQUIRE (notes[1] == std::pair<int, bool> (60, false));
}

TEST_CASE ("MidiPortHub: Ueberlauf blockiert nicht, der finale Wert kommt an (Latest-Pending)", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };
    FakePortRig rig;
    rig.inputs = { juce::MidiDeviceInfo ("Ctrl A", "id-a") };

    const auto device = settings.addDevice ("A", RigDeviceKind::controller);
    settings.setMidiInName (device, "Ctrl A");

    MidiPortHub hub { settings, rig.inputProvider(), rig.outputProvider(),
                      rig.inputOpener(), rig.outputOpener() };
    hub.syncFromRegistry();

    std::vector<int> values;
    hub.subscribeController (device, [&values] (const conduit::midi::ControllerEvent& e)
                             { values.push_back (e.value); });

    // Deutlich mehr Events als die Queue-Kapazität (512) OHNE Drain —
    // 14-Bit-Zaehler als eindeutige Wert-Signatur (7-bit CC-Wert reicht
    // nicht, daher Wert i % 128, Signatur ueber die Reihenfolge).
    constexpr int kFlood = 600;
    for (int i = 0; i < kFlood; ++i)
        rig.feedCc ("id-a", 1, 74, i % 128);

    hub.drainNow();

    // Kein Blockieren; Zwischenwerte der Flut duerfen fehlen, aber der
    // FINALE Wert (599 % 128) ist garantiert dabei — als letztes Element.
    REQUIRE (! values.empty());
    REQUIRE ((int) values.size() < kFlood);      // Ueberlauf hat verworfen
    REQUIRE (values.back() == (kFlood - 1) % 128);
}

TEST_CASE ("MidiPortHub: Reconnect-Re-Sync bindet den Port unter neuem Suffix-Namen (Prefix-Match)", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };
    FakePortRig rig;
    rig.inputs = { juce::MidiDeviceInfo ("Ctrl A", "id-a") };

    const auto device = settings.addDevice ("A", RigDeviceKind::controller);
    settings.setMidiInName (device, "Ctrl A");

    MidiPortHub hub { settings, rig.inputProvider(), rig.outputProvider(),
                      rig.inputOpener(), rig.outputOpener() };
    hub.syncFromRegistry();
    REQUIRE (hub.isInputConnected (device));
    REQUIRE (rig.inputOpenCount == 1);

    // USB ab: Geraet verschwindet aus der Liste.
    rig.inputs.clear();
    hub.syncFromRegistry();
    REQUIRE_FALSE (hub.isInputConnected (device));

    // USB wieder dran — das OS haengt einen Duplikat-Suffix an.
    rig.inputs = { juce::MidiDeviceInfo ("Ctrl A (2)", "id-a2") };
    hub.syncFromRegistry();
    REQUIRE (hub.isInputConnected (device));
    REQUIRE (rig.inputOpenCount == 2);

    // Events fliessen ueber den neuen Port.
    std::vector<int> values;
    hub.subscribeController (device, [&values] (const conduit::midi::ControllerEvent& e)
                             { values.push_back (e.value); });
    rig.feedCc ("id-a2", 1, 74, 42);
    hub.drainNow();
    REQUIRE (values == std::vector<int> { 42 });
}

TEST_CASE ("MidiPortHub: Senden ueber Fassade + Rollen-Fassade, No-op ohne Port", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };
    FakePortRig rig;
    rig.outputs = { juce::MidiDeviceInfo ("Synth Out", "id-out") };

    const auto device = settings.addDevice ("Synth", RigDeviceKind::soundGenerator);
    settings.setMidiOutName (device, "Synth Out");
    settings.setGridOutputDeviceId (device);

    MidiPortHub hub { settings, rig.inputProvider(), rig.outputProvider(),
                      rig.inputOpener(), rig.outputOpener() };
    hub.syncFromRegistry();

    REQUIRE (hub.isOutputConnected (device));
    REQUIRE (hub.openOutputNameFor (device) == "Synth Out");

    hub.outputTargetFor (device).send (juce::MidiMessage::controllerEvent (1, 7, 100));
    hub.gridOutputTarget().send (juce::MidiMessage::controllerEvent (1, 1, 64));
    REQUIRE (rig.sentMessages.size() == 2);
    REQUIRE (rig.sentMessages[0].first == "id-out");
    REQUIRE (rig.sentMessages[1].second.getControllerNumber() == 1);

    // Unbekanntes Geraet / leere Rolle: send ist ein stiller No-op.
    hub.send (juce::Uuid(), juce::MidiMessage::controllerEvent (1, 7, 1));
    settings.setGridOutputDeviceId (juce::Uuid());
    hub.gridOutputTarget().send (juce::MidiMessage::controllerEvent (1, 7, 1));
    REQUIRE (rig.sentMessages.size() == 2);
}

TEST_CASE ("MidiPortHub: Tick-Abo feuert bei jedem Drain, unsubscribe beendet es", "[midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };
    FakePortRig rig;

    MidiPortHub hub { settings, rig.inputProvider(), rig.outputProvider(),
                      rig.inputOpener(), rig.outputOpener() };

    int ticks = 0;
    const auto token = hub.subscribeTick ([&ticks] { ++ticks; });
    hub.drainNow();
    hub.drainNow();
    REQUIRE (ticks == 2);

    hub.unsubscribe (token);
    hub.drainNow();
    REQUIRE (ticks == 2);
}

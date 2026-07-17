#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "TouchLive/LiveRemoteBridge.h"

using conduit::LiveRemoteBridge;
using conduit::MidiPortHub;
using conduit::MidiRigSettings;
using conduit::RigDeviceKind;
using Catch::Approx;
namespace midi = conduit::midi;

namespace
{

struct TempSettings
{
    TempSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitLiveRemoteBridgeTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempSettings() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options result;
        result.applicationName = "LiveRemoteBridgeTests";
        result.filenameSuffix  = ".settings";
        result.folderName      = folder.getFullPathName();
        return result;
    }

    juce::File folder;
};

/** Kompletter Fahrstand: echte Registry/Hub/Library (Factory-AlphaTrack aus
    BinaryData) + Modell-Snapshots aus JSON; alle Seams als Captures. */
struct BridgeRig
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempSettings temp;
    MidiRigSettings settings { temp.options() };
    MidiPortHub hub { settings,
                      [] { return juce::Array<juce::MidiDeviceInfo>(); },
                      [] { return juce::Array<juce::MidiDeviceInfo>(); },
                      [] (const juce::String&, juce::MidiInputCallback&)
                          -> std::unique_ptr<conduit::midirig::InputPortHandle> { return nullptr; },
                      [] (const juce::String&)
                          -> std::unique_ptr<conduit::midirig::OutputPortHandle> { return nullptr; } };
    conduit::ControllerProfileLibrary library { juce::File() };   // nur Factory (BinaryData)
    conduit::LiveSetModel model;

    LiveRemoteBridge bridge { hub, settings, library, model };

    std::vector<juce::MidiMessage> midiOut;
    std::vector<juce::OSCMessage> commands;
    std::vector<juce::OSCMessage> touchValues;
    std::vector<juce::String> touchedKeys;
    bool live = true;
    double fakeNowMs = 10000.0;

    juce::Uuid deviceId;

    BridgeRig()
    {
        bridge.sendMidi        = [this] (const juce::MidiMessage& m) { midiOut.push_back (m); };
        bridge.sendCommand     = [this] (const juce::OSCMessage& m) { commands.push_back (m); return true; };
        bridge.sendTouchValue  = [this] (const juce::OSCMessage& m) { touchValues.push_back (m); };
        bridge.noteTouched     = [this] (const juce::String& k) { touchedKeys.push_back (k); };
        bridge.isLiveConnected = [this] { return live; };
        bridge.nowMs           = [this] { return fakeNowMs; };

        deviceId = settings.addDevice ("AlphaTrack", RigDeviceKind::controller);
        settings.setControllerProfileName (deviceId, "AlphaTrack");
        settings.setLiveRemoteDeviceId (deviceId);
        bridge.refreshFromRegistry();   // Broadcasts sind async -- direkt aufloesen

        // Standard-Live-Set: 2 Tracks, 2 Returns, tr:1 selektiert.
        applyTracks ("{\"selected\":\"tr:1\","
                     "\"tr:1\":{\"name\":\"Drums\",\"kind\":\"audio\",\"index\":0},"
                     "\"tr:2\":{\"name\":\"Bass\",\"kind\":\"midi\",\"index\":1},"
                     "\"ret:0\":{\"name\":\"Reverb\",\"kind\":\"return\",\"index\":0},"
                     "\"ret:1\":{\"name\":\"Delay\",\"kind\":\"return\",\"index\":1}}");
        applyMixer ("{\"tr:1\":{\"vol\":0.85,\"pan\":0.0,\"sends\":[0.25,0.5],"
                    "\"mute\":false,\"solo\":false,\"arm\":false}}");
        model.applySnapshot ("transport", juce::JSON::parse ("{\"bar\":17,\"beat\":3}"));

        midiOut.clear();   // Native-Mode-SysEx des Resolves nicht mitzaehlen
    }

    void applyTracks (const juce::String& json) { model.applySnapshot ("tracks", juce::JSON::parse (json)); }
    void applyMixer  (const juce::String& json) { model.applySnapshot ("mixer",  juce::JSON::parse (json)); }

    void pressNote (int note) { bridge.handleNote ({ 1, note, 127, true }); bridge.handleNote ({ 1, note, 0, false }); }
    void noteOn (int note)    { bridge.handleNote ({ 1, note, 127, true }); }
    void noteOff (int note)   { bridge.handleNote ({ 1, note, 0, false }); }

    void moveFader (float value01)
    {
        midi::ControllerEvent event;
        event.kind    = midi::ControllerEvent::Kind::pitchBend;
        event.channel = 1;
        event.value   = juce::roundToInt (value01 * 16383.0f);
        event.is14Bit = true;
        bridge.handleController (event);
    }

    /** Letzte gesendete Pitch-Bend-Nachricht an den Motor, -1 = keine. */
    [[nodiscard]] int lastMotorValue() const
    {
        for (auto it = midiOut.rbegin(); it != midiOut.rend(); ++it)
            if (it->isPitchWheel())
                return it->getPitchWheelValue();
        return -1;
    }

    [[nodiscard]] int motorSendCount() const
    {
        int count = 0;
        for (const auto& m : midiOut)
            if (m.isPitchWheel())
                ++count;
        return count;
    }

    /** Letzter LED-Zustand einer Note (-1 = nie gesendet). */
    [[nodiscard]] int lastLedValue (int note) const
    {
        for (auto it = midiOut.rbegin(); it != midiOut.rend(); ++it)
            if ((it->isNoteOn() || it->isNoteOff()) && it->getNoteNumber() == note)
                return it->getVelocity();
        return -1;
    }

    /** LCD-Zeileninhalt aus der letzten SysEx an Position pos (Zeile 2 = 0x10ff). */
    [[nodiscard]] juce::String lastLcdTextAt (int wantedPos) const
    {
        for (auto it = midiOut.rbegin(); it != midiOut.rend(); ++it)
        {
            if (! it->isSysEx())
                continue;
            const auto* data = it->getSysExData();   // ohne F0/F7
            const auto size = it->getSysExDataSize();
            if (size < 6 || data[4] != 0x00)
                continue;   // kein LCD-Frame (z. B. Native-Mode-Force)
            if ((int) data[5] != wantedPos)
                continue;
            juce::String text;
            for (int i = 6; i < size; ++i)
                text << (juce::juce_wchar) data[i];
            return text;
        }
        return {};
    }
};

// AlphaTrack-Noten (Factory-CSV): pan 42, f1..f4 54..57, shift 70,
// track_l/r 87/88, mute 16, solo 8, arm 0, fader-touch 104.

} // namespace

//==============================================================================
TEST_CASE ("Bridge: aktiv mit Rolle+Profil, inaktiv bei Grid-Konflikt", "[bridge]")
{
    BridgeRig rig;
    REQUIRE (rig.bridge.isActive());

    // Grid-Rolle auf dasselbe Geraet -> Konflikt -> inaktiv.
    rig.settings.setGridControllerDeviceId (rig.deviceId);
    rig.bridge.refreshFromRegistry();
    CHECK_FALSE (rig.bridge.isActive());

    // Fader-Events laufen ins Leere.
    rig.touchValues.clear();
    rig.moveFader (0.5f);
    CHECK (rig.touchValues.empty());
}

TEST_CASE ("Bridge: Resolve sendet das Native-Mode-Force-SysEx", "[bridge]")
{
    BridgeRig rig;
    rig.midiOut.clear();
    rig.bridge.refreshFromRegistry();

    REQUIRE (! rig.midiOut.empty());
    const auto& first = rig.midiOut.front();
    REQUIRE (first.isSysEx());
    const auto* data = first.getSysExData();
    CHECK (data[3] == 0x20);
    CHECK (data[4] == 0x01);   // Native-Mode-Force (nicht der LCD-Kanal 0x00)
}

TEST_CASE ("Bridge: Fader-Modi -- PAN/F-Tasten togglen, aktive Taste = Volume", "[bridge]")
{
    BridgeRig rig;
    using Mode = LiveRemoteBridge::FaderMode;

    CHECK (rig.bridge.faderMode() == Mode::volume);

    rig.pressNote (42);   // PAN
    CHECK (rig.bridge.faderMode() == Mode::pan);
    rig.pressNote (42);   // PAN erneut -> Volume
    CHECK (rig.bridge.faderMode() == Mode::volume);

    rig.pressNote (55);   // F2 -> Send 2 (Index 1)
    CHECK (rig.bridge.faderMode() == Mode::send);
    CHECK (rig.bridge.activeSendIndex() == 1);

    // SHIFT + F2 -> Send 6 (Index 5)
    rig.noteOn (70);
    rig.pressNote (55);
    rig.noteOff (70);
    CHECK (rig.bridge.activeSendIndex() == 5);

    // Dieselbe Ebene erneut -> Volume.
    rig.noteOn (70);
    rig.pressNote (55);
    rig.noteOff (70);
    CHECK (rig.bridge.faderMode() == Mode::volume);
}

TEST_CASE ("Bridge: Fader sendet OSC je Modus mit Suppression-Key", "[bridge]")
{
    BridgeRig rig;

    rig.moveFader (0.5f);
    REQUIRE (rig.touchValues.size() == 1);
    CHECK (rig.touchValues[0].getAddressPattern().toString() == "/live/track/set/volume");
    CHECK (rig.touchValues[0][0].getString() == "tr:1");
    CHECK (rig.touchValues[0][1].getFloat32() == Approx (0.5f).margin (0.001f));
    REQUIRE (! rig.touchedKeys.empty());
    CHECK (rig.touchedKeys.back().contains ("vol"));

    rig.pressNote (42);   // Pan-Modus
    rig.moveFader (1.0f);
    CHECK (rig.touchValues.back().getAddressPattern().toString() == "/live/track/set/panning");
    CHECK (rig.touchValues.back()[1].getFloat32() == Approx (1.0f).margin (0.001f));   // ganz rechts

    rig.pressNote (42);   // zurueck zu Volume
    rig.pressNote (54);   // F1 -> Send 1 (Index 0)
    rig.moveFader (0.25f);
    const auto& send = rig.touchValues.back();
    CHECK (send.getAddressPattern().toString() == "/live/track/set/send");
    CHECK (send[1].getInt32() == 0);
    CHECK (send[2].getFloat32() == Approx (0.25f).margin (0.001f));
}

TEST_CASE ("Bridge: Send-Index ueber vorhandene Sends hinaus ist inert", "[bridge]")
{
    BridgeRig rig;

    // Send 6 (Index 5) existiert nicht (Set hat 2 Sends).
    rig.noteOn (70);
    rig.pressNote (55);
    rig.noteOff (70);
    rig.touchValues.clear();

    rig.moveFader (0.7f);
    CHECK (rig.touchValues.empty());
}

TEST_CASE ("Bridge: Motor folgt dem Modellwert, dedupet, Touch gated", "[bridge]")
{
    BridgeRig rig;

    rig.bridge.tick();
    CHECK (rig.lastMotorValue() == juce::roundToInt (0.85f * 16383.0f));

    // Unveraendert -> kein zweiter Send.
    const auto count = rig.motorSendCount();
    rig.bridge.tick();
    CHECK (rig.motorSendCount() == count);

    // Finger auf dem Fader: Modellaenderung faehrt NICHT (Gate) ...
    rig.noteOn (104);
    rig.applyMixer ("{\"tr:1\":{\"vol\":0.2,\"pan\":0.0,\"sends\":[0.25,0.5],"
                    "\"mute\":false,\"solo\":false,\"arm\":false}}");
    rig.bridge.tick();
    CHECK (rig.motorSendCount() == count);

    // ... Loslassen faehrt nach.
    rig.noteOff (104);
    rig.bridge.tick();
    CHECK (rig.lastMotorValue() == juce::roundToInt (0.2f * 16383.0f));
}

TEST_CASE ("Bridge: Moduswechsel faehrt den Motor auf den neuen Zielwert", "[bridge]")
{
    BridgeRig rig;
    rig.bridge.tick();   // Volume 0.85

    rig.pressNote (54);   // Send 1 (Index 0) = 0.25
    rig.bridge.tick();
    CHECK (rig.lastMotorValue() == juce::roundToInt (0.25f * 16383.0f));

    rig.pressNote (42);   // Pan (0.0 -> Mitte = 0.5)
    rig.bridge.tick();
    CHECK (rig.lastMotorValue() == juce::roundToInt (0.5f * 16383.0f));
}

TEST_CASE ("Bridge: TRACK-Tasten navigieren mit Klemmen an den Enden", "[bridge]")
{
    BridgeRig rig;

    rig.pressNote (88);   // TRACK> -> tr:2
    REQUIRE (rig.commands.size() == 1);
    CHECK (rig.commands[0].getAddressPattern().toString() == "/live/song/set/selected_track");
    CHECK (rig.commands[0][0].getString() == "tr:2");

    // Am linken Ende geklemmt: tr:1 ist der erste Track -> kein Send.
    rig.commands.clear();
    rig.pressNote (87);   // TRACK<
    CHECK (rig.commands.empty());
}

TEST_CASE ("Bridge: Mute/Solo/Arm togglen den Modell-Zustand als Int", "[bridge]")
{
    BridgeRig rig;

    rig.pressNote (16);   // MUTE (aktuell false -> 1)
    REQUIRE (! rig.commands.empty());
    CHECK (rig.commands.back().getAddressPattern().toString() == "/live/track/set/mute");
    CHECK (rig.commands.back()[1].getInt32() == 1);

    rig.applyMixer ("{\"tr:1\":{\"vol\":0.85,\"pan\":0.0,\"sends\":[0.25,0.5],"
                    "\"mute\":true,\"solo\":false,\"arm\":false}}");
    rig.pressNote (16);   // MUTE (jetzt true -> 0)
    CHECK (rig.commands.back()[1].getInt32() == 0);
}

TEST_CASE ("Bridge: LEDs folgen dem Modell und dedupen", "[bridge]")
{
    BridgeRig rig;
    rig.bridge.tick();
    CHECK (rig.lastLedValue (16) == 0);   // mute aus

    rig.applyMixer ("{\"tr:1\":{\"vol\":0.85,\"pan\":0.0,\"sends\":[0.25,0.5],"
                    "\"mute\":true,\"solo\":false,\"arm\":false}}");
    rig.bridge.tick();
    CHECK (rig.lastLedValue (16) == 127);   // mute an

    // Modus-LED: F1 aktiv nach Send-Auswahl.
    rig.pressNote (54);
    rig.bridge.tick();
    CHECK (rig.lastLedValue (54) == 127);
    rig.pressNote (54);   // zurueck zu Volume
    rig.bridge.tick();
    CHECK (rig.lastLedValue (54) == 0);
}

TEST_CASE ("Bridge: LCD zeigt Track-Name und Song-Position", "[bridge]")
{
    BridgeRig rig;
    rig.bridge.tick();

    CHECK (rig.lastLcdTextAt (0x00).trim() == "Drums");
    CHECK (rig.lastLcdTextAt (0x10).trim() == "17.3");
}

TEST_CASE ("Bridge: Fader-Touch zeigt das aktive Ziel, Loslassen die Position", "[bridge]")
{
    BridgeRig rig;
    rig.bridge.tick();
    rig.fakeNowMs += LiveRemoteBridge::kOverrideMs + 1.0;   // Startup-Override ablaufen lassen

    rig.noteOn (104);   // Fader anfassen (Volume-Modus, 0.85 -> 0.0 dB)
    rig.bridge.tick();
    CHECK (rig.lastLcdTextAt (0x10).startsWith ("Vol 0.0"));

    rig.noteOff (104);
    rig.fakeNowMs += LiveRemoteBridge::kOverrideMs + 1.0;
    rig.bridge.tick();
    CHECK (rig.lastLcdTextAt (0x10).trim() == "17.3");
}

TEST_CASE ("Bridge: Send-Touch zeigt den Return-Track-Namen", "[bridge]")
{
    BridgeRig rig;
    rig.bridge.tick();

    rig.pressNote (55);   // F2 -> Send 2 (Index 1) = Return "Delay", Wert 0.5
    rig.noteOn (104);
    rig.bridge.tick();

    const auto line2 = rig.lastLcdTextAt (0x10);
    CHECK (line2.startsWith ("Delay"));
    CHECK (line2.contains ("50%"));
}

TEST_CASE ("Bridge: disconnected -> LCD offline, LEDs aus, keine Sends", "[bridge]")
{
    BridgeRig rig;
    rig.applyMixer ("{\"tr:1\":{\"vol\":0.85,\"pan\":0.0,\"sends\":[0.25,0.5],"
                    "\"mute\":true,\"solo\":false,\"arm\":false}}");
    rig.bridge.tick();
    CHECK (rig.lastLedValue (16) == 127);

    rig.live = false;
    rig.bridge.tick();
    CHECK (rig.lastLedValue (16) == 0);
    CHECK (rig.lastLcdTextAt (0x00).startsWith ("Live offline"));

    rig.touchValues.clear();
    rig.commands.clear();
    rig.moveFader (0.5f);
    rig.pressNote (16);
    CHECK (rig.touchValues.empty());
    CHECK (rig.commands.empty());
}

TEST_CASE ("Bridge: geloeschter/unbekannter Track -> Motor haelt, LCD '-'", "[bridge]")
{
    BridgeRig rig;
    rig.bridge.tick();
    const auto count = rig.motorSendCount();

    rig.applyTracks ("{\"selected\":\"tr:99\"}");   // Selektion zeigt ins Leere
    rig.bridge.tick();

    CHECK (rig.motorSendCount() == count);   // kein Ziel -> Motor steht
    CHECK (rig.lastLcdTextAt (0x00).trim() == "-");
}

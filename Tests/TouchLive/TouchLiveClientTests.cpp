#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TouchLive/LiveSetModel.h"
#include "TouchLive/TouchLiveClient.h"
#include "TouchLive/TouchLiveMeterBus.h"
#include "TouchLive/TouchLiveSettings.h"

using Catch::Approx;

namespace
{

//==============================================================================
/** Persistenz in ein Temp-Verzeichnis statt in die echte Settings-Datei. */
struct TempTouchLiveSettings
{
    TempTouchLiveSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitTouchLiveSettingsTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempTouchLiveSettings() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ConduitTouchLiveSettingsTests";
        o.filenameSuffix  = ".settings";
        o.folderName      = folder.getFullPathName();  // absoluter Pfad
        return o;
    }

    juce::File folder;
};

//==============================================================================
/** Fängt Sends statt UDP und speist Empfang synchron ein — der
    IRemoteTransport-Seam des TouchLiveClient (Muster IOscSink, 7.3). */
struct FakeRemoteTransport final : conduit::IRemoteTransport
{
    void setMessageHandler (MessageHandler handlerToUse) override
    {
        handler = std::move (handlerToUse);
    }

    bool connect (const juce::String& h, int cp, int lp) override
    {
        host = h;
        commandPort = cp;
        listenPort = lp;
        ++connectCalls;
        return connectResult;
    }

    void disconnect() override { ++disconnectCalls; }

    bool send (const juce::OSCMessage& message) override
    {
        sent.push_back (message);
        return true;
    }

    /** Simuliert den Netzwerk-Thread: Message an den Client liefern. */
    void deliver (const juce::OSCMessage& message)
    {
        REQUIRE (handler != nullptr);
        handler (message);
    }

    [[nodiscard]] int countSent (const juce::String& address) const
    {
        int count = 0;

        for (const auto& message : sent)
            if (message.getAddressPattern().toString() == address)
                ++count;

        return count;
    }

    [[nodiscard]] std::vector<juce::OSCMessage> sentTo (const juce::String& address) const
    {
        std::vector<juce::OSCMessage> filtered;

        for (const auto& message : sent)
            if (message.getAddressPattern().toString() == address)
                filtered.push_back (message);

        return filtered;
    }

    MessageHandler handler;
    juce::String host;
    int commandPort = 0;
    int listenPort = 0;
    int connectCalls = 0;
    int disconnectCalls = 0;
    bool connectResult = true;
    std::vector<juce::OSCMessage> sent;
};

//==============================================================================
[[nodiscard]] juce::OSCMessage stateMessage (const juce::String& domainName, const char* kind,
                                             int seq, int chunk, int chunks,
                                             const juce::String& json)
{
    juce::OSCMessage message { juce::OSCAddressPattern ("/remote/state/" + domainName
                                                        + "/" + kind) };
    message.addInt32 (seq);
    message.addInt32 (chunk);
    message.addInt32 (chunks);
    message.addString (json);
    return message;
}

[[nodiscard]] juce::OSCMessage pongMessage()
{
    juce::OSCMessage message { juce::OSCAddressPattern ("/remote/pong") };
    message.addInt32 (1);  // PROTOCOL_VERSION
    return message;
}

//==============================================================================
/** Gemeinsames Setup: Client mit Fake-Transport und Fake-Clock. */
struct ClientRig
{
    ClientRig()
        : settings (temp.options())
    {
        auto transportOwned = std::make_unique<FakeRemoteTransport>();
        transport = transportOwned.get();
        client = std::make_unique<conduit::TouchLiveClient> (model, meterBus, settings,
                                                             std::move (transportOwned));
        client->setTimeSource ([this] { return fakeNowMs; });
    }

    void enable()
    {
        settings.setEnabled (true);
        settings.dispatchPendingMessages();  // synchrone Zustellung
    }

    /** Deliver + synchroner Message-Thread-Hop. */
    void deliver (const juce::OSCMessage& message)
    {
        transport->deliver (message);
        client->flushPendingMessages();
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempTouchLiveSettings temp;
    conduit::LiveSetModel model;
    conduit::TouchLiveMeterBus meterBus;
    conduit::TouchLiveSettings settings;
    double fakeNowMs = 1000.0;
    FakeRemoteTransport* transport = nullptr;
    std::unique_ptr<conduit::TouchLiveClient> client;
};

} // namespace

//==============================================================================
TEST_CASE ("TouchLiveSettings: Defaults und Persistenz-Roundtrip", "[touchlive][io]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempTouchLiveSettings temp;

    {
        conduit::TouchLiveSettings settings (temp.options());
        REQUIRE (settings.getHost() == juce::String (conduit::TouchLiveSettings::defaultHost));
        REQUIRE (settings.getCommandPort() == conduit::TouchLiveSettings::defaultCommandPort);
        REQUIRE (settings.getListenPort() == conduit::TouchLiveSettings::defaultListenPort);
        REQUIRE_FALSE (settings.isEnabled());

        settings.setHost ("192.168.1.42");
        settings.setCommandPort (9020);
        settings.setListenPort (9021);
        settings.setEnabled (true);
    }

    conduit::TouchLiveSettings reloaded (temp.options());
    REQUIRE (reloaded.getHost() == "192.168.1.42");
    REQUIRE (reloaded.getCommandPort() == 9020);
    REQUIRE (reloaded.getListenPort() == 9021);
    REQUIRE (reloaded.isEnabled());
}

TEST_CASE ("TouchLiveSettings: ungültige Werte werden verworfen", "[touchlive]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempTouchLiveSettings temp;
    conduit::TouchLiveSettings settings (temp.options());

    settings.setHost ("  ");
    settings.setCommandPort (0);
    settings.setListenPort (70000);

    REQUIRE (settings.getHost() == juce::String (conduit::TouchLiveSettings::defaultHost));
    REQUIRE (settings.getCommandPort() == conduit::TouchLiveSettings::defaultCommandPort);
    REQUIRE (settings.getListenPort() == conduit::TouchLiveSettings::defaultListenPort);
}

//==============================================================================
TEST_CASE ("TouchLiveClient: disabled sendet nie", "[touchlive]")
{
    ClientRig rig;

    REQUIRE (rig.client->getStatus() == conduit::TouchLiveClient::Status::disabled);
    REQUIRE_FALSE (rig.client->sendCommand (
        juce::OSCMessage (juce::OSCAddressPattern ("/live/song/start_playing"))));
    rig.client->sendTouchValue (
        juce::OSCMessage (juce::OSCAddressPattern ("/live/track/set/volume")));

    REQUIRE (rig.transport->sent.empty());
    REQUIRE (rig.transport->connectCalls == 0);
}

TEST_CASE ("TouchLiveClient: enable verbindet und abonniert alle Domains (get + subscribe)", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    REQUIRE (rig.transport->connectCalls == 1);
    REQUIRE (rig.transport->host == juce::String (conduit::TouchLiveSettings::defaultHost));
    REQUIRE (rig.transport->commandPort == conduit::TouchLiveSettings::defaultCommandPort);
    REQUIRE (rig.transport->listenPort == conduit::TouchLiveSettings::defaultListenPort);
    REQUIRE (rig.client->getStatus() == conduit::TouchLiveClient::Status::connecting);

    for (const auto* domainName : conduit::TouchLiveClient::domainNames)
    {
        const auto base = "/remote/state/" + juce::String (domainName);
        REQUIRE (rig.transport->countSent (base + "/get") == 1);
        REQUIRE (rig.transport->countSent (base + "/subscribe") == 1);
    }

    REQUIRE (rig.transport->countSent ("/remote/meters/subscribe") == 1);
    REQUIRE (rig.transport->countSent ("/remote/ping") == 1);
}

//==============================================================================
TEST_CASE ("TouchLiveClient: /remote/meters füllt den MeterBus (roh, ohne Suppression)", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    // Sogar berührte Keys ändern die Meter — Meter sind von der
    // Echo-Suppression ausgenommen (Feel-Regel 5.1)
    rig.client->noteTouchedParameter (
        conduit::TouchLiveClient::makeParameterKey ("mixer", "tr:0", "vol"));

    // clear() (Enable/Disable) zählt den Frame-Zähler ebenfalls hoch —
    // relativ zur Baseline prüfen
    const auto baseline = rig.meterBus.getFrameCounter();

    juce::OSCMessage frame { juce::OSCAddressPattern ("/remote/meters") };
    frame.addString ("tr:0"); frame.addFloat32 (0.6f); frame.addFloat32 (0.4f);
    frame.addString ("ma:1"); frame.addFloat32 (0.9f); frame.addFloat32 (0.9f);
    rig.deliver (frame);

    REQUIRE (rig.meterBus.getFrameCounter() == baseline + 1);
    REQUIRE (rig.meterBus.getLevel ("tr:0").left == Approx (0.6f));
    REQUIRE (rig.meterBus.getLevel ("tr:0").right == Approx (0.4f));
    REQUIRE (rig.meterBus.getLevel ("ma:1").left == Approx (0.9f));
    REQUIRE (rig.meterBus.getLevel ("unbekannt").left == Approx (0.0f));
    REQUIRE (rig.client->getStats().meterFrames == 1);
}

TEST_CASE ("TouchLiveClient: defekter Meter-Frame wird verworfen", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    const auto baseline = rig.meterBus.getFrameCounter();

    juce::OSCMessage broken { juce::OSCAddressPattern ("/remote/meters") };
    broken.addString ("tr:0"); broken.addString ("kaputt"); broken.addFloat32 (0.5f);
    rig.deliver (broken);

    REQUIRE (rig.meterBus.getFrameCounter() == baseline);
    REQUIRE (rig.client->getStats().meterFrames == 0);
}

TEST_CASE ("TouchLiveClient: Disable leert den MeterBus", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    juce::OSCMessage frame { juce::OSCAddressPattern ("/remote/meters") };
    frame.addString ("tr:0"); frame.addFloat32 (0.8f); frame.addFloat32 (0.8f);
    rig.deliver (frame);
    REQUIRE (rig.meterBus.getLevel ("tr:0").left == Approx (0.8f));

    rig.settings.setEnabled (false);
    rig.settings.dispatchPendingMessages();

    REQUIRE (rig.meterBus.getLevel ("tr:0").left == Approx (0.0f));
}

TEST_CASE ("TouchLiveClient: Snapshot läuft ins LiveSetModel", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    rig.deliver (stateMessage ("mixer", "snapshot", 1, 1, 1,
        R"({"tr:0":{"volume":0.85,"pan":0.0},"ma:0":{"volume":0.9,"pan":0.0}})"));

    auto item = rig.model.findItem ("mixer", "tr:0");
    REQUIRE (item.isValid());
    REQUIRE (static_cast<double> (item.getProperty ("volume")) == Approx (0.85));
    REQUIRE (rig.model.findItem ("mixer", "ma:0").isValid());
}

TEST_CASE ("TouchLiveClient: Diff in Folge-Sequenz wird angewendet, Duplikat ignoriert", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    rig.deliver (stateMessage ("transport", "snapshot", 3, 1, 1,
                               R"({"is_playing":false,"tempo":120.0})"));
    rig.deliver (stateMessage ("transport", "diff", 4, 1, 1, R"({"tempo":124.0})"));

    auto transport = rig.model.getDomain ("transport");
    REQUIRE (static_cast<double> (transport.getProperty ("tempo")) == Approx (124.0));

    // Dieselbe (oder ältere) Sequenz noch einmal → keine Anwendung
    rig.deliver (stateMessage ("transport", "diff", 4, 1, 1, R"({"tempo":90.0})"));
    REQUIRE (static_cast<double> (transport.getProperty ("tempo")) == Approx (124.0));
}

TEST_CASE ("TouchLiveClient: Seq-Lücke fordert genau einen Snapshot an (/get)", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    rig.deliver (stateMessage ("mixer", "snapshot", 1, 1, 1, R"({"tr:0":{"volume":0.5}})"));
    rig.transport->sent.clear();

    // seq 3 nach 1 → Lücke: Diff verwerfen, /get senden
    rig.deliver (stateMessage ("mixer", "diff", 3, 1, 1, R"({"tr:0":{"volume":0.9}})"));

    REQUIRE (static_cast<double> (rig.model.findItem ("mixer", "tr:0")
                                      .getProperty ("volume")) == Approx (0.5));
    REQUIRE (rig.transport->countSent ("/remote/state/mixer/get") == 1);

    // Weitere Lücken-Diffs lösen KEINEN weiteren Request aus (Drossel) …
    rig.deliver (stateMessage ("mixer", "diff", 4, 1, 1, R"({"tr:0":{"volume":0.9}})"));
    REQUIRE (rig.transport->countSent ("/remote/state/mixer/get") == 1);

    // … erst der nächste Heartbeat-Tick erlaubt den Retry (verlorener /get)
    rig.client->runHeartbeatTick();
    rig.deliver (stateMessage ("mixer", "diff", 5, 1, 1, R"({"tr:0":{"volume":0.9}})"));
    REQUIRE (rig.transport->countSent ("/remote/state/mixer/get") == 2);

    // Der Snapshot heilt: danach greifen Folge-Diffs wieder
    rig.deliver (stateMessage ("mixer", "snapshot", 6, 1, 1, R"({"tr:0":{"volume":0.6}})"));
    rig.deliver (stateMessage ("mixer", "diff", 7, 1, 1, R"({"tr:0":{"volume":0.7}})"));
    REQUIRE (static_cast<double> (rig.model.findItem ("mixer", "tr:0")
                                      .getProperty ("volume")) == Approx (0.7));
}

TEST_CASE ("TouchLiveClient: Chunk-Reassembly in verkehrter Reihenfolge", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    // Chunk 2 zuerst — nichts darf angewendet werden, bis alles da ist
    rig.deliver (stateMessage ("tracks", "snapshot", 1, 2, 2,
                               R"({"tr:1":{"name":"Bass","index":1}})"));
    REQUIRE_FALSE (rig.model.findItem ("tracks", "tr:1").isValid());

    rig.deliver (stateMessage ("tracks", "snapshot", 1, 1, 2,
                               R"({"tr:0":{"name":"Drums","index":0}})"));

    REQUIRE (rig.model.findItem ("tracks", "tr:0").isValid());
    REQUIRE (rig.model.findItem ("tracks", "tr:1").isValid());
    REQUIRE (rig.model.findItem ("tracks", "tr:1").getProperty ("name").toString() == "Bass");
}

TEST_CASE ("TouchLiveClient: Echo-Suppression verwirft berührte Keys, Release gibt frei", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    rig.deliver (stateMessage ("mixer", "snapshot", 1, 1, 1,
                               R"({"tr:0":{"volume":0.5,"pan":0.0}})"));

    rig.client->noteTouchedParameter (
        conduit::TouchLiveClient::makeParameterKey ("mixer", "tr:0", "volume"));

    // Eingehender Diff während der Berührung: volume verworfen, pan greift
    rig.deliver (stateMessage ("mixer", "diff", 2, 1, 1,
                               R"({"tr:0":{"volume":0.9,"pan":0.25}})"));

    auto item = rig.model.findItem ("mixer", "tr:0");
    REQUIRE (static_cast<double> (item.getProperty ("volume")) == Approx (0.5));
    REQUIRE (static_cast<double> (item.getProperty ("pan")) == Approx (0.25));

    // Nach 250 ms Release greift der Wert wieder
    rig.fakeNowMs += conduit::TouchLiveClient::echoSuppressionReleaseMs + 1.0;
    rig.deliver (stateMessage ("mixer", "diff", 3, 1, 1,
                               R"({"tr:0":{"volume":0.9,"pan":0.25}})"));
    REQUIRE (static_cast<double> (item.getProperty ("volume")) == Approx (0.9));
}

TEST_CASE ("TouchLiveClient: applyLocalMixerValue schreibt optimistisch und suppress't das Echo", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    rig.deliver (stateMessage ("mixer", "snapshot", 1, 1, 1,
                               R"({"tr:0":{"vol":0.5,"pan":0.0,"sends":[0.1,0.2]}})"));

    // Optimistischer Edit (wie ihn ein UI-Strip beim Fader-Drag macht): das
    // Modell traegt den Wert SOFORT -> andere lokale Controller sehen ihn.
    rig.client->applyLocalMixerValue ("tr:0", "vol", 0.8);
    auto item = rig.model.findItem ("mixer", "tr:0");
    REQUIRE (static_cast<double> (item.getProperty ("vol")) == Approx (0.8));

    // Lives (redundantes) Echo waehrend der Suppression wird verworfen (Diffs
    // tragen das ganze Mixer-Objekt der Gegenseite -> vol suppressed, Rest greift).
    rig.deliver (stateMessage ("mixer", "diff", 2, 1, 1,
                               R"({"tr:0":{"vol":0.5,"pan":0.0,"sends":[0.1,0.2]}})"));
    REQUIRE (static_cast<double> (item.getProperty ("vol")) == Approx (0.8));

    // ... nach dem Release greift Lives Wert wieder.
    rig.fakeNowMs += conduit::TouchLiveClient::echoSuppressionReleaseMs + 1.0;
    rig.deliver (stateMessage ("mixer", "diff", 3, 1, 1,
                               R"({"tr:0":{"vol":0.42,"pan":0.0,"sends":[0.1,0.2]}})"));
    REQUIRE (static_cast<double> (item.getProperty ("vol")) == Approx (0.42));

    // Array-Element (Send)
    rig.client->applyLocalMixerArrayElement ("tr:0", "sends", 1, 0.66);
    const auto* sends = rig.model.findItem ("mixer", "tr:0").getProperty ("sends").getArray();
    REQUIRE (sends != nullptr);
    REQUIRE (static_cast<double> (sends->getReference (1)) == Approx (0.66));
}

TEST_CASE ("TouchLiveClient: Touch-Thinning — 100 Sends in 100 ms werden gedrosselt, letzter Wert gewinnt", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    const juce::String address = "/live/track/set/volume";
    const auto start = rig.fakeNowMs;

    for (int i = 0; i < 100; ++i)
    {
        rig.fakeNowMs = start + i;  // 1 ms Abstand → 100 Sends in 100 ms

        juce::OSCMessage message { juce::OSCAddressPattern (address) };
        message.addInt32 (0);
        message.addFloat32 (static_cast<float> (i) / 100.0f);
        rig.client->sendTouchValue (message);
    }

    // Nachzügler flushen (in der App macht das der Thinning-Timer)
    rig.fakeNowMs = start + 200.0;
    rig.client->flushPendingTouchValues();

    const auto datagrams = rig.transport->sentTo (address);
    REQUIRE (static_cast<int> (datagrams.size()) <= 8);   // ~16-ms-Fenster → ~7 (+1 Flush)
    REQUIRE (static_cast<int> (datagrams.size()) >= 2);
    REQUIRE (datagrams.back()[1].getFloat32() == Approx (0.99f));  // letzter Wert
}

TEST_CASE ("TouchLiveClient: Thinning trennt Ziele derselben Adresse (M5)", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    const juce::String address = "/live/device/set/parameter";

    const auto send = [&rig, &address] (int parameterIndex, float value)
    {
        juce::OSCMessage message { juce::OSCAddressPattern (address) };
        message.addString ("dv:1");
        message.addInt32 (parameterIndex);
        message.addFloat32 (value);
        rig.client->sendTouchValue (message);
    };

    // EQ-Punkt-Drag (M5): Frequenz + Gain im SELBEN Fenster — verschiedene
    // Ziele derselben Adresse dürfen sich nicht gegenseitig wegdrosseln
    send (3, 0.5f);
    send (4, 2.0f);

    auto sent = rig.transport->sentTo (address);
    REQUIRE (sent.size() == 2);
    REQUIRE (sent[0][1].getInt32() == 3);
    REQUIRE (sent[1][1].getInt32() == 4);

    // Dasselbe Ziel bleibt im Fenster gedrosselt (letzter Wert gewinnt)
    send (3, 0.6f);
    REQUIRE (rig.transport->sentTo (address).size() == 2);

    rig.fakeNowMs += 20.0;
    rig.client->flushPendingTouchValues();
    sent = rig.transport->sentTo (address);
    REQUIRE (sent.size() == 3);
    REQUIRE (sent.back()[1].getInt32() == 3);
    REQUIRE (sent.back()[2].getFloat32() == Approx (0.6f));
}

TEST_CASE ("TouchLiveClient: Heartbeat-Timeout → disconnected, Reconnect subscribed neu", "[touchlive]")
{
    ClientRig rig;
    rig.enable();

    rig.deliver (pongMessage());
    REQUIRE (rig.client->getStatus() == conduit::TouchLiveClient::Status::connected);

    // 3 unbeantwortete Pings → der vierte Tick kippt auf disconnected
    rig.client->runHeartbeatTick();
    rig.client->runHeartbeatTick();
    rig.client->runHeartbeatTick();
    REQUIRE (rig.client->getStatus() == conduit::TouchLiveClient::Status::connected);
    rig.client->runHeartbeatTick();
    REQUIRE (rig.client->getStatus() == conduit::TouchLiveClient::Status::disconnected);

    // Wiederverbindung: Pong → connected + frisches get/subscribe pro Domain
    rig.transport->sent.clear();
    rig.deliver (pongMessage());
    REQUIRE (rig.client->getStatus() == conduit::TouchLiveClient::Status::connected);

    for (const auto* domainName : conduit::TouchLiveClient::domainNames)
    {
        const auto base = "/remote/state/" + juce::String (domainName);
        REQUIRE (rig.transport->countSent (base + "/get") == 1);
        REQUIRE (rig.transport->countSent (base + "/subscribe") == 1);
    }
}

TEST_CASE ("TouchLiveClient: Diff vor dem ersten Snapshot fordert Snapshot an", "[touchlive]")
{
    ClientRig rig;
    rig.enable();
    rig.transport->sent.clear();

    rig.deliver (stateMessage ("session", "diff", 7, 1, 1, R"({"scene:sc:0":{"name":"X"}})"));

    REQUIRE_FALSE (rig.model.findItem ("session", "scene:sc:0").isValid());
    REQUIRE (rig.transport->countSent ("/remote/state/session/get") == 1);
}

TEST_CASE ("TouchLiveClient: Deaktivieren trennt und setzt Status disabled", "[touchlive]")
{
    ClientRig rig;
    rig.enable();
    rig.deliver (pongMessage());
    REQUIRE (rig.client->getStatus() == conduit::TouchLiveClient::Status::connected);

    rig.settings.setEnabled (false);
    rig.settings.dispatchPendingMessages();

    REQUIRE (rig.client->getStatus() == conduit::TouchLiveClient::Status::disabled);
    REQUIRE_FALSE (rig.client->sendCommand (
        juce::OSCMessage (juce::OSCAddressPattern ("/live/song/stop_playing"))));
}

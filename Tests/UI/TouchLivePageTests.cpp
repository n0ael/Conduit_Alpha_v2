#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TouchLive/LiveFaderScale.h"
#include "TouchLive/LiveSetModel.h"
#include "TouchLive/TouchLiveClient.h"
#include "TouchLive/TouchLiveMeterBus.h"
#include "TouchLive/TouchLiveSettings.h"
#include "UI/TouchLivePage/TouchLivePage.h"

using Catch::Approx;

namespace
{

//==============================================================================
struct TempTouchLiveSettings
{
    TempTouchLiveSettings()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ConduitTouchLivePageTests")
                      .getChildFile (juce::Uuid().toString()))
    {
        folder.createDirectory();
    }

    ~TempTouchLiveSettings() { folder.deleteRecursively(); }

    [[nodiscard]] juce::PropertiesFile::Options options() const
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ConduitTouchLivePageTests";
        o.filenameSuffix  = ".settings";
        o.folderName      = folder.getFullPathName();
        return o;
    }

    juce::File folder;
};

//==============================================================================
struct FakeRemoteTransport final : conduit::IRemoteTransport
{
    void setMessageHandler (MessageHandler handlerToUse) override { handler = std::move (handlerToUse); }

    bool connect (const juce::String&, int, int) override { return true; }
    void disconnect() override {}

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
    std::vector<juce::OSCMessage> sent;
};

//==============================================================================
[[nodiscard]] juce::var parse (const char* json)
{
    auto result = juce::JSON::parse (juce::String::fromUTF8 (json));
    REQUIRE (result.getDynamicObject() != nullptr);
    return result;
}

/** Gemeinsames Setup: Modell direkt gefüttert (Anzeige), Client mit
    Fake-Transport (Kommando-Pfad), Page mit fester Größe. */
struct PageRig
{
    PageRig()
        : settings (temp.options())
    {
        auto transportOwned = std::make_unique<FakeRemoteTransport>();
        transport = transportOwned.get();
        client = std::make_unique<conduit::TouchLiveClient> (model, meterBus, settings,
                                                             std::move (transportOwned));
        page = std::make_unique<conduit::TouchLivePage> (model, *client, meterBus, settings);
        page->setBounds (0, 0, 720, 460);

        settings.setEnabled (true);          // Kommando-Pfad scharf
        settings.dispatchPendingMessages();
        transport->sent.clear();             // Subscribe-/Ping-Traffic weg
    }

    /** Deliver + synchroner Message-Thread-Hop. */
    void deliver (const juce::OSCMessage& message)
    {
        transport->deliver (message);
        client->flushPendingMessages();
    }

    void loadDemoSet()
    {
        model.applySnapshot ("tracks", parse (
            R"({"tr:0":{"name":"Drums","color":16711680,"kind":"audio","index":0},)"
            R"("tr:1":{"name":"Keys","color":255,"kind":"midi","index":1},)"
            R"("rt:0":{"name":"A Reverb","color":65280,"kind":"return","index":0},)"
            R"("ma:0":{"name":"Master","color":8421504,"kind":"master","index":0}})"));

        model.applySnapshot ("mixer", parse (
            R"({"tr:0":{"vol":0.85,"pan":0.0,"sends":[0.1,0.2],"mute":false,"solo":false,"arm":false},)"
            R"("tr:1":{"vol":0.5,"pan":-0.25,"sends":[0.0,0.0],"mute":true,"solo":false,"arm":true},)"
            R"("rt:0":{"vol":0.7,"pan":0.0,"sends":[],"mute":false,"solo":false,"arm":false},)"
            R"("ma:0":{"vol":0.9,"pan":0.0}})"));

        model.applySnapshot ("session", parse (
            R"({"scene:sc:0":{"name":"Intro","color":255,"index":0},)"
            R"("scene:sc:1":{"name":"Drop","color":255,"index":1},)"
            R"("grid:tr:0":[{"name":"Beat","color":16711680,"state":"playing"},null],)"
            R"("grid:tr:1":[null,{"name":"Chords","color":255,"state":"stopped"}]})"));

        // Devices (M3): EQ mit 10 Parametern (2 Bänke) + Compressor auf tr:0
        juce::String parmeta = R"([{"name":"Device On","min":0,"max":1,"quant":false})";
        juce::String parvals = "[1.0";

        for (int i = 1; i <= 10; ++i)
        {
            parmeta << R"(,{"name":"P)" << juce::String (i) << R"(","min":0,"max":1,"quant":false})";
            parvals << "," << juce::String (0.1 * i, 2);
        }

        parmeta << "]";
        parvals << "]";

        model.applySnapshot ("devices", parse ((
            juce::String (R"({"chain:tr:0":["dv:1","dv:2"],"chain:tr:1":[],)"
                          R"("chain:rt:0":[],"chain:ma:0":[],)"
                          R"("dev:dv:1":{"name":"EQ Eight","class_name":"Eq8","is_active":true},)"
                          R"("dev:dv:2":{"name":"Compressor","class_name":"Compressor2","is_active":true},)"
                          R"("parmeta:dv:2":[{"name":"Device On","min":0,"max":1,"quant":false},)"
                          R"({"name":"Model","min":0,"max":2,"quant":true,"items":["Peak","RMS","Expand"]}],)"
                          R"("parvals:dv:2":[1.0,1.0],)")
            + R"("parmeta:dv:1":)" + parmeta + R"(,"parvals:dv:1":)" + parvals + "}").toRawUTF8()));

        page->mixerView.flushPendingRebuild();
        page->gridView.flushPendingRebuild();
        page->deviceView.flushPendingRebuild();
        page->resized();
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempTouchLiveSettings temp;
    conduit::LiveSetModel model;
    conduit::TouchLiveMeterBus meterBus;
    conduit::TouchLiveSettings settings;
    FakeRemoteTransport* transport = nullptr;
    std::unique_ptr<conduit::TouchLiveClient> client;
    std::unique_ptr<conduit::TouchLivePage> page;
};

} // namespace

//==============================================================================
TEST_CASE ("TouchLivePage: GRID ist default, Sub-Tabs schalten genau eine Ansicht", "[touchlive][ui]")
{
    PageRig rig;

    REQUIRE (rig.page->getSubTab() == conduit::TouchLivePage::tabGrid);
    REQUIRE (rig.page->gridView.isVisible());
    REQUIRE_FALSE (rig.page->mixerView.isVisible());

    rig.page->mixerTabTile.onClick();
    REQUIRE (rig.page->getSubTab() == conduit::TouchLivePage::tabMixer);
    REQUIRE (rig.page->mixerView.isVisible());
    REQUIRE_FALSE (rig.page->gridView.isVisible());
    REQUIRE (rig.page->mixerTabTile.isActive());
    REQUIRE_FALSE (rig.page->gridTabTile.isActive());

    rig.page->deviceTabTile.onClick();
    REQUIRE_FALSE (rig.page->mixerView.isVisible());
}

TEST_CASE ("TouchLiveMixerView: Kanalzüge aus tracks+mixer (Join über Stable-ID)", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto& mixer = rig.page->mixerView;
    REQUIRE (mixer.getStripCount() == 3);   // 2 Tracks + 1 Return; Master separat
    REQUIRE (mixer.getMasterStrip() != nullptr);

    auto* drums = mixer.findStrip ("tr:0");
    REQUIRE (drums != nullptr);
    REQUIRE (drums->fader.getDisplayedValue() == Approx (0.85f));
    REQUIRE (drums->sendSliders.size() == 2);
    REQUIRE (drums->activatorTile.isActive());        // nicht gemutet → gelb

    auto* keys = mixer.findStrip ("tr:1");
    REQUIRE (keys != nullptr);
    REQUIRE_FALSE (keys->activatorTile.isActive());   // gemutet → dunkel
    REQUIRE (keys->armTile.isActive());

    auto* reverb = mixer.findStrip ("rt:0");
    REQUIRE (reverb != nullptr);
    REQUIRE (reverb->getKind() == "return");
    REQUIRE (reverb->sendSliders.empty());            // M1a kann Return-Sends nicht setzen
    REQUIRE_FALSE (reverb->activatorTile.isVisible());
}

TEST_CASE ("TouchLiveMixerView: Fader-Drag sendet Touch-Wert mit Stable-ID", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto* drums = rig.page->mixerView.findStrip ("tr:0");
    REQUIRE (drums != nullptr);
    drums->fader.setBounds (0, 0, 60, 300);

    drums->fader.beginGesture();
    drums->fader.dragGestureTo (30.0f, false);   // nach unten → leiser
    drums->fader.endGesture();

    const auto sent = rig.transport->sentTo ("/live/track/set/volume");
    REQUIRE_FALSE (sent.empty());
    REQUIRE (sent.back()[0].isString());
    REQUIRE (sent.back()[0].getString() == "tr:0");
    REQUIRE (sent.back()[1].getFloat32() < 0.85f);
    REQUIRE (sent.back()[1].getFloat32() == Approx (drums->fader.getDisplayedValue()));
}

TEST_CASE ("TouchLiveMixerView: Master und Return adressieren ihre eigenen Pfade", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto* master = rig.page->mixerView.getMasterStrip();
    REQUIRE (master != nullptr);
    master->fader.setBounds (0, 0, 60, 300);
    master->fader.beginGesture();
    master->fader.dragGestureTo (10.0f, false);
    master->fader.endGesture();

    const auto masterSent = rig.transport->sentTo ("/live/master/set/volume");
    REQUIRE_FALSE (masterSent.empty());
    REQUIRE (masterSent.back()[0].isFloat32());   // kein track_ref

    auto* reverb = rig.page->mixerView.findStrip ("rt:0");
    REQUIRE (reverb != nullptr);
    reverb->fader.setBounds (0, 0, 60, 300);
    reverb->fader.beginGesture();
    reverb->fader.dragGestureTo (10.0f, false);
    reverb->fader.endGesture();

    const auto returnSent = rig.transport->sentTo ("/live/return/set/volume");
    REQUIRE_FALSE (returnSent.empty());
    REQUIRE (returnSent.back()[0].isInt32());     // Return-Index
    REQUIRE (returnSent.back()[0].getInt32() == 0);
}

TEST_CASE ("TouchLiveMixerView: Mute-Toggle optimistisch + Command", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto* drums = rig.page->mixerView.findStrip ("tr:0");
    REQUIRE (drums != nullptr);
    REQUIRE (drums->activatorTile.isActive());

    drums->activatorTile.onClick();

    REQUIRE_FALSE (drums->activatorTile.isActive());   // sofort, ohne Live-Feedback

    const auto sent = rig.transport->sentTo ("/live/track/set/mute");
    REQUIRE (sent.size() == 1);
    REQUIRE (sent.front()[0].getString() == "tr:0");
    REQUIRE (sent.front()[1].getInt32() == 1);
}

TEST_CASE ("TouchLiveMixerView: Doppeltipp setzt auf 0 dB", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto* keys = rig.page->mixerView.findStrip ("tr:1");
    REQUIRE (keys != nullptr);
    keys->fader.setBounds (0, 0, 60, 300);

    keys->fader.resetToUnity();

    REQUIRE (keys->fader.getDisplayedValue()
             == Approx ((float) conduit::touchlive::faderscale::unityValue));

    const auto sent = rig.transport->sentTo ("/live/track/set/volume");
    REQUIRE_FALSE (sent.empty());
    REQUIRE (sent.back()[1].getFloat32()
             == Approx ((float) conduit::touchlive::faderscale::unityValue));
}

//==============================================================================
TEST_CASE ("TouchLiveGridView: Tap feuert Clip, Scene und Stop über die richtigen Adressen", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto& grid = rig.page->gridView;
    grid.setBounds (0, 0, 720, 416);
    REQUIRE (grid.getColumnCount() == 2);   // nur reguläre Tracks
    REQUIRE (grid.getSceneCount() == 2);

    const auto colW = rig.settings.getChannelWidth();
    const auto top = conduit::TouchLiveGridView::headerHeight;
    const auto cellH = conduit::TouchLiveGridView::cellHeight;

    // Zelle (Spalte 1, Szene 1)
    grid.tapAt ({ colW + 10, top + cellH + 10 });
    {
        const auto sent = rig.transport->sentTo ("/live/clip_slot/fire");
        REQUIRE (sent.size() == 1);
        REQUIRE (sent.front()[0].getString() == "tr:1");
        REQUIRE (sent.front()[1].getInt32() == 1);
    }

    // Scene-Fire-Spalte (rechts), Szene 0
    grid.tapAt ({ grid.getWidth() - 10, top + 10 });
    {
        const auto sent = rig.transport->sentTo ("/live/scene/fire");
        REQUIRE (sent.size() == 1);
        REQUIRE (sent.front()[0].getInt32() == 0);
    }

    // Stop-Zeile (unten), Spalte 0
    grid.tapAt ({ 10, grid.getHeight() - 10 });
    {
        const auto sent = rig.transport->sentTo ("/live/track/stop_all_clips");
        REQUIRE (sent.size() == 1);
        REQUIRE (sent.front()[0].getString() == "tr:0");
    }
}

//==============================================================================
TEST_CASE ("TouchLiveMixerView: eingehender Mixer-Diff bewegt den bestehenden Strip", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto* drums = rig.page->mixerView.findStrip ("tr:0");
    REQUIRE (drums != nullptr);
    REQUIRE (drums->fader.getDisplayedValue() == Approx (0.85f));

    // Live→Conduit: Diff über den Listener-Pfad (kein Rebuild) — Fader folgt
    rig.model.applyDiff ("mixer",
        parse (R"({"tr:0":{"vol":0.3,"pan":0.5,"sends":[0.1,0.2],"mute":false,"solo":false,"arm":false}})"));

    REQUIRE (drums->fader.getDisplayedValue() == Approx (0.3f));   // headless snappt
    REQUIRE (drums->pan.getValue() == Approx (0.5));

    // Master analog (eigener Strip außerhalb des Viewports)
    rig.model.applyDiff ("mixer", parse (R"({"ma:0":{"vol":0.4,"pan":0.0}})"));
    REQUIRE (rig.page->mixerView.getMasterStrip()->fader.getDisplayedValue()
             == Approx (0.4f));
}

//==============================================================================
TEST_CASE ("TouchLiveMixerView: MeterBus-Frames landen in den Fader-Metern (M2)", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    rig.meterBus.update ("tr:0", 0.7f, 0.5f);
    rig.meterBus.noteFrame();
    rig.page->mixerView.refreshMetersNow();

    auto* drums = rig.page->mixerView.findStrip ("tr:0");
    REQUIRE (drums != nullptr);
    REQUIRE (drums->fader.getMeterBarLevel (0) == Approx (0.7f));
    REQUIRE (drums->fader.getMeterBarLevel (1) == Approx (0.5f));
    REQUIRE (drums->fader.getMeterPeakLevel (0) == Approx (0.7f));

    // Ballistik: ohne neuen Frame fällt der Balken weich, Peak hält länger
    rig.meterBus.update ("tr:0", 0.0f, 0.0f);
    rig.meterBus.noteFrame();
    rig.page->mixerView.refreshMetersNow();
    rig.page->mixerView.refreshMetersNow();

    const auto bar = drums->fader.getMeterBarLevel (0);
    REQUIRE (bar < 0.7f);
    REQUIRE (bar > 0.0f);
    REQUIRE (drums->fader.getMeterPeakLevel (0) > bar);

    // Unbekannte Keys (Master hat eigenen) bleiben still
    REQUIRE (rig.page->mixerView.getMasterStrip()->fader.getMeterBarLevel (0)
             == Approx (0.0f));
}

//==============================================================================
TEST_CASE ("TouchLiveDeviceView: Chips + Bank aus der devices-Domain (M3)", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto& device = rig.page->deviceView;

    REQUIRE (device.getTrackChipCount() == 4);            // 2 Tracks + Return + Master
    REQUIRE (device.getSelectedTrackKey() == "tr:0");     // erster Track mit Geräten
    REQUIRE (device.getDeviceChipCount() == 2);
    REQUIRE (device.getSelectedDeviceKey() == "dv:1");

    // EQ: 10 Parameter (ohne Device On) → 2 Bänke à 8
    REQUIRE (device.getBankCount() == 2);
    REQUIRE (device.getParameterName (0) == "P1");
    REQUIRE (device.getParameterSlider (0)->getValue() == Approx (0.1));

    device.setBank (1);
    REQUIRE (device.getParameterName (0) == "P9");
    REQUIRE (device.getParameterName (2).isEmpty());      // Bank 2 hat nur 2 Spalten

    // Gerätewechsel: quantisierter Parameter zeigt seine Werteliste
    device.selectDevice ("dv:2");
    REQUIRE (device.getBankCount() == 1);
    REQUIRE (device.getParameterName (0) == "Model");
    REQUIRE (device.getParameterValueText (0) == "RMS");  // Wert 1.0 → items[1]
}

TEST_CASE ("TouchLiveDeviceView: Slider sendet Touch-Command, parvals-Diff aktualisiert", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto& device = rig.page->deviceView;
    auto* slider = device.getParameterSlider (0);
    REQUIRE (slider != nullptr);

    slider->setValue (0.8, juce::sendNotificationSync);

    const auto sent = rig.transport->sentTo ("/live/device/set/parameter");
    REQUIRE_FALSE (sent.empty());
    REQUIRE (sent.back()[0].getString() == "dv:1");
    REQUIRE (sent.back()[1].getInt32() == 1);             // Bank 0, Spalte 0 → Param 1
    REQUIRE (sent.back()[2].getFloat32() == Approx (0.8f));

    // Eingehende parvals-Zeile bewegt den Slider (direkter Modell-Weg —
    // die Echo-Suppression sitzt im Client-Pfad und ist hier nicht beteiligt)
    rig.model.applyDiff ("devices",
        parse (R"({"parvals:dv:1":[1.0,0.25,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1.0]})"));
    REQUIRE (slider->getValue() == Approx (0.25));
}

TEST_CASE ("TouchLiveDeviceView: ON-Tile toggelt optimistisch + is_active-Command", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto& device = rig.page->deviceView;
    REQUIRE (device.onTile.isActive());

    device.onTile.onClick();

    REQUIRE_FALSE (device.onTile.isActive());

    const auto sent = rig.transport->sentTo ("/live/device/set/is_active");
    REQUIRE (sent.size() == 1);
    REQUIRE (sent.front()[0].getString() == "dv:1");
    REQUIRE (sent.front()[1].getInt32() == 0);
}

TEST_CASE ("TouchLiveDeviceView: Gain-Reduction aus dem Meter-Frame (dv:-Tripel)", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    auto& device = rig.page->deviceView;
    REQUIRE (device.getSelectedDeviceKey() == "dv:1");
    REQUIRE (device.getGainReductionLevel() == Approx (0.0f));

    // GR-Frame für das gewählte Device
    rig.meterBus.update ("dv:1", 0.4f, 0.4f);
    rig.meterBus.noteFrame();
    device.refreshGainReductionNow();
    REQUIRE (device.getGainReductionLevel() == Approx (0.4f));

    // Gerätewechsel setzt die GR-Anzeige zurück (fremder Key liefert 0)
    device.selectDevice ("dv:2");
    rig.meterBus.noteFrame();
    device.refreshGainReductionNow();
    REQUIRE (device.getGainReductionLevel() == Approx (0.0f));
}

TEST_CASE ("TouchLiveDeviceView: Kette entfernt → Auswahl fällt sauber zurück", "[touchlive][ui]")
{
    PageRig rig;
    rig.loadDemoSet();

    rig.model.applyDiff ("devices",
        parse (R"({"chain:tr:0":[],"dev:dv:1":null,"dev:dv:2":null,)"
               R"("parmeta:dv:1":null,"parvals:dv:1":null,)"
               R"("parmeta:dv:2":null,"parvals:dv:2":null})"));
    rig.page->deviceView.flushPendingRebuild();

    REQUIRE (rig.page->deviceView.getSelectedDeviceKey().isEmpty());
    REQUIRE (rig.page->deviceView.getDeviceChipCount() == 0);
    REQUIRE (rig.page->deviceView.getParameterSlider (0) != nullptr);
    REQUIRE_FALSE (rig.page->deviceView.getParameterSlider (0)->isVisible());
}

//==============================================================================
namespace
{

/** Browser-Antwort der Gegenseite: /remote/browser/list [seq,1,1,json]. */
[[nodiscard]] juce::OSCMessage browserList (int seq, const juce::String& json)
{
    juce::OSCMessage message { juce::OSCAddressPattern ("/remote/browser/list") };
    message.addInt32 (seq);
    message.addInt32 (1);
    message.addInt32 (1);
    message.addString (json);
    return message;
}

} // namespace

TEST_CASE ("TouchLiveBrowserView: Roots → Ordner → Item → Laden (M4)", "[touchlive][ui]")
{
    PageRig rig;
    auto& browser = rig.page->browserView;
    browser.setBounds (0, 0, 600, 400);

    // Sichtbar werden fordert die Wurzeln an
    rig.page->browserTabTile.onClick();
    REQUIRE (rig.transport->countSent ("/remote/browser/roots") == 1);
    REQUIRE (browser.isLoading());

    rig.deliver (browserList (1, R"({"p":0,"it":[[1,"Sounds",1,0],[2,"Drums",1,0]]})"));
    REQUIRE_FALSE (browser.isLoading());
    REQUIRE (browser.getRowCount() == 2);
    REQUIRE (browser.getRowName (1) == "Drums");
    REQUIRE (browser.getDepth() == 1);

    // Ordner antippen → children-Request; Antwort stapelt eine Ebene
    browser.tapRow (1);
    const auto sent = rig.transport->sentTo ("/remote/browser/children");
    REQUIRE (sent.size() == 1);
    REQUIRE (sent.front()[0].getInt32() == 2);

    rig.deliver (browserList (2, R"({"p":2,"it":[[7,"Kick.adg",0,1],[8,"Snare.adg",0,1]]})"));
    REQUIRE (browser.getDepth() == 2);
    REQUIRE (browser.getRowName (0) == "Kick.adg");

    // Ladbares Item: Tap wählt aus, Doppeltipp lädt
    browser.tapRow (0);
    REQUIRE (browser.getSelectedNodeId() == 7);
    REQUIRE (rig.transport->sentTo ("/live/browser/load").empty());

    browser.doubleTapRow (0);
    const auto loads = rig.transport->sentTo ("/live/browser/load");
    REQUIRE (loads.size() == 1);
    REQUIRE (loads.front()[0].getInt32() == 7);

    // Zurück nutzt den Ebenen-Cache (kein weiterer Request)
    browser.goBack();
    REQUIRE (browser.getDepth() == 1);
    REQUIRE (browser.getRowName (0) == "Sounds");
    REQUIRE (rig.transport->countSent ("/remote/browser/children") == 1);
}

TEST_CASE ("TouchLiveBrowserView: Preview-Modus spielt beim Antippen an", "[touchlive][ui]")
{
    PageRig rig;
    auto& browser = rig.page->browserView;
    browser.setBounds (0, 0, 600, 400);
    rig.page->browserTabTile.onClick();
    rig.deliver (browserList (1, R"({"p":0,"it":[[3,"Pad.adg",0,1]]})"));

    browser.previewTile.onClick();   // Modus an
    browser.tapRow (0);

    const auto previews = rig.transport->sentTo ("/live/browser/preview");
    REQUIRE (previews.size() == 1);
    REQUIRE (previews.front()[0].getInt32() == 3);

    browser.previewTile.onClick();   // Modus aus → stop_preview
    REQUIRE (rig.transport->countSent ("/live/browser/stop_preview") == 1);
}

TEST_CASE ("TouchLiveClient: gechunkte Browser-Liste wird zusammengesetzt", "[touchlive]")
{
    PageRig rig;
    auto& browser = rig.page->browserView;
    browser.setBounds (0, 0, 600, 400);
    rig.page->browserTabTile.onClick();

    // Zwei Chunks derselben seq, verkehrte Reihenfolge — Arrays mergen
    juce::OSCMessage chunk2 { juce::OSCAddressPattern ("/remote/browser/list") };
    chunk2.addInt32 (5); chunk2.addInt32 (2); chunk2.addInt32 (2);
    chunk2.addString (R"({"p":0,"it":[[2,"Drums",1,0]]})");
    rig.deliver (chunk2);
    REQUIRE (browser.isLoading());   // unvollständig — nichts angewendet

    juce::OSCMessage chunk1 { juce::OSCAddressPattern ("/remote/browser/list") };
    chunk1.addInt32 (5); chunk1.addInt32 (1); chunk1.addInt32 (2);
    chunk1.addString (R"({"p":0,"it":[[1,"Sounds",1,0]]})");
    rig.deliver (chunk1);

    REQUIRE (browser.getRowCount() == 2);
}

//==============================================================================
TEST_CASE ("TouchLivePage: Enable-Kachel und Kanalbreite schreiben in die Settings", "[touchlive][ui]")
{
    PageRig rig;

    REQUIRE (rig.settings.isEnabled());
    rig.page->enableTile.onClick();
    REQUIRE_FALSE (rig.settings.isEnabled());

    rig.page->widthTile.onCommitText ("120");
    REQUIRE (rig.settings.getChannelWidth() == 120);

    // Klemmen an den Grenzen
    rig.page->widthTile.onCommitText ("9999");
    REQUIRE (rig.settings.getChannelWidth() == conduit::TouchLiveSettings::maxChannelWidth);
}

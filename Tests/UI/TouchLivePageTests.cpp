#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TouchLive/LiveFaderScale.h"
#include "TouchLive/LiveSetModel.h"
#include "TouchLive/TouchLiveClient.h"
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
        client = std::make_unique<conduit::TouchLiveClient> (model, settings,
                                                             std::move (transportOwned));
        page = std::make_unique<conduit::TouchLivePage> (model, *client, settings);
        page->setBounds (0, 0, 720, 460);

        settings.setEnabled (true);          // Kommando-Pfad scharf
        settings.dispatchPendingMessages();
        transport->sent.clear();             // Subscribe-/Ping-Traffic weg
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

        page->mixerView.flushPendingRebuild();
        page->gridView.flushPendingRebuild();
        page->resized();
    }

    juce::ScopedJuceInitialiser_GUI juceRuntime;
    TempTouchLiveSettings temp;
    conduit::LiveSetModel model;
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

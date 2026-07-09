#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchLive/LiveSetModel.h"
#include "TouchLive/TouchLiveClient.h"
#include "TouchLive/TouchLiveSettings.h"
#include "TouchLiveFader.h"
#include "UI/PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Ein Kanalzug der TouchLive-Mixer-Ansicht (docs/TouchLive.md §5):
    Pan (Mini-Slider) · Sends (Anzahl dynamisch) · Volume-Fader (User-SVG)
    · Mute/Solo/Arm-LEDs (gelb/blau/rot, Mute-Off-Konvention wie Live) ·
    Track-Name auf Track-Farbe als Fußzeile.

    kind steuert die Bedienbreite: „audio"/„midi" = voller Zug (Commands
    über die Stable-ID), „return" = nur Vol/Pan (/live/return/set/*, Index),
    „master" = nur Vol/Pan (/live/master/set/*). Die Gegenseite (M1a)
    kann Mute/Solo/Sends für Returns noch nicht setzen — die Regler
    erscheinen erst, wenn das Script sie kann.

    Schreibweg (Feel-Regeln §5.1): lokal-optimistisch + noteTouchedParameter
    + sendTouchValue (thinned); Buttons optimistisch + sendCommand.
    Eingehende Werte kommen von der MixerView (updateValues) und slewen.
*/
class TouchLiveChannelStrip final : public juce::Component
{
public:
    TouchLiveChannelStrip (TouchLiveClient& clientToUse, juce::String keyToUse);

    /** Struktur (Name/Farbe/Kind/Live-Index + Send-Anzahl) neu übernehmen. */
    void updateStructure (const juce::ValueTree& tracksItem, const juce::ValueTree& mixerItem);

    /** Nur Werte (vol/pan/sends/mute/solo/arm); animate = Slew statt Snap. */
    void updateValues (const juce::ValueTree& mixerItem, bool animate);

    [[nodiscard]] const juce::String& getKey() const noexcept { return key; }
    [[nodiscard]] const juce::String& getKind() const noexcept { return kind; }

    void paint (juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
    // Controls public — UI-Tests treiben sie direkt (Muster ParameterPanel)
    TouchLiveFader fader;
    juce::Slider pan { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::vector<std::unique_ptr<juce::Slider>> sendSliders;
    push::TextTile activatorTile;   // gelb: leuchtet wenn NICHT gemutet (Live)
    push::TextTile soloTile;
    push::TextTile armTile;

private:
    //==========================================================================
    [[nodiscard]] bool isRegularTrack() const noexcept { return kind == "audio" || kind == "midi"; }

    void sendVolume (float value);
    void sendPan (float value);
    void sendSend (int sendIndex, float value);
    void toggleMute();
    void toggleSolo();
    void toggleArm();
    void rebuildSendSliders (int sendCount);
    void refreshButtonLeds();

    TouchLiveClient& client;
    const juce::String key;

    juce::String kind { "audio" };
    juce::String name;
    juce::Colour colour { 0xff5a5a5a };
    int liveIndex = 0;
    bool muted = false, soloed = false, armed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLiveChannelStrip)
};

//==============================================================================
/**
    Mixer-Sub-Tab der TouchLive-Page: ein Kanalzug pro Live-Track (Tracks
    nach Live-Reihenfolge, dann Returns), Master rechts angepinnt. Baut sich
    aus tracks- + mixer-Domain des LiveSetModel (Join über die Stable-ID).

    Struktur-Änderungen (Tracks kommen/gehen, Reorder) rebuilden coalesced
    über AsyncUpdater (Strips werden per Key wiederverwendet — kein
    Flackern); reine Wert-Diffs gehen direkt an den betroffenen Strip
    (Fader-Slew ~30 ms, Feel-Regel §5.1.3).

    Kanalbreite kommt aus TouchLiveSettings (User: „wie viele Tracks
    parallel"), Änderung wirkt sofort.
*/
class TouchLiveMixerView final : public juce::Component,
                                 private juce::ValueTree::Listener,
                                 private juce::ChangeListener,
                                 private juce::AsyncUpdater
{
public:
    TouchLiveMixerView (TouchLiveClient& clientToUse, LiveSetModel& modelToUse,
                        TouchLiveSettings& settingsToUse);
    ~TouchLiveMixerView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    //==========================================================================
    // Test-Seams
    /** Führt einen ausstehenden Struktur-Rebuild sofort aus. */
    void flushPendingRebuild();

    [[nodiscard]] TouchLiveChannelStrip* findStrip (const juce::String& key) const;
    [[nodiscard]] int getStripCount() const noexcept { return (int) strips.size(); }
    [[nodiscard]] TouchLiveChannelStrip* getMasterStrip() const noexcept { return masterStrip.get(); }

private:
    //==========================================================================
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void handleAsyncUpdate() override;

    void rebuildStrips();
    void layoutStrips();

    /** Domain-Name des Trees (Item → Parent), leer wenn außerhalb. */
    [[nodiscard]] static juce::String domainNameOf (const juce::ValueTree& tree);

    TouchLiveClient& client;
    LiveSetModel& model;
    TouchLiveSettings& settings;

    // Listener-Handle als Member halten (ValueTree-Listener hängen an der
    // Instanz — Lektion M1b, docs/TouchLive.md §10)
    juce::ValueTree modelState;

    juce::Viewport viewport;
    juce::Component stripRow;   // Inhalt des Viewports
    std::vector<std::unique_ptr<TouchLiveChannelStrip>> strips;
    std::unique_ptr<TouchLiveChannelStrip> masterStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLiveMixerView)
};

} // namespace conduit

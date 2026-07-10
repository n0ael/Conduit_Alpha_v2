#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchLive/LiveSetModel.h"
#include "TouchLive/LiveSpectrumTap.h"
#include "TouchLive/TouchLiveClient.h"
#include "TouchLive/TouchLiveSettings.h"
#include "TouchLiveBrowserView.h"
#include "TouchLiveDeviceView.h"
#include "TouchLiveGridView.h"
#include "TouchLiveMixerView.h"
#include "UI/PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    TouchLive-Page (docs/TouchLive.md §1/§5): eine Page, vier Sub-Tabs —
    GRID · MIXER · DEVICE · BROWSER. GRID/MIXER sind M1c, DEVICE/BROWSER
    gestylte Platzhalter (M3/M4). „GRID" ist das Clip-Grid der Session-View,
    nicht Conduits Grid-Touch-Controller Ω.

    Kopfleiste: Sub-Tabs links; rechts die Verbindungsleiste — Status-LED
    (grau disabled · orange connecting · grün connected · rot getrennt),
    LIVE-Enable, Host (editierbar), LEARN (IP-Learn-Broadcast-Probe) und
    die Kanalbreite (Drag/Direkteingabe — wie viele Tracks parallel).

    Reiner Message-Thread-Code; die Page hängt NUR an LiveSetModel/
    TouchLiveClient/TouchLiveSettings (der Audio-Thread ist NIE beteiligt).
*/
class TouchLivePage final : public juce::Component,
                            private juce::ChangeListener
{
public:
    /** spectrumTap darf nullptr sein (Tests) — dann gibt es kein Spektrum. */
    TouchLivePage (LiveSetModel& modelToUse, TouchLiveClient& clientToUse,
                   TouchLiveMeterBus& meterBusToUse, TouchLiveSettings& settingsToUse,
                   LiveSpectrumTap* spectrumTapToUse = nullptr);
    ~TouchLivePage() override;

    enum SubTab { tabGrid = 0, tabMixer = 1, tabDevice = 2, tabBrowser = 3 };

    void setSubTab (int newTab);
    [[nodiscard]] int getSubTab() const noexcept { return currentTab; }

    static constexpr int headerHeight = 44;

    void paint (juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
    // Controls public — UI-Tests treiben sie direkt (Muster ParameterPanel)
    push::TextTile gridTabTile    { "GRID" };
    push::TextTile mixerTabTile   { "MIXER" };
    push::TextTile deviceTabTile  { "DEVICE" };
    push::TextTile browserTabTile { "BROWSER" };

    push::TextTile enableTile { "LIVE", push::colours::ledGreen };
    push::TextTile learnTile  { "LEARN", push::colours::ledCyan };
    push::ValueTile hostTile  { "touchliveHost" };
    push::ValueTile widthTile { "touchliveWidth" };

    TouchLiveGridView gridView;
    TouchLiveMixerView mixerView;
    TouchLiveDeviceView deviceView;     // M3 — generische Device-Steuerung
    TouchLiveBrowserView browserView;   // M4 — Lives Browser (load_children)

private:
    //==========================================================================
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void refreshHeader();

    [[nodiscard]] juce::Colour statusColour() const;

    TouchLiveClient& client;
    TouchLiveSettings& settings;

    juce::Rectangle<int> statusLedArea;
    int currentTab = tabGrid;
    int widthAtDragStart = TouchLiveSettings::defaultChannelWidth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLivePage)
};

} // namespace conduit

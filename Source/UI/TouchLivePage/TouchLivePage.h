#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchLive/LiveSetModel.h"
#include "TouchLive/TouchLiveClient.h"
#include "TouchLive/TouchLiveSettings.h"
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
    TouchLivePage (LiveSetModel& modelToUse, TouchLiveClient& clientToUse,
                   TouchLiveSettings& settingsToUse);
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

private:
    //==========================================================================
    /** Gestylter Platzhalter (Muster PageHost) für DEVICE/BROWSER. */
    class PlaceholderView final : public juce::Component
    {
    public:
        PlaceholderView (push::Icon iconToUse, juce::String titleToUse, juce::String hintToUse);
        void paint (juce::Graphics& g) override;

    private:
        push::Icon icon;
        juce::String title, hint;
    };

    //==========================================================================
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void refreshHeader();

    [[nodiscard]] juce::Colour statusColour() const;

    TouchLiveClient& client;
    TouchLiveSettings& settings;

    PlaceholderView devicePlaceholder;
    PlaceholderView browserPlaceholder;

    juce::Rectangle<int> statusLedArea;
    int currentTab = tabGrid;
    int widthAtDragStart = TouchLiveSettings::defaultChannelWidth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLivePage)
};

} // namespace conduit

#include "TouchLivePage.h"

namespace conduit
{

namespace
{
    constexpr int tabWidth      = 88;
    constexpr int ledDiameter   = 12;
    constexpr int enableWidth   = 56;
    constexpr int hostWidth     = 132;
    constexpr int learnWidth    = 64;
    constexpr int widthTileSize = 76;
}

//==============================================================================
TouchLivePage::PlaceholderView::PlaceholderView (push::Icon iconToUse, juce::String titleToUse,
                                                 juce::String hintToUse)
    : icon (iconToUse), title (std::move (titleToUse)), hint (std::move (hintToUse))
{
}

void TouchLivePage::PlaceholderView::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    const auto area = getLocalBounds().toFloat();
    const auto centre = area.getCentre();

    push::draw (g, icon,
                juce::Rectangle<float> (72.0f, 72.0f).withCentre (centre.translated (0.0f, -40.0f)),
                push::colours::textDim);

    g.setColour (push::colours::text);
    g.setFont (push::scaledFont (26.0f));
    g.drawText (title, area.withTop (centre.y + 16.0f).withHeight (34.0f),
                juce::Justification::centredTop);

    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (15.0f));
    g.drawText (hint, area.withTop (centre.y + 52.0f).withHeight (24.0f),
                juce::Justification::centredTop);
}

//==============================================================================
TouchLivePage::TouchLivePage (LiveSetModel& modelToUse, TouchLiveClient& clientToUse,
                              TouchLiveMeterBus& meterBusToUse, TouchLiveSettings& settingsToUse)
    : gridView (clientToUse, modelToUse, settingsToUse),
      mixerView (clientToUse, modelToUse, meterBusToUse, settingsToUse),
      deviceView (clientToUse, modelToUse),
      client (clientToUse),
      settings (settingsToUse),
      browserPlaceholder (push::Icon::browserPanel, "Browser",
                          juce::String::fromUTF8 ("kommt als eigener Meilenstein (M4)"))
{
    // Sub-Tabs (Muster PageHost/TransportBar)
    const auto wireTab = [this] (push::TextTile& tile, int tab)
    {
        tile.onClick = [this, tab] { setSubTab (tab); };
        addAndMakeVisible (tile);
    };

    wireTab (gridTabTile, tabGrid);
    wireTab (mixerTabTile, tabMixer);
    wireTab (deviceTabTile, tabDevice);
    wireTab (browserTabTile, tabBrowser);

    // Verbindungsleiste
    enableTile.setTooltip (juce::String::fromUTF8 ("Verbindung zum ConduitRemote-Script an/aus"));
    enableTile.onClick = [this] { settings.setEnabled (! settings.isEnabled()); };
    addAndMakeVisible (enableTile);

    hostTile.setCaption ("HOST");
    hostTile.setTooltip (juce::String::fromUTF8 ("IP des Ableton-Rechners — Doppelklick zum Eingeben"));
    hostTile.onCommitText = [this] (const juce::String& entered)
    { settings.setHost (entered); refreshHeader(); };
    addAndMakeVisible (hostTile);

    learnTile.setTooltip (juce::String::fromUTF8 (
        "IP-Learn: broadcastet Pings — die Antwort des Scripts verrät die Live-IP"));
    learnTile.onClick = [this]
    {
        if (client.isLearning())
        {
            client.cancelIpLearn();
            learnTile.setActive (false);
            return;
        }

        const auto started = client.beginIpLearn ([this] (const juce::String& senderIp)
        {
            learnTile.setActive (false);

            if (senderIp.isNotEmpty())
                settings.setHost (senderIp);
        });

        learnTile.setActive (started);
    };
    addAndMakeVisible (learnTile);

    widthTile.setCaption ("KANAL");
    widthTile.setTooltip (juce::String::fromUTF8 (
        "Kanalzug-/Spaltenbreite — Drag oder Doppelklick (mehr/weniger Tracks parallel)"));
    widthTile.onDragStart = [this] { widthAtDragStart = settings.getChannelWidth(); };
    widthTile.onDrag = [this] (float totalDeltaY)
    {
        // Drag nach oben = breiter (weniger Tracks parallel)
        settings.setChannelWidth (widthAtDragStart - (int) (totalDeltaY * 0.5f));
        refreshHeader();
    };
    widthTile.onCommitText = [this] (const juce::String& entered)
    {
        settings.setChannelWidth (entered.getIntValue());
        refreshHeader();
    };
    addAndMakeVisible (widthTile);

    addChildComponent (gridView);
    addChildComponent (mixerView);
    addChildComponent (deviceView);
    addChildComponent (browserPlaceholder);

    client.addChangeListener (this);     // Status-LED
    settings.addChangeListener (this);   // Host/Enable/Breite

    setSubTab (tabGrid);
    refreshHeader();
}

TouchLivePage::~TouchLivePage()
{
    settings.removeChangeListener (this);
    client.removeChangeListener (this);
}

//==============================================================================
void TouchLivePage::setSubTab (int newTab)
{
    currentTab = juce::jlimit ((int) tabGrid, (int) tabBrowser, newTab);

    gridView.setVisible (currentTab == tabGrid);
    mixerView.setVisible (currentTab == tabMixer);
    deviceView.setVisible (currentTab == tabDevice);
    browserPlaceholder.setVisible (currentTab == tabBrowser);

    gridTabTile.setActive (currentTab == tabGrid);
    mixerTabTile.setActive (currentTab == tabMixer);
    deviceTabTile.setActive (currentTab == tabDevice);
    browserTabTile.setActive (currentTab == tabBrowser);
}

//==============================================================================
void TouchLivePage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshHeader();
}

void TouchLivePage::refreshHeader()
{
    enableTile.setActive (settings.isEnabled());
    hostTile.setText (settings.getHost());
    widthTile.setText (juce::String (settings.getChannelWidth()) + " px");
    repaint (statusLedArea.expanded (2));
}

juce::Colour TouchLivePage::statusColour() const
{
    switch (client.getStatus())
    {
        case TouchLiveClient::Status::disabled:     return push::colours::textDim.withAlpha (0.4f);
        case TouchLiveClient::Status::connecting:   return push::colours::ledOrange;
        case TouchLiveClient::Status::connected:    return push::colours::ledGreen;
        case TouchLiveClient::Status::disconnected: return push::colours::ledRed;
    }

    return push::colours::textDim;
}

//==============================================================================
void TouchLivePage::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (4, 4);

    gridTabTile.setBounds (header.removeFromLeft (tabWidth).reduced (2, 0));
    mixerTabTile.setBounds (header.removeFromLeft (tabWidth).reduced (2, 0));
    deviceTabTile.setBounds (header.removeFromLeft (tabWidth).reduced (2, 0));
    browserTabTile.setBounds (header.removeFromLeft (tabWidth).reduced (2, 0));

    widthTile.setBounds (header.removeFromRight (widthTileSize).reduced (2, 0));
    learnTile.setBounds (header.removeFromRight (learnWidth).reduced (2, 0));
    hostTile.setBounds (header.removeFromRight (hostWidth).reduced (2, 0));
    enableTile.setBounds (header.removeFromRight (enableWidth).reduced (2, 0));

    statusLedArea = header.removeFromRight (ledDiameter + 8)
                          .withSizeKeepingCentre (ledDiameter, ledDiameter);

    gridView.setBounds (area);
    mixerView.setBounds (area);
    deviceView.setBounds (area);
    browserPlaceholder.setBounds (area);
}

void TouchLivePage::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    g.setColour (push::colours::panel);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));

    // Status-LED der Verbindung
    g.setColour (statusColour());
    g.fillEllipse (statusLedArea.toFloat());
}

} // namespace conduit

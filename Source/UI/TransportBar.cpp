#include "TransportBar.h"

#include "Core/LinkClock.h"
#include "Modules/ConduitModule.h"
#include "Util/ScaleQuantizer.h"

namespace conduit
{

namespace
{
constexpr double minTempo = 20.0;
constexpr double maxTempo = 300.0;
constexpr double tempoPerPixel = 0.25;  // Vertikal-Drag: 4 px pro BPM

juce::String formatTempo (double bpm)
{
    return juce::String (bpm, 2);
}
} // namespace

//==============================================================================
TransportBar::TransportBar (juce::ValueTree rootTree, LinkClock& linkClockToUse)
    : rootState (std::move (rootTree)), linkClock (linkClockToUse)
{
    // -- Transport links ------------------------------------------------------
    // Play/Tape/Metronom bekommen ihre Funktion in Schritt 3/5 — sichtbar,
    // aber disabled, damit das Layout von Anfang an komplett steht
    playTile.setEnabled (false);
    playTile.setTooltip (juce::String::fromUTF8 ("Link Start/Stop — folgt in Schritt 3"));
    tapeTile.setEnabled (false);
    tapeTile.setTooltip (juce::String::fromUTF8 ("Looper-Page — eigener Meilenstein"));
    metronomeTile.setEnabled (false);
    metronomeTile.setTooltip (juce::String::fromUTF8 ("Metronom — folgt in Schritt 5"));

    captureTile.setTooltip ("Capture: alle Aufnahmen exportieren\nShift-Klick: Kanal-Panel");
    captureTile.onClick = [this]
    {
        if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
        {
            if (onToggleCapturePanel != nullptr)
                onToggleCapturePanel();
        }
        else if (onCaptureAll != nullptr)
        {
            onCaptureAll();
        }
    };

    tapTile.setEnabled (false);
    tapTile.setTooltip (juce::String::fromUTF8 ("Tap-and-Commit-Tempo — folgt in Schritt 4"));
    nudgeDownTile.setEnabled (false);
    nudgeUpTile.setEnabled (false);
    nudgeDownTile.setTooltip (juce::String::fromUTF8 ("Tempo-Nudge — folgt in Schritt 4"));
    nudgeUpTile.setTooltip (juce::String::fromUTF8 ("Tempo-Nudge — folgt in Schritt 4"));

    // -- Tempo/Position/Swing/Link -------------------------------------------
    tempoTile.setCaption ("BPM");
    tempoTile.setText (formatTempo (linkClock.getTempo()));
    tempoTile.onDragStart = [this] { tempoAtDragStart = linkClock.getTempo(); };
    tempoTile.onDrag = [this] (float totalDeltaY)
    {
        linkClock.setTempo (juce::jlimit (minTempo, maxTempo,
                                          tempoAtDragStart + (double) totalDeltaY * tempoPerPixel));
        tempoTile.setText (formatTempo (linkClock.getTempo()));
    };
    tempoTile.onCommitText = [this] (const juce::String& entered) { applyTempoText (entered); };

    positionTile.setCaption ("POS");
    positionTile.setText ("1. 1. 1");
    positionTile.setEnabled (false);  // Quelle folgt in Schritt 4

    swingTile.setCaption ("SWING");
    swingTile.setText ("0 %");
    swingTile.setEnabled (false);     // globaler Swing folgt in Schritt 4

    linkTile.setEnabled (false);      // Dropdown-Menü folgt in Schritt 3
    linkTile.setTooltip (juce::String::fromUTF8 ("Link-Menü (Start/Stop-Sync, Clock-Offset) — folgt in Schritt 3"));

    // -- Pages (Reihenfolge wie auf dem Push-Controller) ----------------------
    const push::Icon pageIcons[] = { push::Icon::pageGrid, push::Icon::pageMixer,
                                     push::Icon::pageClip, push::Icon::pageDevice };
    const char* pageNames[] = { "Grid", "Mixer", "Clip", "Device" };

    for (int page = 0; page < 4; ++page)
    {
        auto tile = std::make_unique<push::IconTile> (pageIcons[page], pageNames[page],
                                                      push::colours::ledWhite);
        tile->setTooltip (juce::String (pageNames[page]) + "-Page");
        tile->onClick = [this, page]
        {
            setSelectedPage (page);

            if (onPageSelected != nullptr)
                onPageSelected (page);
        };

        addAndMakeVisible (*tile);
        pageTiles.push_back (std::move (tile));
    }

    setSelectedPage (pageDevice);

    // -- Aktionen rechts ------------------------------------------------------
    plusTile.setTooltip (juce::String::fromUTF8 ("Browser: Module hinzufügen, Presets"));
    plusTile.onClick = [this] { openBrowser(); };

    undoTile.setTooltip (juce::String::fromUTF8 ("Undo — Shift-Klick: Redo"));
    undoTile.onClick = [this]
    {
        const auto redo = juce::ModifierKeys::getCurrentModifiers().isShiftDown();

        if (redo && onRedo != nullptr)
            onRedo();
        else if (! redo && onUndo != nullptr)
            onUndo();
    };

    saveTile.onClick = [this] { if (onSave != nullptr) onSave(); };
    gearTile.setTooltip ("Einstellungen");
    gearTile.onClick = [this] { if (onSettings != nullptr) onSettings(); };

    // -- Globale Session-Skala (Schema 6.2) — Code aus der alten Toolbar ------
    {
        const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                    "F#", "G", "G#", "A", "A#", "B" };

        for (int note = 0; note < 12; ++note)
            rootCombo.addItem (noteNames[note], note + 1);

        for (const auto type : { ScaleType::chromatic, ScaleType::major,
                                 ScaleType::minor, ScaleType::pentatonic })
            scaleCombo.addItem (toString (type), static_cast<int> (type) + 1);

        rootCombo.setSelectedId (juce::jlimit (0, 11,
            (int) rootState.getProperty (id::scaleRoot, 0)) + 1, juce::dontSendNotification);
        scaleCombo.setSelectedId (static_cast<int> (scaleTypeFromString (
            rootState.getProperty (id::scaleType).toString())) + 1, juce::dontSendNotification);

        rootCombo.onChange = [this]
        { rootState.setProperty (id::scaleRoot, rootCombo.getSelectedId() - 1, nullptr); };
        scaleCombo.onChange = [this]
        {
            rootState.setProperty (id::scaleType,
                toString (static_cast<ScaleType> (scaleCombo.getSelectedId() - 1)), nullptr);
        };
    }

    warningLabel.setColour (juce::Label::textColourId, push::colours::ledOrange);
    warningLabel.setJustificationType (juce::Justification::centredRight);

    for (auto* component : std::initializer_list<juce::Component*> {
             &playTile, &tapeTile, &captureTile, &tapTile, &nudgeDownTile, &nudgeUpTile,
             &metronomeTile, &tempoTile, &positionTile, &swingTile, &linkTile,
             &plusTile, &undoTile, &saveTile, &gearTile, &rootCombo, &scaleCombo,
             &warningLabel })
        addAndMakeVisible (component);
}

//==============================================================================
void TransportBar::setBrowserItems (std::vector<ModuleBrowser::Item> items)
{
    browserItems = std::move (items);
}

void TransportBar::openBrowser()
{
    if (browserItems.empty())
        return;

    auto browser = std::make_unique<ModuleBrowser> (browserItems);
    juce::CallOutBox::launchAsynchronously (std::move (browser),
                                            plusTile.getScreenBounds(), nullptr);
}

//==============================================================================
void TransportBar::refresh()
{
    // Kein Kampf mit dem User: während eines Tempo-Drags gewinnt die Kachel
    if (! tempoTile.isMouseButtonDown (true))
        tempoTile.setText (formatTempo (linkClock.getTempo()));

    const auto numPeers = linkClock.getNumPeers();
    linkTile.setText (numPeers == 0 ? juce::String ("Link")
                                    : "Link " + juce::String ((int) numPeers));
    linkTile.setActive (numPeers > 0);
}

void TransportBar::setCaptureStatus (bool recording, bool held, bool exporting)
{
    captureTile.setActive (recording || held || exporting);
    captureTile.setAccentColour (recording  ? push::colours::ledRed
                                : exporting ? push::colours::ledCyan
                                            : push::colours::ledOrange);
}

void TransportBar::setWarningText (const juce::String& warning)
{
    if (warningLabel.getText() == warning)
        return;

    warningLabel.setText (warning, juce::dontSendNotification);
    resized();
}

void TransportBar::setPositionText (const juce::String& text)
{
    positionTile.setText (text);
}

void TransportBar::setSelectedPage (int pageIndex)
{
    selectedPage = juce::jlimit (0, 3, pageIndex);

    for (int page = 0; page < (int) pageTiles.size(); ++page)
        pageTiles[(size_t) page]->setActive (page == selectedPage);
}

//==============================================================================
void TransportBar::applyTempoText (const juce::String& entered)
{
    const auto value = entered.retainCharacters ("0123456789.,")
                              .replaceCharacter (',', '.')
                              .getDoubleValue();

    if (value > 0.0)
        linkClock.setTempo (juce::jlimit (minTempo, maxTempo, value));

    tempoTile.setText (formatTempo (linkClock.getTempo()));
}

//==============================================================================
void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);
}

void TransportBar::resized()
{
    auto bounds = getLocalBounds().reduced (8, 6);  // Kacheln ≥ 44 px hoch
    const auto tile = bounds.getHeight();

    const auto placeLeft = [&bounds] (juce::Component& component, int width, int gapAfter = 6)
    {
        component.setBounds (bounds.removeFromLeft (width));
        bounds.removeFromLeft (gapAfter);
    };

    const auto placeRight = [&bounds] (juce::Component& component, int width, int gapBefore = 6)
    {
        component.setBounds (bounds.removeFromRight (width));
        bounds.removeFromRight (gapBefore);
    };

    placeLeft (playTile,    tile);
    placeLeft (tapeTile,    tile);
    placeLeft (captureTile, tile, 14);

    placeLeft (tapTile,       52);
    placeLeft (nudgeDownTile, 30);
    placeLeft (nudgeUpTile,   30);
    placeLeft (metronomeTile, tile, 14);

    placeLeft (tempoTile,    86);
    placeLeft (positionTile, 78);
    placeLeft (swingTile,    62);
    placeLeft (linkTile,     78, 14);

    // Rechts von außen nach innen: Skala, Aktionen, Pages
    placeRight (scaleCombo, 104);
    placeRight (rootCombo,   56, 14);

    placeRight (gearTile, tile);
    placeRight (saveTile,  56);
    placeRight (undoTile,  56);
    placeRight (plusTile, tile, 14);

    for (auto page = (int) pageTiles.size(); --page >= 0;)
        placeRight (*pageTiles[(size_t) page], tile, page == 0 ? 14 : 6);

    // Rest der Mitte: Audio-Setup-Warnung (9.1), rechtsbündig
    warningLabel.setBounds (bounds);
}

} // namespace conduit

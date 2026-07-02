#include "TransportBar.h"

#include "Core/LinkClock.h"
#include "LinkMenuPanel.h"
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
TransportBar::TransportBar (juce::ValueTree rootTree, LinkClock& linkClockToUse,
                            TransportSettings& transportSettingsToUse)
    : rootState (std::move (rootTree)), linkClock (linkClockToUse),
      transportSettings (transportSettingsToUse)
{
    // -- Transport links ------------------------------------------------------
    // Play = Link-Transport: mit Start/Stop-Sync starten/stoppen alle Peers
    // (inkl. Ableton); die LED folgt dem Session-Zustand über refresh()
    playTile.setTooltip ("Link Start/Stop");
    playTile.onClick = [this] { linkClock.requestIsPlaying (! linkClock.isPlaying()); };

    tapeTile.setEnabled (false);
    tapeTile.setTooltip (juce::String::fromUTF8 ("Looper-Page — eigener Meilenstein"));

    // Metronom: Link-synchroner Click auf die Anker-Kanäle (Ziel im Link-Menü)
    metronomeTile.setTooltip (juce::String::fromUTF8 ("Metronom — Ziel-Kanäle im Link-Menü"));
    metronomeTile.onClick = [this]
    { transportSettings.setMetronomeEnabled (! transportSettings.isMetronomeEnabled()); };

    // Vorbereitete Looper-Toggles (Endless-Meilenstein) — Zustand persistiert
    // bereits in den TransportSettings, die LED folgt über refresh()
    fixedLengthTile.setTooltip (juce::String::fromUTF8 ("Fixed Length — Looper-Länge (Endless-Meilenstein)"));
    fixedLengthTile.onClick = [this]
    { transportSettings.setFixedLengthEnabled (! transportSettings.isFixedLengthEnabled()); };

    automateTile.setTooltip (juce::String::fromUTF8 ("Automate — Looper-Automation (Endless-Meilenstein)"));
    automateTile.onClick = [this]
    { transportSettings.setAutomateEnabled (! transportSettings.isAutomateEnabled()); };

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

    // Tap-and-Commit (User-Entwurf): n Taps erfassen, der (n+1)-te committet
    // zur Link-Session; n steht im Link-Menü. Preview cyan in der Tempo-Kachel.
    tapTile.setTooltip (juce::String::fromUTF8 ("Tap-Tempo: n Taps erfassen, der nächste committet (n im Link-Menü)"));
    tapTile.onClick = [this]
    { tapWithTime (juce::Time::getMillisecondCounterHiRes() * 0.001); };

    // Nudge (DJ-Angleichen): solange gehalten läuft die Session ±2 % —
    // Loslassen stellt das Tempo wieder her, der Phasen-Versatz bleibt
    nudgeDownTile.setTooltip (juce::String::fromUTF8 ("Nudge: Tempo −2 % solange gehalten"));
    nudgeUpTile.setTooltip (juce::String::fromUTF8 ("Nudge: Tempo +2 % solange gehalten"));
    nudgeDownTile.onStateChange = [this] { handleNudge (nudgeDownTile, nudgeDownWasDown, 0.98); };
    nudgeUpTile.onStateChange   = [this] { handleNudge (nudgeUpTile,   nudgeUpWasDown,   1.02); };

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
    positionTile.setText ("1. 1. 1");  // Anzeige folgt über refresh()

    // Globaler Session-Swing (4.5): Root-Property, ohne Undo (Session-Setting
    // wie die Skala) — Sequencer mit lokalem Swing 0 folgen dem Wert
    swingTile.setCaption ("SWING");
    swingTile.setTooltip (juce::String::fromUTF8 ("Globaler Swing — lokaler Sequencer-Swing > 0 überschreibt"));
    swingTile.onDragStart = [this] { swingAtDragStart = getGlobalSwing(); };
    swingTile.onDrag = [this] (float totalDeltaY)
    { setGlobalSwing (swingAtDragStart + (double) totalDeltaY * 0.0025); };
    swingTile.onCommitText = [this] (const juce::String& entered) { applySwingText (entered); };

    linkTile.setTooltip (juce::String::fromUTF8 ("Link-Menü: Start/Stop-Sync, Clock-Offset"));
    linkTile.onClick = [this] { openLinkMenu(); };

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
             &playTile, &tapeTile, &captureTile, &fixedLengthTile, &automateTile,
             &tapTile, &nudgeDownTile, &nudgeUpTile,
             &metronomeTile, &tempoTile, &positionTile, &swingTile, &linkTile,
             &plusTile, &undoTile, &saveTile, &gearTile, &rootCombo, &scaleCombo,
             &warningLabel })
        addAndMakeVisible (component);

    refresh();  // LED-Zustände sofort (Play/Looper-Toggles aus Settings)
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

void TransportBar::openLinkMenu()
{
    auto panel = std::make_unique<LinkMenuPanel> (
        transportSettings, linkClock,
        metronomeTargetNames != nullptr ? metronomeTargetNames() : juce::StringArray());

    juce::CallOutBox::launchAsynchronously (std::move (panel),
                                            linkTile.getScreenBounds(), nullptr);
}

//==============================================================================
void TransportBar::refresh()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // Tap-Preview verlischt nach dem Timeout — danach folgt die Kachel
    // wieder der Session
    if (tapPreviewShowing && ! tapTempo.isActive (now))
    {
        tapPreviewShowing = false;
        tempoTile.setAccentColour (push::colours::text);
        tapTempo.reset();
    }

    // Kein Kampf mit dem User: während Tempo-Drag oder Tap-Preview gewinnt
    // die Kachel
    if (! tempoTile.isMouseButtonDown (true) && ! tapPreviewShowing)
        tempoTile.setText (formatTempo (linkClock.getTempo()));

    positionTile.setText (formatPosition (linkClock.getBeatPosition()));
    swingTile.setText (juce::String (juce::roundToInt (getGlobalSwing() * 100.0)) + " %");

    const auto numPeers = linkClock.getNumPeers();
    linkTile.setText (numPeers == 0 ? juce::String ("Link")
                                    : "Link " + juce::String ((int) numPeers));
    linkTile.setActive (numPeers > 0);

    // Play-LED folgt der Session (auch wenn Ableton startet/stoppt);
    // Looper-/Metronom-Toggles folgen den Settings (Repaint nur bei Änderung)
    playTile.setActive (linkClock.isPlaying());
    fixedLengthTile.setActive (transportSettings.isFixedLengthEnabled());
    automateTile.setActive (transportSettings.isAutomateEnabled());
    metronomeTile.setActive (transportSettings.isMetronomeEnabled());
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

juce::String TransportBar::formatPosition (double beatPosition)
{
    // Quantum 4 (4/4, wie die LinkClock): Takt. Beat. Sechzehntel — 1-basiert
    const auto beat      = juce::jmax (0.0, beatPosition);
    const auto bar       = (int) (beat / 4.0);
    const auto beatInBar = (int) (beat - bar * 4.0);
    const auto sixteenth = (int) ((beat - bar * 4.0 - beatInBar) * 4.0);

    return juce::String (bar + 1) + ". " + juce::String (beatInBar + 1)
           + ". " + juce::String (sixteenth + 1);
}

void TransportBar::tapWithTime (double timeSeconds)
{
    tapTempo.setRequiredTaps (transportSettings.getTapCount());
    const auto result = tapTempo.tap (timeSeconds);

    if (! result.hasPreview)
        return;

    const auto previewBpm = juce::jlimit (minTempo, maxTempo, result.previewBpm);

    if (result.committed)
    {
        // Commit-Tap: Tempo in die Link-Session, Kachel zurück in Normalfarbe
        linkClock.setTempo (previewBpm);
        tempoTile.setAccentColour (push::colours::text);
        tempoTile.setText (formatTempo (linkClock.getTempo()));
        tapPreviewShowing = false;
    }
    else
    {
        // Erfassungs-Phase: Preview cyan, Session bleibt unberührt
        tempoTile.setAccentColour (push::colours::ledCyan);
        tempoTile.setText (formatTempo (previewBpm));
        tapPreviewShowing = true;
    }
}

void TransportBar::handleNudge (push::IconTile& tile, bool& wasDown, double factor)
{
    const auto down = tile.isDown();

    if (down == wasDown)
        return;  // onStateChange feuert auch für Hover — nur Down-Flanken zählen

    wasDown = down;

    if (down)
    {
        tempoBeforeNudge = linkClock.getTempo();
        linkClock.setTempo (juce::jlimit (minTempo, maxTempo, tempoBeforeNudge * factor));
        tile.setActive (true);
    }
    else
    {
        // Loslassen: Rate zurück — der aufgelaufene Phasen-Versatz bleibt
        // (genau das Angleichen wie am Turntable)
        linkClock.setTempo (tempoBeforeNudge);
        tile.setActive (false);
    }
}

void TransportBar::setGlobalSwing (double swing)
{
    rootState.setProperty (id::globalSwing, juce::jlimit (0.0, 0.75, swing), nullptr);
    swingTile.setText (juce::String (juce::roundToInt (getGlobalSwing() * 100.0)) + " %");
}

double TransportBar::getGlobalSwing() const
{
    return juce::jlimit (0.0, 0.75, (double) rootState.getProperty (id::globalSwing, 0.0));
}

void TransportBar::applySwingText (const juce::String& entered)
{
    const auto percent = entered.retainCharacters ("0123456789.,")
                                .replaceCharacter (',', '.')
                                .getDoubleValue();

    setGlobalSwing (percent / 100.0);
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

    placeLeft (fixedLengthTile, 92);
    placeLeft (automateTile,    76, 14);

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

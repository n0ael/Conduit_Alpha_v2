#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/LooperSettings.h"
#include "UI/LooperPage.h"
#include "UI/LooperDockTabs.h"
#include "UI/TransportBar.h"

using Catch::Approx;

//==============================================================================
TEST_CASE ("LooperPage (M6): Panel-Struktur folgt setLooperCount", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperPage page;
    page.setBounds (0, 0, 1400, 700);

    int rebuilds = 0;
    page.onPanelsChanged = [&] { ++rebuilds; };

    REQUIRE (page.getLooperCount() == 1);

    page.setLooperCount (3);
    REQUIRE (page.getLooperCount() == 3);
    REQUIRE (rebuilds == 1);

    // Clamps 1..4; No-op feuert keinen Rebuild
    page.setLooperCount (9);
    REQUIRE (page.getLooperCount() == 4);
    page.setLooperCount (4);
    REQUIRE (rebuilds == 2);

    page.setLooperCount (0);
    REQUIRE (page.getLooperCount() == 1);
}

TEST_CASE ("LooperPanel: Quellen-Auswahl und Track-Struktur", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperPanel panel { 1 };
    panel.setBounds (0, 0, 460, 620);

    const std::vector<conduit::LooperPanel::Source> sources = {
        { "master", "Master" },
        { "hw:0",   "In 1 / In 2" },
    };

    juce::String selectedKey;
    panel.onSourceSelected = [&] (const juce::String& key) { selectedKey = key; };

    panel.setSources (sources, "hw:0");
    REQUIRE (panel.getSourceCombo().getSelectedItemIndex() == 1);
    REQUIRE (selectedKey.isEmpty());   // Anzeige ≠ User-Klick

    panel.getSourceCombo().setSelectedItemIndex (0, juce::sendNotificationSync);
    REQUIRE (selectedKey == "master");

    // Keine Phantom-Vorauswahl (ADR 010): unbekannter/Legacy-Schlüssel
    // → Combo bleibt LEER statt fälschlich den ersten Eintrag zu zeigen
    panel.setSources (sources, "out:1");
    REQUIRE (panel.getSourceCombo().getSelectedId() == 0);

    // FFT/WAVE-Kachel pro Looper (ersetzt MST — der lebt global im
    // MIXER-Tab): Zustand + Hook
    bool spectrum = false;
    panel.onViewToggled = [&] (bool wantsSpectrum) { spectrum = wantsSpectrum; };
    panel.setSpectrumView (true);
    REQUIRE (panel.getViewTile().isActive());
    panel.getViewTile().onClick();
    REQUIRE_FALSE (spectrum);   // aktiv → Toggle meldet false

    // Tracks 1..4 (die frühere „+"-Kachel lebt im Seitenpanel-LAYOUT)
    REQUIRE (panel.getTrackCount() == 1);
    panel.setTrackCount (4);
    REQUIRE (panel.getTrackCount() == 4);
    panel.setTrackCount (2);
    REQUIRE (panel.getTrackCount() == 2);
}

TEST_CASE ("LooperPanel: Quellen-Menü mit Link-Gruppen — Separatoren, Farben, stabile IDs", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperPanel panel { 1 };
    panel.setBounds (0, 0, 460, 620);

    const auto orange = juce::Colour (0xffffa726);
    const auto green  = juce::Colour (0xff66bb6a);
    const auto cyan   = juce::Colour (0xff4dd0e1);

    // Lokale Quellen | Link-Peer "Live" | Link-Peer "M4L" (je Separator davor)
    const std::vector<conduit::LooperPanel::Source> sources = {
        { "master",     "Master",       juce::Colour(), false },
        { "hw:0",       "In 1 / In 2",  orange,         false },
        { "tap:live_a", "Live / Drums", green,          true  },
        { "tap:live_b", "Live / Bass",  green,          false },
        { "tap:m4l",    "M4L / Synth",  cyan,           true  },
    };

    juce::String selectedKey;
    panel.onSourceSelected = [&] (const juce::String& key) { selectedKey = key; };

    panel.setSources (sources, "tap:live_a");
    auto& combo = panel.getSourceCombo();

    SECTION ("Separatoren strukturieren das Menü, ohne Items zu verschieben")
    {
        REQUIRE (combo.getNumItems() == 5);   // Separatoren zählen nicht

        int separators = 0;
        for (juce::PopupMenu::MenuItemIterator iterator (*combo.getRootMenu()); iterator.next();)
            if (iterator.getItem().isSeparator)
                ++separators;
        REQUIRE (separators == 2);

        // Auswahl über den Key gefunden — Item-ID = Quell-Index + 1
        REQUIRE (combo.getSelectedId() == 3);
        REQUIRE (selectedKey.isEmpty());   // Anzeige ≠ User-Klick
    }

    SECTION ("Auswahl-Wechsel meldet trotz Separatoren den richtigen Key")
    {
        combo.setSelectedId (5, juce::sendNotificationSync);
        REQUIRE (selectedKey == "tap:m4l");

        combo.setSelectedId (1, juce::sendNotificationSync);
        REQUIRE (selectedKey == "master");
    }

    SECTION ("Menü-Einträge und Combo-Text tragen die Quellfarbe")
    {
        // Item-Farben im RootMenu (Position: Master, hw, SEP, live_a, …)
        juce::Array<juce::Colour> itemColours;
        for (juce::PopupMenu::MenuItemIterator iterator (*combo.getRootMenu()); iterator.next();)
            if (! iterator.getItem().isSeparator)
                itemColours.add (iterator.getItem().colour);

        REQUIRE (itemColours.size() == 5);
        REQUIRE (itemColours[1] == orange);
        REQUIRE (itemColours[2] == green);
        REQUIRE (itemColours[4] == cyan);

        // Combo-Text: Farbe der Auswahl; farblose Quelle → Standard-Text
        REQUIRE (combo.findColour (juce::ComboBox::textColourId) == green);

        combo.setSelectedId (1, juce::sendNotificationSync);
        REQUIRE (combo.findColour (juce::ComboBox::textColourId)
                 == conduit::push::colours::text);
    }
}

TEST_CASE ("LooperSlotCell: Thumbnail-Lifecycle (Commit-Snapshot)", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperSlotCell cell;
    cell.setBounds (0, 0, 120, 32);

    REQUIRE_FALSE (cell.hasThumbnail());

    juce::Image ink (juce::Image::ARGB, 64, 16, true);
    cell.setThumbnail (ink, juce::Colour (0xffffa726), 7, "Live / wavetable");

    REQUIRE (cell.hasThumbnail());
    REQUIRE (cell.getThumbnailClipId() == 7);
    REQUIRE (cell.getThumbnailSourceLabel() == "Live / wavetable");

    // Timer-Aufräumlogik: Mismatch der clipId → clear (Überschreib-Commit)
    cell.clearThumbnail();
    REQUIRE_FALSE (cell.hasThumbnail());
    REQUIRE (cell.getThumbnailClipId() == 0);
    REQUIRE (cell.getThumbnailSourceLabel().isEmpty());

    cell.clearThumbnail();   // idempotent (30-Hz-Timer ruft blind)
    REQUIRE_FALSE (cell.hasThumbnail());
}

TEST_CASE ("LooperSlotCell: Tinten-Deckung der Kopfzeilen-Zonen", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    // Obere Hälfte volle Tinte, untere transparent
    juce::Image ink (juce::Image::ARGB, 100, 100, true);
    {
        juce::Graphics g (ink);
        g.setColour (juce::Colours::black);
        g.fillRect (0, 0, 100, 50);
    }

    using Cell = conduit::LooperSlotCell;

    REQUIRE (Cell::computeInkCoverage (ink, { 0.0f, 0.0f, 1.0f, 0.5f })
             > 0.95f);                                                   // volle Zone
    REQUIRE (Cell::computeInkCoverage (ink, { 0.0f, 0.5f, 1.0f, 0.5f })
             < 0.05f);                                                   // leere Zone
    const auto mixed = Cell::computeInkCoverage (ink, { 0.0f, 0.0f, 1.0f, 1.0f });
    REQUIRE (mixed > 0.4f);                                              // halb/halb
    REQUIRE (mixed < 0.6f);

    // Ungültiges Bild / Zone außerhalb → 0 (kein Crash)
    REQUIRE (juce::exactlyEqual (
        Cell::computeInkCoverage (juce::Image(), { 0.0f, 0.0f, 1.0f, 1.0f }), 0.0f));
    REQUIRE (juce::exactlyEqual (
        Cell::computeInkCoverage (ink, { 2.0f, 2.0f, 0.5f, 0.5f }), 0.0f));
}

TEST_CASE ("LooperTrackStrip: Hooks liefern Track-lokale Indizes und Werte", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperTrackStrip strip { 2 };
    strip.setBounds (0, 0, 120, 520);

    SECTION ("Mute/Solo togglen über den externen Zustand")
    {
        bool muted = false, solo = false;
        strip.onMuteToggled = [&] (bool m) { muted = m; };
        strip.onSoloToggled = [&] (bool s) { solo = s; };

        strip.getMuteTile().onClick();
        
        REQUIRE (muted);

        strip.setMute (true);   // Editor bestätigt → nächster Klick toggelt aus
        strip.getMuteTile().onClick();
        
        REQUIRE_FALSE (muted);

        strip.getSoloTile().onClick();
        
        REQUIRE (solo);
    }

    SECTION ("Slot-Taps melden den Slot-Index; sichtbare Zahl folgt dem Setter")
    {
        int tapped = -1;
        strip.onSlotTapped = [&] (int slot) { tapped = slot; };

        strip.setVisibleSlots (6);
        REQUIRE (strip.getVisibleSlots() == 6);

        strip.getSlotCell (4).onTap();
        REQUIRE (tapped == 4);

        strip.setVisibleSlots (12);
        REQUIRE (strip.getVisibleSlots() == 12);
        strip.getSlotCell (11).onTap();
        REQUIRE (tapped == 11);
    }

    SECTION ("Slot-Zellen-Zustand: Setter idempotent, Zustände sichtbar")
    {
        auto& cell = strip.getSlotCell (0);

        conduit::LooperSlotCell::State state;
        state.hasClip = true;
        state.playing = true;
        state.progress01 = 0.5f;
        state.label = "Clip 1 · 4 Bars";
        state.rateBadge = juce::String::fromUTF8 ("0.50×");
        cell.setState (state);

        REQUIRE (cell.getState().playing);
        REQUIRE (cell.getState().rateBadge.isNotEmpty());

        state.playing = false;
        cell.setState (state);
        REQUIRE_FALSE (cell.getState().playing);
    }

    SECTION ("setProgress (VBlank-Sweep) wirkt nur auf spielende Zellen")
    {
        auto& cell = strip.getSlotCell (0);

        conduit::LooperSlotCell::State state;
        state.hasClip = true;
        state.playing = false;
        cell.setState (state);

        cell.setProgress (0.75f);   // gestoppt → no-op
        REQUIRE (cell.getState().progress01 == Approx (0.0f).margin (1.0e-6f));

        state.playing = true;
        cell.setState (state);
        cell.setProgress (0.75f);
        REQUIRE (cell.getState().progress01 == Approx (0.75f));

        // Struktur bleibt Sache von setState — setProgress ändert nur die Phase
        REQUIRE (cell.getState().playing);
        REQUIRE (cell.getState().hasClip);
    }
}

TEST_CASE ("LooperTrackStrip: Mixer — XY-Panner, Send-Kacheln, Display-Flags", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperTrackStrip strip { 1 };
    strip.setBounds (0, 0, 160, 620);

    SECTION ("XY-Panner: Setter spiegeln, Doppelklick reset meldet Hooks")
    {
        strip.setPan (0.5f);
        strip.setDistance (0.75f);
        REQUIRE (strip.getXyPad().getPan() == Catch::Approx (0.5f));
        REQUIRE (strip.getXyPad().getDistance() == Catch::Approx (0.75f));

        float lastPan = -9.0f, lastDistance = -9.0f;
        strip.onPanChanged = [&] (float pan) { lastPan = pan; };
        strip.onDistanceChanged = [&] (float distance) { lastDistance = distance; };

        // Doppelklick = Reset (Center, Distanz 0) — beide Hooks feuern
        const auto centre = strip.getXyPad().getLocalBounds().getCentre().toFloat();
        juce::MouseEvent dummy { juce::Desktop::getInstance().getMainMouseSource(),
                                 centre, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &strip.getXyPad(), &strip.getXyPad(),
                                 juce::Time::getCurrentTime(), centre,
                                 juce::Time::getCurrentTime(), 2, false };
        strip.getXyPad().mouseDoubleClick (dummy);
        REQUIRE (lastPan == Catch::Approx (0.0f));
        REQUIRE (lastDistance == Catch::Approx (0.0f));
    }

    SECTION ("Send-Kacheln: Level-Anzeige, Anzahl, Y-Link, Doppelklick = 0")
    {
        strip.setSendLevels ({ 0.4f, 0.0f, 1.0f, 0.2f });
        REQUIRE (strip.getSendTile (0).getLevel() == Catch::Approx (0.4f));
        REQUIRE (strip.getSendTile (2).getLevel() == Catch::Approx (1.0f));

        strip.setSendCount (2);
        REQUIRE (strip.getSendTile (0).isVisible());
        REQUIRE (strip.getSendTile (1).isVisible());
        REQUIRE_FALSE (strip.getSendTile (2).isVisible());
        REQUIRE_FALSE (strip.getSendTile (3).isVisible());

        int lastSend = -1;
        float lastLevel = -1.0f;
        strip.onSendLevelChanged = [&] (int s, float level)
        { lastSend = s; lastLevel = level; };

        auto& tile = strip.getSendTile (0);
        const auto centre = tile.getLocalBounds().getCentre().toFloat();
        juce::MouseEvent dummy { juce::Desktop::getInstance().getMainMouseSource(),
                                 centre, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &tile, &tile, juce::Time::getCurrentTime(), centre,
                                 juce::Time::getCurrentTime(), 2, false };
        tile.mouseDoubleClick (dummy);
        REQUIRE (lastSend == 0);
        REQUIRE (lastLevel == Catch::Approx (0.0f));
        REQUIRE (tile.getLevel() == Catch::Approx (0.0f));
    }

    SECTION ("Display-Flags: Mute+Solo und XY ausblendbar")
    {
        REQUIRE (strip.getMuteTile().isVisible());
        REQUIRE (strip.getXyPad().isVisible());

        strip.setShowMuteSolo (false);
        strip.setShowXy (false);
        REQUIRE_FALSE (strip.getMuteTile().isVisible());
        REQUIRE_FALSE (strip.getSoloTile().isVisible());
        REQUIRE_FALSE (strip.getXyPad().isVisible());

        strip.setShowMuteSolo (true);
        strip.setShowXy (true);
        REQUIRE (strip.getMuteTile().isVisible());
        REQUIRE (strip.getXyPad().isVisible());
    }
}

TEST_CASE ("LooperClipControlsRow: Dispatch nur mit Aktiv-Clip, VARI-Mapping", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LooperClipControlsRow row;
    row.setBounds (0, 0, 640, 58);

    int reversed = 0, cycles = 0;
    double lastRate = 1.0;
    bool lastRaster = false;
    double lastLenNorm = -1.0, lastPosNorm = -1.0;
    bool lastSyncFlag = false, lastSyncToggle = true;
    int lenEvents = 0, posEvents = 0, syncToggles = 0;

    row.onReverseToggled = [&] { ++reversed; };
    row.onTargetCycle = [&] { ++cycles; };
    row.onRateChanged = [&] (double rate) { lastRate = rate; };
    row.onRasterToggled = [&] (bool quantized) { lastRaster = quantized; };
    row.onSyncFreeToggled = [&] (bool sync) { lastSyncToggle = sync; ++syncToggles; };
    row.onLoopLenChanged = [&] (double norm, bool sync)
    { lastLenNorm = norm; lastSyncFlag = sync; ++lenEvents; };
    row.onLoopPosChanged = [&] (double norm, bool sync)
    { lastPosNorm = norm; lastSyncFlag = sync; ++posEvents; };

    SECTION ("Ohne Aktiv-Clip sind die Clip-Controls wirkungslos (Übergabe §3)")
    {
        row.setClipControlsEnabled (false);
        row.getReverseTile().onClick();
        row.getLenKnob().setValue (0.5, juce::sendNotificationSync);
        row.getPosKnob().setValue (0.5, juce::sendNotificationSync);

        REQUIRE (reversed + lenEvents + posEvents == 0);

        // Tape/Quant bleibt bedienbar (Track-Eigenschaft, kein Clip nötig)
        row.getRasterTile().onClick();

        REQUIRE (lastRaster);
    }

    SECTION ("Mit Aktiv-Clip feuern die Hooks; LEN/POS liefern Norm + Modus")
    {
        row.setClipControlsEnabled (true);
        row.getReverseTile().onClick();
        REQUIRE (reversed == 1);

        // Sync-Modus ist der Default; der Toggle meldet den Wechsel
        REQUIRE (row.isSyncMode());
        row.getSyncFreeTile().onClick();
        REQUIRE (syncToggles == 1);
        REQUIRE_FALSE (lastSyncToggle);
        row.setSyncFree (false);   // Editor spiegelt zurück
        REQUIRE_FALSE (row.isSyncMode());

        row.getLenKnob().setValue (0.25, juce::sendNotificationSync);
        REQUIRE (lenEvents == 1);
        REQUIRE (lastLenNorm == Approx (0.25));
        REQUIRE_FALSE (lastSyncFlag);   // Free-Modus aktiv

        row.getPosKnob().setValue (0.6, juce::sendNotificationSync);
        REQUIRE (posEvents == 1);
        REQUIRE (lastPosNorm == Approx (0.6));

        // Anzeige-Setter fassen die Knobs ohne Callback an
        row.setLoopLenNorm (1.0, "4 Bars");
        row.setLoopPosNorm (0.0, juce::String::fromUTF8 ("—"));
        REQUIRE (lenEvents == 1);
        REQUIRE (posEvents == 1);
    }

    SECTION ("LEN/POS-Mapping-Helfer: Sync-Raster + Free-Log")
    {
        using namespace conduit::looperui;
        REQUIRE (syncFractionFromNorm (1.0) == Approx (1.0));
        REQUIRE (syncFractionFromNorm (0.0) == Approx (0.125));
        REQUIRE (syncFractionFromNorm (2.0 / 3.0) == Approx (0.5));
        REQUIRE (syncNormFromFraction (0.25) == Approx (1.0 / 3.0));
        REQUIRE (freeLenSecondsFromNorm (0.0) == Approx (0.05));
        REQUIRE (freeLenSecondsFromNorm (1.0) == Approx (60.0));
        REQUIRE (freeLenNormFromSeconds (freeLenSecondsFromNorm (0.4))
                 == Approx (0.4));
    }

    SECTION ("VARI-Knob: Oktav-Mapping, Detent bei 1×, Rastung via snapFunction")
    {
        row.setClipControlsEnabled (true);

        // −1 Oktave → Rate 0.5
        row.getVariKnob().setValue (-1.0, juce::sendNotificationSync);
        REQUIRE (lastRate == Approx (0.5));

        // Detent: kleine Auslenkung fällt auf 1×
        row.getVariKnob().setValue (0.05, juce::sendNotificationSync);
        REQUIRE (lastRate == Approx (1.0));

        // Gerastert + Halbton-Snap: 0.30 Oktaven → 4/12 Oktaven
        row.setRasterQuantized (true);
        row.snapFunction = [] (double octaves) { return conduit::looperui::snapToSemitones (octaves); };
        row.getVariKnob().setValue (0.30, juce::sendNotificationSync);
        REQUIRE (lastRate == Approx (std::pow (2.0, 4.0 / 12.0)).epsilon (1.0e-6));
    }

    SECTION ("Pure Mapping-Helfer")
    {
        using namespace conduit::looperui;
        REQUIRE (rateFromOctaves (octavesFromRate (0.71)) == Approx (0.71));
        REQUIRE (rateFromOctaves (2.0) == Approx (4.0));
        REQUIRE (rateFromOctaves (-2.0) == Approx (0.25));
        REQUIRE (applyDetent (0.05) == Approx (0.0));
        REQUIRE (applyDetent (0.2) == Approx (0.2));
        REQUIRE (snapToSemitones (0.13) == Approx (2.0 / 12.0));
    }
}

TEST_CASE ("LooperDockTabs: LOOPER-Tab schreibt in die LooperSettings", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ConduitLooperDockTests")
                            .getChildFile (juce::Uuid().toString());
    folder.createDirectory();

    juce::PropertiesFile::Options options;
    options.applicationName = "LooperDockTests";
    options.filenameSuffix  = ".settings";
    options.folderName      = folder.getFullPathName();

    {
        conduit::LooperSettings settings { options };
        conduit::EditorDockPanel dock;
        conduit::LooperDockTabs tabs { dock, settings };

        // Tabs registriert, nur auf der Looper-Page sichtbar
        dock.setActivePage (conduit::TransportBar::pageLooper);
        dock.setActiveTab ("looper");
        REQUIRE (dock.getActiveTabId() == "looper");

        auto* content = tabs.getLooperTabContent();
        REQUIRE (content != nullptr);
        content->setBounds (0, 0, 320, 800);

        // Tiefensuche über die Sektionen/Zeilen-Hierarchie
        std::function<juce::Component* (juce::Component&, const juce::String&)> deepFind =
            [&] (juce::Component& parent, const juce::String& id) -> juce::Component*
        {
            if (parent.getComponentID() == id)
                return &parent;
            for (auto* child : parent.getChildren())
                if (auto* hit = deepFind (*child, id))
                    return hit;
            return nullptr;
        };
        const auto comboById = [&] (const juce::String& id)
        { return deepFind (*content, id); };

        auto* quant = dynamic_cast<juce::ComboBox*> (comboById ("quant"));
        REQUIRE (quant != nullptr);
        quant->setSelectedId ((int) conduit::LaunchQuant::off + 1,
                              juce::sendNotificationSync);
        REQUIRE (settings.getLaunchQuant() == conduit::LaunchQuant::off);

        auto* tap = dynamic_cast<juce::ComboBox*> (comboById ("tapMode"));
        REQUIRE (tap != nullptr);
        tap->setSelectedId (3, juce::sendNotificationSync);   // Legato
        REQUIRE (settings.getTapMode() == conduit::LooperSettings::TapMode::legato);

        auto* reverse = dynamic_cast<juce::ComboBox*> (comboById ("reverse"));
        REQUIRE (reverse != nullptr);
        reverse->setSelectedId (3, juce::sendNotificationSync);   // Quantized
        REQUIRE (settings.getReverseMode()
                 == conduit::LooperSettings::ReverseMode::quantized);

        auto* display = dynamic_cast<juce::ComboBox*> (comboById ("variDisplay"));
        REQUIRE (display != nullptr);
        display->setSelectedId (2, juce::sendNotificationSync);   // Scale Degrees
        REQUIRE (settings.getVariDisplay()
                 == conduit::LooperSettings::VariDisplay::scaleDegrees);

        auto* slots = dynamic_cast<juce::ComboBox*> (comboById ("slots"));
        REQUIRE (slots != nullptr);
        slots->setSelectedId (10, juce::sendNotificationSync);
        REQUIRE (settings.getVisibleSlots() == 10);

        auto* stopAll = dynamic_cast<juce::ToggleButton*> (comboById ("showStopAll"));
        REQUIRE (stopAll != nullptr);
        stopAll->setToggleState (false, juce::dontSendNotification);
        stopAll->onClick();
        REQUIRE_FALSE (settings.isShowStopAll());

        // Es gibt bewusst KEINE ÷2-Hälfte-Zeile mehr (LEN/POS-Potis)
        REQUIRE (comboById ("halve") == nullptr);
    }

    folder.deleteRecursively();
}

TEST_CASE ("LooperPage/Dock: Kopf-Umbau — MASTER-Sektion, Stop All", "[looper][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    SECTION ("MIXER · MASTER: Output-Paare + globaler MST im Seitenpanel")
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ConduitLooperMasterTests")
                                .getChildFile (juce::Uuid().toString());
        folder.createDirectory();

        juce::PropertiesFile::Options options;
        options.applicationName = "LooperMasterTests";
        options.filenameSuffix  = ".settings";
        options.folderName      = folder.getFullPathName();

        {
            conduit::LooperSettings settings { options };
            conduit::EditorDockPanel dock;
            conduit::LooperDockTabs tabs { dock, settings };

            auto* content = tabs.getMixerTabContent();
            REQUIRE (content != nullptr);
            content->setBounds (0, 0, 320, 400);

            std::function<juce::Component* (juce::Component&, const juce::String&)> deepFind =
                [&] (juce::Component& parent, const juce::String& id) -> juce::Component*
            {
                if (parent.getComponentID() == id)
                    return &parent;
                for (auto* child : parent.getChildren())
                    if (auto* hit = deepFind (*child, id))
                        return hit;
                return nullptr;
            };

            auto* combo = dynamic_cast<juce::ComboBox*> (deepFind (*content, "masterOutput"));
            REQUIRE (combo != nullptr);

            // Item 0 = „Kein Master-Out" (pairIndex −1, ADR 010); Paare dahinter
            const juce::StringArray pairs { "Out 1 / Out 2", "Out 3 / Out 4" };
            int selectedPair = -99;
            tabs.onOutputPairSelected = [&] (int pair) { selectedPair = pair; };

            tabs.setOutputPairs (pairs, 1);
            REQUIRE (combo->getSelectedItemIndex() == 2);
            REQUIRE (selectedPair == -99);

            tabs.setOutputPairs (pairs, 7);   // OOB → geclampt
            REQUIRE (combo->getSelectedItemIndex() == 2);

            combo->setSelectedItemIndex (1, juce::sendNotificationSync);
            REQUIRE (selectedPair == 0);

            combo->setSelectedItemIndex (0, juce::sendNotificationSync);
            REQUIRE (selectedPair == -1);

            tabs.setOutputPairs (pairs, -1);
            REQUIRE (combo->getSelectedItemIndex() == 0);

            // Globaler MST: Zustand + Hook
            auto* mst = dynamic_cast<conduit::push::TextTile*> (deepFind (*content, "mst"));
            REQUIRE (mst != nullptr);
            bool toMaster = false;
            tabs.onMasterToggled = [&] (bool on) { toMaster = on; };
            tabs.setMasterState (false);
            mst->onClick();
            REQUIRE (toMaster);
        }

        folder.deleteRecursively();
    }

    SECTION ("Stop All: Hook + Sichtbarkeit per Setting")
    {
        conduit::LooperPage page;
        page.setBounds (0, 0, 1400, 700);

        int stopped = 0;
        page.onStop = [&] { ++stopped; };
        page.getStopAllTile().onClick();
        REQUIRE (stopped == 1);

        REQUIRE (page.getStopAllTile().isVisible());
        page.setShowStopAll (false);
        REQUIRE_FALSE (page.getStopAllTile().isVisible());
        page.setShowStopAll (true);
        REQUIRE (page.getStopAllTile().isVisible());
    }

    SECTION ("FFT pro Looper: Page reicht an das Panel durch")
    {
        conduit::LooperPage page;
        page.setBounds (0, 0, 1400, 700);
        page.setLooperCount (2);

        page.setSpectrumView (1, true);
        REQUIRE_FALSE (page.getPanel (0).getViewTile().isActive());
        REQUIRE (page.getPanel (1).getViewTile().isActive());
    }
}

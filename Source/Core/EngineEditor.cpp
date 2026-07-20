#include "EngineEditor.h"

#include <algorithm>

#include "Browser/BrowserPaths.h"
#include "Core/SignalFlowColours.h"
#include "Modules/LooperPatchOutModule.h"
#include "EngineProcessor.h"
#include "Modules/LinkAudioReceiveModule.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/LooperPatchInModule.h"
#include "UI/LinkSendCreateDialog.h"
#include "Core/Looper/LooperClipExporter.h"
#include "UI/LooperDeleteConfirmDialog.h"
#include "UI/LooperDockTabs.h"
#include "UI/LooperTrashDialog.h"
#include "UI/SettingsWindow.h"
#include "UI/TrackSelectorPanel.h"
#include "UI/UiFramePacer.h"
#include "Util/ScaleQuantizer.h"

namespace conduit
{

EngineEditor::EngineEditor (EngineProcessor& engineProcessor,
                            juce::AudioDeviceManager* deviceManagerToUse)
    : juce::AudioProcessorEditor (engineProcessor),
      engine (engineProcessor),
      rootState (engineProcessor.getRootState()),
      undoManager (engineProcessor.getUndoManager()),
      graphManager (engineProcessor.getGraphManager()),
      linkClock (engineProcessor.getLinkClock()),
      deviceManager (deviceManagerToUse),
      transportBar (engineProcessor.getRootState(), engineProcessor.getLinkClock(),
                    engineProcessor.getTransportSettings()),
      capturePanel (engineProcessor.getCaptureService(), engineProcessor.getChannelNames()),
      canvas (rootState, engineProcessor.getGraphManager(), engineProcessor.getNodeUiRegistry(),
              &engineProcessor.getChannelNames(),
              &engineProcessor.getInputLevels(), &engineProcessor.getOutputLevels(),
              &engineProcessor.getInputLinkSend(), &engineProcessor.getUiSettings(),
              &engineProcessor.getPageManager(),
              &engineProcessor.getLooperOutLevels()),
      gridPage (rootState, engineProcessor.getGridVoiceEngine(),
               engineProcessor.getGridPanelSettings(), engineProcessor.getMpeMidiSink(),
               engineProcessor.getLiveSetModel(), engineProcessor.getTouchLiveClient(),
               engineProcessor.getMidiPortHub(), engineProcessor.getMidiRigSettings(),
               engineProcessor.getMidiProfileLibrary(), engineProcessor.getControllerProfileLibrary(),
               editorDock, engineProcessor.getGraphManager(), engineProcessor.getLinkClock()),
      touchLivePage (engineProcessor.getLiveSetModel(), engineProcessor.getTouchLiveClient(),
                     engineProcessor.getTouchLiveMeterBus(), engineProcessor.getTouchLiveSettings(),
                     &engineProcessor.getLiveSpectrumTap()),
      browserModel (engineProcessor.getModuleFactory(), browserContext, browserWorker),
      browserPanel (browserModel, engineProcessor.getUiSettings())
{
    // Push-3-Design app-weit: Jost + dunkle Kacheln auch in PopupMenus,
    // Dialogen und dem Settings-Fenster (CLAUDE.md 10)
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    // M9c (ADR 007): „HW Presets"-Zweig im Hardware-Picker des MacroPanels.
    gridPage.setHardwarePresetSources (engineProcessor.getHardwarePresetLibrary(),
                                       engineProcessor.getHardwarePresetScanner());

    // -- TransportBar-Hooks ---------------------------------------------------
    transportBar.onUndo = [this] { undoManager.undo(); };
    transportBar.onRedo = [this] { undoManager.redo(); };

    // M7: Session-Save liegt im Browser (PROJEKTE → „Session speichern…");
    // die Save-Kachel der Bar ist die Clip-Save-Geste der Looper-Page.
    transportBar.getSaveTile().onHoldChanged = [this] (bool holding)
    {
        looperGesture = holding ? LooperGesture::saveClips
                                : (looperDeleteLatched ? LooperGesture::deleteClips
                                                       : LooperGesture::none);
        transportBar.getSaveTile().setActive (holding);
    };
    transportBar.getSaveTile().onShortClick = [this]
    {
        captureToast.show (juce::String::fromUTF8 (
            "Save halten und Clips antippen — speichert die Clips als Dateien"));
    };

    // Delete-Geste (Push-Muster): Halten + Clip/Track-Header antippen;
    // Menü-Option macht die Kachel zum Latch-Toggle (Nicht-Touch)
    transportBar.getLooperDeleteTile().onHoldChanged = [this] (bool holding)
    {
        if (engine.getLooperSettings().isDeleteLatchEnabled())
            return;   // Latch-Modus: nur der Kurzklick schaltet

        looperGesture = holding ? LooperGesture::deleteClips : LooperGesture::none;
        transportBar.getLooperDeleteTile().setActive (holding);
    };
    transportBar.getLooperDeleteTile().onShortClick = [this]
    {
        if (! engine.getLooperSettings().isDeleteLatchEnabled())
        {
            captureToast.show (juce::String::fromUTF8 (
                "Delete halten und Clip antippen (Latch-Modus: ⚙-Menü)"));
            return;
        }

        looperDeleteLatched = ! looperDeleteLatched;
        looperGesture = looperDeleteLatched ? LooperGesture::deleteClips
                                            : LooperGesture::none;
        transportBar.getLooperDeleteTile().setActive (looperDeleteLatched);
    };

    transportBar.onSettings = [this]
    {
        // Non-modal (JUCE_MODAL_LOOPS_PERMITTED=0); Audio-Geräte-Tab nur im
        // Standalone-Pfad mit DeviceManager
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new SettingsWindow (deviceManager, engine.getMeterSettings(),
                                                      engine.getCaptureSettings(),
                                                      engine.getCaptureService(),
                                                      engine.getOscSendSettings(),
                                                      engine.getOscController(),
                                                      engine.getUiSettings(),
                                                      engine.getMidiRigSettings(),
                                                      engine.getMidiPortHub(),
                                                      engine.getMidiProfileLibrary(),
                                                      engine.getControllerProfileLibrary()));
        options.dialogTitle                   = "Einstellungen";
        options.dialogBackgroundColour        = push::colours::panel;
        options.escapeKeyTriggersCloseButton  = true;
        options.useNativeTitleBar             = true;
        options.resizable                     = true;
        options.launchAsync();
    };

    transportBar.onCaptureAll = [this]
    {
        const auto numTracks = engine.getCaptureService().exportAll();
        if (numTracks == 0)
            captureToast.show ("Keine aktive Aufnahme");
    };

    transportBar.onToggleCapturePanel = [this]
    {
        capturePanel.setVisible (! capturePanel.isVisible());
        resized();
    };

    transportBar.onPageSelected = [this] (int pageIndex)
    {
        // Block H: Tap auf den Grid-Page-Button bei SCHON aktiver Grid-Page
        // toggelt das Pad-Layout (64 Pads ↔ XY+Fader) statt Page-Wechsel.
        if (pageIndex == TransportBar::pageGrid
            && pageHost.getPage() == TransportBar::pageGrid)
        {
            gridPage.toggleLayoutMode();
            return;
        }

        selectPage (pageIndex);
    };

    // Block H v2 rev5: Long-Press auf dem Grid-Page-Button → Track-Selector.
    // Auswahl: Ziel-Track Monitor „In" + Grid-MPE-Port als Input, alle
    // anderen All-Ins-MIDI-Tracks wandern STATISCH aufs Master-MIDI-Device
    // (Monitor bleibt) — Lives Arm-/Selektions-Mechanik übernimmt den Rest.
    transportBar.onGridPageHold = [this]
    {
        auto panel = std::make_unique<TrackSelectorPanel> (engine.getLiveSetModel());

        // Gemeinsamer Command-Weg mit den Track-Tabs (Block H3).
        panel->onTrackChosen = [this] (const juce::String& trackKey)
        { gridPage.sendFocusCommand (trackKey); };

        juce::CallOutBox::launchAsynchronously (
            std::move (panel),
            transportBar.getPageTile (TransportBar::pageGrid).getScreenBounds(),
            nullptr);
    };

    // Block H v2: das Grid-Page-Icon zeigt den Pad-Layout-Modus (64 Pads =
    // Punktmatrix, XY+Fader = gemischtes Symbol) — die Kacheln auf der
    // Page sind entfallen.
    gridPage.onLayoutModeChanged = [this] (GridPanelSettings::GridLayoutMode mode)
    {
        transportBar.getPageTile (TransportBar::pageGrid)
            .setIcon (mode == GridPanelSettings::GridLayoutMode::xyFaders
                          ? push::Icon::gridMpeXy
                          : push::Icon::gridMpe);
    };
    gridPage.onLayoutModeChanged (engine.getGridPanelSettings().getGridLayoutMode());

    // Tape (oo) → Retro-Looper-Page (B3): Toggle Looper ↔ Device; die
    // Tape-LED folgt dem Page-Zustand über den Editor-Timer
    transportBar.onToggleLooperPage = [this]
    {
        selectPage (pageHost.getPage() == TransportBar::pageLooper
                        ? TransportBar::pageDevice
                        : TransportBar::pageLooper);
    };

    // Browser-Panel: Toggle über die Bar, Dock-Breite animiert → Layout
    transportBar.onToggleBrowserPanel = [this] { toggleBrowserPanel(); };
    browserPanel.onDockWidthChanged = [this] { resized(); };

    // Editor-Dock-Panel (S2; app-weit seit MIDI-Rig M5b): eigener Toggle,
    // unabhängig vom Browser; LED-Zustand sofort mit der geladenen
    // Persistenz synchronisieren (das Panel kann bereits offen starten,
    // GridPanelSettings::isEditorPanelOpen). GridPage hat seine Tabs im
    // Ctor registriert (Page-Maske "nur Grid-Page"); der EngineEditor
    // besitzt Layout- und Persistenz-Callbacks und koppelt die
    // Tab-Sichtbarkeit an die aktive Page (selectPage).
    addChildComponent (editorDock);
    editorDock.setVisible (editorDock.isPanelOpen());
    editorDock.onWidthChanged   = [this] { resized(); };
    editorDock.onWidthCommitted = [this] (int width)
    { engine.getGridPanelSettings().setEditorPanelWidth (width); };
    editorDock.onActiveTabChanged = [this] (const juce::String&)
    { gridPage.refreshDockModes(); };
    editorDock.setActivePage (pageHost.getPage());

    transportBar.onToggleEditorPanel = [this] { toggleEditorPanel(); };
    transportBar.setEditorPanelOpen (gridPage.isDockPanelOpen());

    // Tap-to-Load (M3): versetzt platzieren, damit gestapelte Nodes
    // greifbar bleiben; Link-Send braucht seinen Config-Dialog (7.2),
    // verankert an der getappten Zeile
    browserPanel.onModuleActivated = [this] (const juce::String& factoryKey,
                                             juce::Rectangle<int> rowScreenBounds)
    {
        if (factoryKey == LinkAudioSendModule::staticModuleId)
        {
            auto dialog = std::make_unique<LinkSendCreateDialog>();
            dialog->onCreate = [this] (std::vector<LinkAudioSendModule::InputMode> modes)
            {
                const auto offset  = 24 * (canvas.getNumNodeComponents() % 8);
                const auto created = graphManager.addModuleNode (
                    LinkAudioSendModule::staticModuleId, { 40 + offset, 40 + offset },
                    [capturedModes = std::move (modes)] (juce::ValueTree& tree)
                    { LinkAudioSendModule::applyInputConfig (tree, capturedModes); });
                jassertquiet (created.isValid());
            };

            juce::CallOutBox::launchAsynchronously (std::move (dialog),
                                                    rowScreenBounds, nullptr);
            return;
        }

        const auto offset  = 24 * (canvas.getNumNodeComponents() % 8);
        const auto created = graphManager.addModuleNode (factoryKey,
                                                         { 40 + offset, 40 + offset });
        jassertquiet (created.isValid());
    };

    // PROJEKTE-Zeile "Preset laden…" (Datei-Dialog für beliebige Pfade;
    // Save bleibt auf der Save-Kachel)
    browserPanel.onAction = [this] (const juce::String& actionId)
    {
        if (actionId == "load_preset")
            launchPresetChooser (false);
        else if (actionId == "save_preset")   // M7: Session-Save wohnt hier
            launchPresetChooser (true);
    };

    // Datei-Bereiche (M6): Verzeichnisse — Captures aus den CaptureSettings
    browserModel.directoriesProvider = [this]
    {
        return BrowserModel::Directories {
            browser_paths::projectsDirectory(),
            browser_paths::loopsDirectory(),
            browser_paths::oneShotsDirectory(),
            engine.getCaptureSettings().getExportDirectory()
        };
    };

    // Session-Load mit Bestätigung: es gibt (noch) keinen Dirty-Flag —
    // deshalb IMMER fragen, der aktuelle Patch wird ersetzt (undo-fähig)
    browserPanel.onLoadProject = [this] (const juce::File& projectFile)
    {
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Session laden",
            juce::String::fromUTF8 ("\xe2\x80\x9e") + projectFile.getFileNameWithoutExtension()
                + juce::String::fromUTF8 ("\xe2\x80\x9c laden? Der aktuelle Patch wird ersetzt."),
            "Laden", "Abbrechen", this,
            juce::ModalCallbackFunction::create ([this, projectFile] (int result)
            {
                if (result == 0)
                    return;   // abgebrochen

                const auto loadResult = engine.loadPreset (projectFile);

                if (loadResult.failed())
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "Session laden", loadResult.getErrorMessage());
            }));
    };

    // ── Looper-Page (M6, Design-Mock 05.07.2026) ────────────────────────
    // Panels folgen der Struktur in den LooperSettings; nach jedem
    // Neuaufbau verdrahtet wireLooperPanels die Hooks neu
    looperPage.onPanelsChanged = [this] { wireLooperPanels(); };

    looperPage.onAddLooper = [this]
    {
        auto& settings = engine.getLooperSettings();
        settings.setNumLoopers (settings.getNumLoopers() + 1);
        refreshLooperStructure();
    };

    // Looper schließen (Big Out 07/2026): leer UND unverkabelt = direkt;
    // sonst X/OK-Dialog — OK löscht Clips + Kabel in den Papierkorb
    // (~3 min rückgängig). Kein Modal-Loop (JUCE_MODAL_LOOPS_PERMITTED=0).
    looperPage.onRemoveLooper = [this]
    {
        auto& session = engine.getLooperSession();
        const auto last = session.getNumLoopers() - 1;
        if (last < 1)
            return;

        const auto hasClips = session.looperHasClips (last);
        const auto hasCables = engine.getGraphManager().hasLooperPatchOutCables (last, -1);

        if (! hasClips && ! hasCables)
        {
            if (const auto result = session.removeLastLooper(); result.failed())
            {
                captureToast.show (result.getErrorMessage());
                return;
            }

            auto& settings = engine.getLooperSettings();
            settings.setNumLoopers (settings.getNumLoopers() - 1);
            refreshLooperStructure();
            return;
        }

        auto message = juce::String::fromUTF8 ("Looper ") + juce::String (last + 1)
                     + juce::String::fromUTF8 (" enthält noch:");
        if (hasClips)
            message << juce::String::fromUTF8 ("\n\xe2\x80\xa2 Clips");
        if (hasCables)
            message << juce::String::fromUTF8 ("\n\xe2\x80\xa2 Kabel am Looper patch OUT");
        message << juce::String::fromUTF8 (
            "\n\nOK löscht beides (\xe2\x86\xba ~3 min rückgängig).");

        auto dialog = std::make_unique<LooperDeleteConfirmDialog> (
            juce::String::fromUTF8 ("Looper schließen?"), message);
        dialog->onConfirm = [this]
        {
            // Alle Tracks des letzten Loopers — Thumbnails parken
            stashLooperThumbnails (engine.getLooperSession().getNumLoopers() - 1, -1, -1);
            if (const auto result = engine.forceRemoveLastLooper(); result.failed())
                captureToast.show (result.getErrorMessage());
            refreshLooperStructure();
        };
        // Anker: Kopf des betroffenen (letzten) Loopers — die frühere
        // −-Kachel der Kopfzeile existiert seit dem Panel-Umzug nicht mehr
        const auto anchorPanel = juce::jlimit (0, looperPage.getLooperCount() - 1, last);
        juce::CallOutBox::launchAsynchronously (
            std::move (dialog),
            looperPage.getPanel (anchorPanel).getScreenBounds()
                .removeFromTop (44), nullptr);
    };

    // Papierkorb-Kachel (↺): EIN Eintrag = direkt wiederherstellen,
    // mehrere = Auswahl-Liste (neuester zuoberst, User 19.07.2026)
    looperPage.onRestoreTrash = [this]
    {
        const auto& entries = engine.getLooperTrash().getEntries();
        if (entries.empty())
            return;

        if (entries.size() == 1)
        {
            restoreLooperTrashFromUi (entries.front().entryId);
            return;
        }

        const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        std::vector<LooperTrashDialog::Item> items;
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        {
            LooperTrashDialog::Item item;
            item.entryId = it->entryId;

            using Kind = LooperTrashCan::Entry::Kind;
            if (it->kind == Kind::clip && ! it->clips.empty())
            {
                const auto& ref = it->clips.front();
                const auto stashed = trashedLooperThumbnails.find (ref.clipId);
                const auto name = stashed != trashedLooperThumbnails.end()
                                    ? stashed->second.sourceLabel
                                    : "Clip " + juce::String (ref.clipId);
                item.label = name + juce::String::fromUTF8 (" · L")
                           + juce::String (it->looperIndex + 1) + " T"
                           + juce::String (ref.track + 1) + " S"
                           + juce::String (ref.slot + 1);
            }
            else if (it->kind == Kind::track)
            {
                item.label = "Track " + juce::String (it->trackIndex + 1)
                           + " (Looper " + juce::String (it->looperIndex + 1) + ")"
                           + juce::String::fromUTF8 (" · ")
                           + juce::String ((int) it->clips.size())
                           + (it->clips.size() == 1 ? " Clip" : " Clips");
            }
            else
            {
                item.label = "Looper " + juce::String (it->looperIndex + 1)
                           + juce::String::fromUTF8 (" · ")
                           + juce::String (it->numTracksSnapshot)
                           + (it->numTracksSnapshot == 1 ? " Track" : " Tracks")
                           + juce::String::fromUTF8 (" · ")
                           + juce::String ((int) it->clips.size())
                           + (it->clips.size() == 1 ? " Clip" : " Clips");
            }
            if (! it->cables.empty())
                item.label << juce::String::fromUTF8 (" · Kabel");

            const auto seconds = juce::jmax (0, (int) std::lround (it->expiresAt - now));
            item.timeLabel = juce::String (seconds / 60) + ":"
                           + juce::String (seconds % 60).paddedLeft ('0', 2);
            items.push_back (std::move (item));
        }

        auto dialog = std::make_unique<LooperTrashDialog> (std::move (items));
        dialog->onRestore = [this] (std::uint32_t entryId)
        {
            restoreLooperTrashFromUi (entryId);
        };
        juce::CallOutBox::launchAsynchronously (
            std::move (dialog),
            looperPage.getTrashTile().getScreenBounds(), nullptr);
    };

    engine.getLooperTrash().onChanged = [this]
    {
        auto& trash = engine.getLooperTrash();
        looperPage.setTrashState (trash.secondsRemaining(), trash.hasEntries());
        // Ablauf/Restore: verwaiste geparkte Thumbnails freigeben
        purgeLooperThumbnails();
    };
    engine.getLooperTrash().onExpired = [this] { looperPage.flashTrashEmptied(); };

    // Patch-OUT-Slot-Farben (User-Skizze 19.07.2026): Clip-/Mischfarben aus
    // dem Spiel-Zustand — der 15-Hz-Tick stößt bei Änderung den Farb-Refresh an
    canvas.onResolveLooperOutColour = [this] (const juce::String& uuid, int channel)
    {
        return looperOutChannelRgb (uuid, channel);
    };

    // Looper-Überschriften der Patch-OUT-Kachel: Farbe der GEWÄHLTEN Quelle
    canvas.onResolveLooperHeaderColour = [this] (int looperIndex)
    {
        if (looperIndex < 0 || looperIndex >= engine.getLooperSession().getNumLoopers())
            return (juce::uint32) 0;

        const auto colour = looperSourceColour (
            engine.getLooperSettings().getSourceKey (looperIndex));
        return colour.isTransparent() ? 0u : (colour.getARGB() & 0x00ffffffu);
    };

    // Seitenpanel LOOPER · MIXER · MIDI (ersetzt das ⚙-CallOutBox-Menü):
    // Struktur-Hooks laufen über die Delete-Gating-Pfade der Page-Hooks
    looperDockTabs = std::make_unique<LooperDockTabs> (editorDock,
                                                       engine.getLooperSettings());
    looperDockTabs->onAddLooper = [this]
    {
        if (looperPage.onAddLooper != nullptr)
            looperPage.onAddLooper();
    };
    looperDockTabs->onRemoveLooper = [this]
    {
        if (looperPage.onRemoveLooper != nullptr)
            looperPage.onRemoveLooper();
    };
    looperDockTabs->onAddTrack = [this] (int looperIndex)
    {
        if (looperIndex >= 0 && looperIndex < looperPage.getLooperCount())
            if (auto& hook = looperPage.getPanel (looperIndex).onAddTrack; hook != nullptr)
                hook();
    };
    looperDockTabs->onRemoveTrack = [this] (int looperIndex)
    {
        const auto tracks = engine.getLooperSession().getNumTracks (looperIndex);
        if (tracks > 1)
            removeLooperTrack (looperIndex, tracks - 1);
    };

    looperPage.onStop = [this] { engine.stopLooper(); };

    // MST global (MIXER · MASTER, User 20.07.2026): EIN Toggle setzt
    // sendMaster ALLER Looper; Anzeige an = alle an
    looperDockTabs->onMasterToggled = [this] (bool toMaster)
    {
        auto& settings = engine.getLooperSettings();
        for (int l = 0; l < LooperSettings::maxLoopers; ++l)
            settings.setSendToMaster (l, toMaster);
        looperDockTabs->setMasterState (toMaster);
    };

    // Metronom-Ziel-Paare fürs Link-Menü: Labels aus den ChannelNames,
    // Kanalzahl aus dem audio_out-Tree-Node (folgt der Hardware)
    transportBar.metronomeTargetNames = [this] { return buildOutputPairNames(); };

    // Ausgabe-Paar des Loop-Playbacks (global, B6 — seit dem Kopf-Umbau
    // im MIXER · MASTER): Auswahl persistiert looperAnchor und routet
    // die Bank sofort um
    looperDockTabs->onOutputPairSelected = [this] (int pairIndex)
    { engine.setLooperAnchor (pairIndex); };

    // Startzustand: Struktur + Quellen aus den Settings ziehen
    refreshLooperStructure();
    engine.getLooperSettings().addChangeListener (this);

    // -- Capture-Panel + Toast ------------------------------------------------
    capturePanel.setVisible (false);
    capturePanel.onToast = [this] (const juce::String& message)
    { captureToast.show (message); };

    engine.getCaptureService().onExportFinished =
        [this] (const CaptureWriter::Report& report) { handleExportReport (report); };

    addAndMakeVisible (transportBar);
    addChildComponent (capturePanel);    // eingeklappt bis zum Shift-Klick
    addAndMakeVisible (pageHost);        // Device (Canvas) + Platzhalter-Pages
    addChildComponent (browserPanel);    // rechts angedockt, zeigt sich beim Toggle
    addChildComponent (captureToast);    // Overlay, zeigt sich selbst

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setSize (1480, 720);

    // Oberflächen-Einstellungen: Startwerte hat Main.cpp bereits angewendet
    // (bzw. im Test-Pfad bewusst niemand) — hier nur den Ist-Stand merken
    // und auf Live-Änderungen lauschen
    appliedUiScale   = engine.getUiSettings().getUiScale();
    appliedFontScale = engine.getUiSettings().getFontScale();
    engine.getUiSettings().addChangeListener (this);

    // UI-Framerate (User-Regel 14.07.2026): globales FPS-Limit fuer alle
    // UiFramePacer-Refreshes — Startwert + Live-Aenderungen (unten).
    uiframe::setFpsLimit (engine.getUiSettings().getUiFpsLimit());

    // Looper-Quellen folgen Tap-Registrierungen (CaptureService-Broadcast)
    // und Label-/Pairing-Änderungen (ChannelNames-Broadcast)
    engine.getCaptureService().addChangeListener (this);
    engine.getChannelNames().addChangeListener (this);

    // … und Node-Property-Änderungen (Link-Kanal-Wahl, Node-Farben,
    // I/O-Kanalzahl) — read/listen-only auf dem Root-Tree (6)
    rootState.addListener (this);

    // Dev-Tile + Dev-Panel (nur im Dev Mode)
    transportBar.onToggleDevPanel = [this] { toggleDevPanel(); };
    transportBar.setDevTileVisible (engine.getUiSettings().isDevModeEnabled());

    timerCallback();      // Status sofort befüllen, nicht erst nach 66ms
    startTimerHz (15);    // EIN Editor-Timer — lock-freie Reads, Repaint bei Änderung
}

EngineEditor::~EngineEditor()
{
    rootState.removeListener (this);
    engine.getLooperSettings().removeChangeListener (this);
    engine.getChannelNames().removeChangeListener (this);
    engine.getCaptureService().removeChangeListener (this);
    engine.getUiSettings().removeChangeListener (this);

    // Der Service überlebt den Editor — Callback lösen, sonst zeigte ein
    // späterer Export-Report ins Leere
    engine.getCaptureService().onExportFinished = nullptr;

    // Papierkorb überlebt den Editor ebenfalls — UI-Hooks lösen
    engine.getLooperTrash().onChanged = nullptr;
    engine.getLooperTrash().onExpired = nullptr;

    // Default-LookAndFeel VOR der Member-Destruktion zurücksetzen
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
void EngineEditor::valueTreePropertyChanged (juce::ValueTree& tree,
                                             const juce::Identifier& property)
{
    juce::ignoreUnused (tree);

    // Node-Farben, I/O-Kanalzahlen (auch Looper-In-Slot-Umbau) und
    // Slot-Namen ändern Labels/Farben der Looper-Quellauswahl —
    // Tap-Registrierung und Rename decken die CaptureService-Broadcasts
    // bereits ab
    if (property == id::targetPeer || property == id::targetChannel
        || property == id::nodeColour
        || property == id::numOutputChannels || property == id::numInputChannels
        || property == id::inputUserName || property == id::inputAutoName)
        rebuildLooperSources();
}

void EngineEditor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    juce::ignoreUnused (parent);

    // Neues/entferntes Kabel ändert die geerbte Quellfarbe der
    // Looper-In-Slots (und ggf. deren Auto-Namen via GraphManager)
    if (child.hasType (id::connection))
        rebuildLooperSources();
}

void EngineEditor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    juce::ignoreUnused (parent);

    if (child.hasType (id::connection))
        rebuildLooperSources();
}

void EngineEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // Looper-Quellen: Tap-Zeilen (CaptureService) und Kanal-Labels
    // (ChannelNames) ändern die Auswahl-Liste der Looper-Page
    if (source == &engine.getCaptureService() || source == &engine.getChannelNames())
    {
        rebuildLooperSources();
        return;
    }

    // Looper-Settings (M6): Struktur/Slots/Mix können sich auch von
    // außen ändern (Menü, spätere OSC-Wege) — Page nachziehen
    if (source == &engine.getLooperSettings())
    {
        refreshLooperStructure();
        return;
    }

    if (source != &engine.getUiSettings())
        return;

    auto& uiSettings = engine.getUiSettings();

    // UI-Framerate-Limit (UiFramePacer) — billig, einfach immer setzen.
    uiframe::setFpsLimit (uiSettings.getUiFpsLimit());

    // Globale Skalierung (Ableton-Verhalten: trifft ALLE Fenster inkl.
    // offener Dialoge; multipliziert sich auf das OS-Display-Scaling)
    if (! juce::exactlyEqual (appliedUiScale, uiSettings.getUiScale()))
    {
        appliedUiScale = uiSettings.getUiScale();
        juce::Desktop::getInstance().setGlobalScaleFactor (appliedUiScale);
    }

    // Schriftgröße: Faktor setzen + LnF-Kaskade über alle Desktop-Fenster
    // (MainWindow, Settings-Dialog, DevPanel, CallOutBoxen). Die Kaskade
    // ist ein Full-Repaint — nur bei echtem Font-Delta feuern.
    if (! juce::exactlyEqual (appliedFontScale, uiSettings.getFontScale()))
    {
        appliedFontScale = uiSettings.getFontScale();
        push::setFontScale (appliedFontScale);

        auto& desktop = juce::Desktop::getInstance();

        for (int i = desktop.getNumComponents(); --i >= 0;)
            if (auto* component = desktop.getComponent (i))
                component->sendLookAndFeelChange();
    }

    // Soft-Keyboard-Setting live umschalten: aus → offene Tastatur einklappen
    browserPanel.refreshSoftKeyboardSetting();

    // Dev Mode: Tile-Sichtbarkeit folgt; Deaktivieren schließt das Panel
    transportBar.setDevTileVisible (uiSettings.isDevModeEnabled());

    if (! uiSettings.isDevModeEnabled() && devPanel != nullptr)
    {
        devPanel.reset();
        transportBar.setDevPanelOpen (false);
    }
}

void EngineEditor::toggleDevPanel()
{
    if (devPanel != nullptr)
    {
        devPanel.reset();
    }
    else
    {
        devPanel = std::make_unique<DevPanel> (engine.getUiSettings(), engine.getGridPanelSettings());
        devPanel->onClose = [this] { closeDevPanelAsync(); };
        devPanel->centreAroundComponent (this, devPanel->getWidth(), devPanel->getHeight());
    }

    transportBar.setDevPanelOpen (devPanel != nullptr);
}

void EngineEditor::closeDevPanelAsync()
{
    // Das Fenster darf sich nicht aus dem eigenen Close-Callback heraus
    // destruieren — async über SafePointer (Muster Friedhof/TransportBar)
    juce::Component::SafePointer<EngineEditor> self (this);

    juce::MessageManager::callAsync ([self]
    {
        if (self != nullptr)
        {
            self->devPanel.reset();
            self->transportBar.setDevPanelOpen (false);
        }
    });
}

//==============================================================================
//==============================================================================
std::vector<LooperPanel::Source> EngineEditor::buildLooperSources()
{
    // Looper-I/O (ADR 010, User-Entscheidung 18.07.2026): die Liste ist
    // KOMPLETT ersetzt — Looper-In-Slots zuoberst, dann die Interface-
    // Eingänge (Mono/Stereo folgt dem ∥-Pairing der ChannelNames).
    // Master/Out-Paare/fremde Taps sind bewusst raus: solche Signale
    // loopt man per Kabel ins Looper-In-Modul.
    std::vector<LooperPanel::Source> sources;
    auto nodesTree = rootState.getChildWithName (id::nodes);

    // 1) Looper-In-Slots, gruppiert pro Modul-Instanz (Separator zwischen
    //    Instanzen); der Modulname im Label disambiguiert
    bool firstModule = true;
    for (int n = 0; n < nodesTree.getNumChildren(); ++n)
    {
        auto node = nodesTree.getChild (n);
        if (GraphManager::factoryKeyOf (node) != LooperPatchInModule::staticModuleId)
            continue;

        const auto moduleId = node.getProperty (id::moduleId).toString();
        const auto inputsTree = node.getChildWithName (id::inputs);

        for (int i = 0; i < inputsTree.getNumChildren(); ++i)
        {
            const auto inputTree = inputsTree.getChild (i);
            const auto name = LooperPatchInModule::effectiveInputName (inputTree, i);
            const auto key = "tap:" + LooperPatchInModule::tapBaseName (moduleId, name);

            // Quellname ZUERST (Auto-Naming folgt der verkabelten Quelle,
            // User-Regel 19.07.2026), dahinter Modul (disambiguiert gegen
            // die Hardware-Labels) und Aufnahmeart
            const auto mono = inputTree.getProperty (id::inputMode).toString()
                                  == LooperPatchInModule::modeMono;
            const auto label = name + juce::String::fromUTF8 (" · ") + moduleId
                             + juce::String::fromUTF8 (mono ? " · mono" : " · stereo");

            sources.push_back ({ key, label, looperSourceColour (key),
                                 ! firstModule && i == 0 });
        }

        firstModule = false;
    }

    // 2) Interface-Eingänge: Kanalzahl aus dem audio_in-Tree-Node (folgt
    //    der Hardware), Mono/Stereo nach ∥-Pairing, Labels/Farben aus den
    //    ChannelNames. Gerade verankerte Paare behalten den Legacy-Key
    //    "hw:{paar}" (gespeicherte Auswahlen bleiben gültig), ungerade
    //    Anker bekommen "hws:{kanal}", ungepaarte Kanäle "hwm:{kanal}".
    auto inNode = nodesTree.getChildWithProperty (id::factoryId, juce::String (audioInputModuleId));
    if (! inNode.isValid())
        inNode = nodesTree.getChildWithProperty (id::moduleId, juce::String (audioInputModuleId));

    const auto channels = inNode.isValid()
                        ? (int) inNode.getProperty (id::numOutputChannels, 2) : 2;
    auto& labels = engine.getChannelNames();
    using Direction = ChannelNames::Direction;

    bool firstHardware = true;
    for (int channel = 0; channel < channels; )
    {
        const auto paired = channel + 1 < channels
                         && labels.isPortPairStart (Direction::input, channel);

        juce::String key, label;
        if (paired)
        {
            key = channel % 2 == 0 ? "hw:" + juce::String (channel / 2)
                                   : "hws:" + juce::String (channel);
            label = labels.getLabel (Direction::input, channel)
                  + " / " + labels.getLabel (Direction::input, channel + 1);
        }
        else
        {
            key = "hwm:" + juce::String (channel);
            label = labels.getLabel (Direction::input, channel);
        }

        sources.push_back ({ key, label, looperSourceColour (key),
                             firstHardware && ! sources.empty() });
        firstHardware = false;
        channel += paired ? 2 : 1;
    }

    return sources;
}

juce::Colour EngineEditor::looperSourceColour (const juce::String& sourceKey) const
{
    auto fromRgb = [] (juce::uint32 rgb)
    {
        return rgb != 0 ? juce::Colour (0xff000000u | (rgb & 0x00ffffffu))
                        : juce::Colour();
    };

    auto& labels = engine.getChannelNames();
    using Direction = ChannelNames::Direction;

    // Paar-Farbe: linker Kanal, sonst rechter (vereinfachte Anker-Logik)
    auto pairColour = [&] (Direction direction, int leftChannel)
    {
        const auto left = labels.getColour (direction, leftChannel);
        return fromRgb (left != 0 ? left
                                  : labels.getColour (direction, leftChannel + 1));
    };

    if (sourceKey.startsWith ("hw:"))
        return pairColour (Direction::input, sourceKey.substring (3).getIntValue() * 2);

    if (sourceKey.startsWith ("hws:"))
        return pairColour (Direction::input, sourceKey.substring (4).getIntValue());

    if (sourceKey.startsWith ("hwm:"))
        return fromRgb (labels.getColour (Direction::input,
                                          sourceKey.substring (4).getIntValue()));

    if (sourceKey.startsWith ("tap:"))
    {
        // Looper-In-Slots heißen "{moduleId}/{slotName}": Farbe der an den
        // Slot VERKABELTEN Quelle (Signal-Flow-Vererbung — die Kette
        // Eingang → FX → Slot → Waveform → Clip bleibt durchgängig,
        // User-Regel 19.07.2026); Fallback Modul-Farbe. Klassische Taps
        // tragen die moduleId direkt.
        const auto base = sourceKey.substring (4);
        const auto moduleId = base.contains ("/")
                                  ? base.upToFirstOccurrenceOf ("/", false, false)
                                  : base;

        const auto node = rootState.getChildWithName (id::nodes)
                              .getChildWithProperty (id::moduleId, moduleId);
        if (! node.isValid())
            return {};

        if (base.contains ("/"))
        {
            // Slot per effektivem Namen finden → Kanal-Offset → Kabelquelle
            const auto slotName = base.fromFirstOccurrenceOf ("/", false, false);
            const auto inputsTree = node.getChildWithName (id::inputs);

            int offset = 0;
            for (int i = 0; i < inputsTree.getNumChildren(); ++i)
            {
                const auto inputTree = inputsTree.getChild (i);
                if (LooperPatchInModule::effectiveInputName (inputTree, i) == slotName)
                {
                    const auto rgb = flow_colours::resolveDestSourceRgb (
                        rootState, &engine.getChannelNames(),
                        node.getProperty (id::nodeId).toString(), offset);
                    if (rgb != 0)
                        return fromRgb (rgb);
                    break;
                }

                offset += inputTree.getProperty (id::inputMode).toString()
                              == LooperPatchInModule::modeStereo ? 2 : 1;
            }
        }

        return fromRgb ((juce::uint32) (int) node.getProperty (id::nodeColour, 0));
    }

    return {};   // master / unbekannt → Strip-Default
}

void EngineEditor::rebuildLooperSources()
{
    // Dieselbe Quellen-Liste für alle Panels; Auswahl pro Looper aus den
    // LooperSettings (M6)
    const auto sources = buildLooperSources();
    auto& settings = engine.getLooperSettings();

    for (int l = 0; l < looperPage.getLooperCount(); ++l)
    {
        auto& panel = looperPage.getPanel (l);
        panel.setSources (sources, settings.getSourceKey (l));

        // Wellenform/Spektrum in der Farbe der Quelle (08.07.2026)
        panel.getStrip().setSourceColour (looperSourceColour (settings.getSourceKey (l)));
    }

    // Ausgabe-Paare hängen an denselben Broadcasts (ChannelNames/Hardware)
    if (looperDockTabs != nullptr)
        looperDockTabs->setOutputPairs (buildOutputPairNames(),
                                        engine.getTransportSettings().getLooperAnchor());
}

//==============================================================================
void EngineEditor::refreshLooperStructure()
{
    auto& settings = engine.getLooperSettings();
    auto& session = engine.getLooperSession();

    // Panels folgen der Settings-Struktur; Hooks IMMER (re-)verdrahten —
    // beim Startaufruf hat die Page ihr Panel schon aus dem ctor
    // (setLooperCount wäre dann ein No-op ohne onPanelsChanged)
    looperPage.setLooperCount (settings.getNumLoopers());
    wireLooperPanels();

    for (int l = 0; l < looperPage.getLooperCount(); ++l)
    {
        auto& panel = looperPage.getPanel (l);
        panel.setTrackCount (settings.getNumTracks (l));
        panel.setVisibleSlots (settings.getVisibleSlots());

        for (int t = 0; t < panel.getTrackCount(); ++t)
        {
            auto& track = panel.getTrack (t);
            track.setDisplayNumber (LooperPatchOutModule::globalTrackNumber (l + 1, t + 1));
            track.setGain (settings.getTrackGain (l, t));
            track.setPan (settings.getTrackPan (l, t));
            track.setDistance (settings.getTrackDistance (l, t));
            track.setMute (settings.isTrackMuted (l, t));
            track.setSolo (settings.isTrackSolo (l, t));

            std::array<float, 4> levels {};
            for (int s = 0; s < 4; ++s)
                levels[(std::size_t) s] = settings.getTrackSendLevel (l, t, s);
            track.setSendLevels (levels);
            track.setSendCount (settings.getSendCount());
            track.setYLinkSend (settings.getYLinkSend());
            track.setShowMuteSolo (! settings.isHideMuteSolo());
            track.setShowXy (! settings.isHideMixerXy());
        }

        // VARI-Rast-Zustand der Controls: Scope-abhängig (Track des
        // Aktiv-Clips bzw. Track 0 als Looper-Vertreter)
        const auto active = session.getActiveSlot (l);
        const auto rasterTrack = settings.getVariScope() == LooperSettings::VariScope::perTrack
                               ? juce::jmax (0, active.track) : 0;
        panel.getControls().setRasterQuantized (
            settings.isTrackVariQuantized (l, rasterTrack));
        panel.getControls().setTargetVisible (panel.getTrackCount() > 1);
    }

    for (int l = 0; l < looperPage.getLooperCount(); ++l)
        looperPage.setSpectrumView (l, settings.isSpectrumView (l));
    looperPage.setShowStopAll (settings.isShowStopAll());

    if (looperDockTabs != nullptr)
    {
        looperDockTabs->refreshLayout();

        // MST-Anzeige: an = ALLE Looper senden an den Master
        bool allToMaster = true;
        for (int l = 0; l < LooperSettings::maxLoopers; ++l)
            allToMaster = allToMaster && settings.isSendToMaster (l);
        looperDockTabs->setMasterState (allToMaster);
    }
    rebuildLooperSources();
}

void EngineEditor::wireLooperPanels()
{
    for (int l = 0; l < looperPage.getLooperCount(); ++l)
    {
        auto& panel = looperPage.getPanel (l);

        // Strip: eigener Waveform-Tap pro Looper (M4), Beat-Achse LinkClock
        panel.getStrip().setDataSource (&engine.getLooperWaveformTap (l));
        panel.getStrip().getBeatNow = [this] { return linkClock.getBeatPosition(); };

        panel.onSourceSelected = [this, l] (const juce::String& key)
        {
            engine.setLooperSource (l, key);
            looperPage.getPanel (l).getStrip().setSourceColour (looperSourceColour (key));
        };

        // FFT/WAVE pro Looper (07/2026, ersetzt die MST-Kachel des Kopfs):
        // Settings pro Looper, Kachel + Strip folgen sofort
        panel.setSpectrumView (engine.getLooperSettings().isSpectrumView (l));
        panel.onViewToggled = [this, l] (bool spectrum)
        {
            engine.getLooperSettings().setSpectrumView (l, spectrum);
            looperPage.getPanel (l).setSpectrumView (spectrum);
        };

        // Segment-Klick = Commit in den Target-Slot dieses Loopers;
        // danach schnappt die Ziel-Zelle die Strip-Ansicht des Loops
        panel.onSegmentClicked = [this, l] (int bars)
        {
            const auto result = engine.commitToTarget (l, bars);
            if (result.failed())
            {
                captureToast.show (result.getErrorMessage());
                return;
            }

            captureLooperClipThumbnail (l);
        };

        panel.onSlotTapped = [this, l] (int t, int s) { handleLooperSlotTap (l, t, s); };

        // Mixer: Persistenz in die LooperSettings — die Engine folgt über
        // applyLooperSettings (ein Pfad, keine Doppel-Writes)
        panel.onTrackGain = [this, l] (int t, float gain)
        { engine.getLooperSettings().setTrackGain (l, t, gain); };
        panel.onTrackPan = [this, l] (int t, float pan)
        { engine.getLooperSettings().setTrackPan (l, t, pan); };
        panel.onTrackMute = [this, l] (int t, bool muted)
        { engine.getLooperSettings().setTrackMuted (l, t, muted); };
        panel.onTrackSolo = [this, l] (int t, bool solo)
        { engine.getLooperSettings().setTrackSolo (l, t, solo); };

        // Mixer 07/2026: Distanz (XY-Y) + Send-LEVEL direkt in die
        // Settings — die Engine folgt über applyLooperSettings (der
        // frühere SND-Dialog ist durch die Send-Kacheln ersetzt)
        panel.onTrackDistance = [this, l] (int t, float distance)
        { engine.getLooperSettings().setTrackDistance (l, t, distance); };
        panel.onTrackSendLevel = [this, l] (int t, int s, float level)
        { engine.getLooperSettings().setTrackSendLevel (l, t, s, level); };

        panel.onTrackStop = [this, l] (int t)
        {
            engine.getLooperSession().stopTrack (
                l, t, launchQuantBeats (engine.getLooperSettings().getLaunchQuant()));
        };

        panel.onAddTrack = [this, l]
        {
            auto& looperSettings = engine.getLooperSettings();
            looperSettings.setNumTracks (l, looperSettings.getNumTracks (l) + 1);
            refreshLooperStructure();
        };

        // Track entfernen: Long-Press auf Header ODER Delete-Geste +
        // Header-Tap (M7) — nur der LETZTE Track (M4-Entscheidung,
        // Bank-Player sind positional)
        panel.onTrackHeaderLongPress = [this, l] (int t) { removeLooperTrack (l, t); };
        panel.onTrackHeaderTapped = [this, l] (int t)
        {
            if (looperGesture == LooperGesture::deleteClips)
                removeLooperTrack (l, t);
        };

        // ── Clip-Controls (wirken auf den Aktiv-Clip, Übergabe §2) ──────
        auto& controls = panel.getControls();

        controls.onDoubleLength = [this, l]
        {
            if (auto* clip = engine.getLooperSession().getActiveClip (l))
                engine.getLooperBank().multiplyClipLength (*clip, true,
                                         engine.getLooperSettings().getHalveMode());
        };
        controls.onHalveLength = [this, l]
        {
            if (auto* clip = engine.getLooperSession().getActiveClip (l))
                engine.getLooperBank().multiplyClipLength (*clip, false,
                                         engine.getLooperSettings().getHalveMode());
        };
        controls.onReverseToggled = [this, l]
        {
            if (auto* clip = engine.getLooperSession().getActiveClip (l))
                engine.getLooperBank().toggleClipReverse (*clip,
                                        engine.getLooperSettings().getReverseMode()
                                            == LooperSettings::ReverseMode::boundary);
        };
        controls.onRateChanged = [this, l] (double rate)
        {
            if (auto* clip = engine.getLooperSession().getActiveClip (l))
                engine.getLooperBank().setClipRate (*clip, rate);
        };
        controls.onResetWithSync = [this, l]
        {
            if (auto* clip = engine.getLooperSession().getActiveClip (l))
            {
                engine.getLooperBank().resetClipWithSync (*clip);
                looperPage.getPanel (l).getControls().setRate (1.0);
            }
        };

        controls.onRasterToggled = [this, l] (bool quantized)
        {
            auto& looperSettings = engine.getLooperSettings();
            if (looperSettings.getVariScope() == LooperSettings::VariScope::perLooper)
            {
                for (int t = 0; t < looperSettings.getNumTracks (l); ++t)
                    looperSettings.setTrackVariQuantized (l, t, quantized);
            }
            else
            {
                const auto active = engine.getLooperSession().getActiveSlot (l);
                looperSettings.setTrackVariQuantized (l, juce::jmax (0, active.track),
                                                      quantized);
            }
            looperPage.getPanel (l).getControls().setRasterQuantized (quantized);
        };

        controls.onTargetCycle = [this, l]
        { engine.getLooperSession().cycleTargetTrack (l); };
        controls.onTargetHold = [this, l] (bool holding)
        { looperTargetHold[(size_t) l] = holding; };

        // Rast-Funktion: Halbtöne, optional auf die Session-Skala
        // eingeschränkt (Intervall-Quantisierung über die Skala-Maske)
        controls.snapFunction = [this] (double octaves)
        {
            const auto semis = (int) std::round (octaves * 12.0);
            if (engine.getLooperSettings().getVariRaster()
                    == LooperSettings::VariRaster::semitones)
                return semis / 12.0;

            const auto type = scaleTypeFromString (
                engine.getRootState().getProperty (id::scaleType).toString());

            for (int distance = 0; distance <= 6; ++distance)
                for (const auto candidate : { semis - distance, semis + distance })
                    if (scale::isInScale (candidate, type))
                        return candidate / 12.0;

            return semis / 12.0;
        };
    }

    // Nach dem Neuaufbau Quellen-Listen + Zustände nachziehen
    rebuildLooperSources();
}

void EngineEditor::removeLooperTrack (int looperIndex, int trackIndex)
{
    auto& session = engine.getLooperSession();

    if (trackIndex != session.getNumTracks (looperIndex) - 1)
    {
        captureToast.show (juce::String::fromUTF8 (
            "Nur der letzte Track kann entfernt werden"));
        return;
    }

    // Gating (Big Out 07/2026): leer UND unverkabelt = direkt; sonst
    // X/OK-Dialog — OK löscht Clips + Kabel in den Papierkorb
    const auto hasClips = session.trackHasClips (looperIndex, trackIndex);
    const auto hasCables = engine.getGraphManager().hasLooperPatchOutCables (looperIndex,
                                                                           trackIndex);

    if (! hasClips && ! hasCables)
    {
        if (const auto result = session.removeLastTrack (looperIndex); result.failed())
        {
            captureToast.show (result.getErrorMessage());
            return;
        }

        engine.getLooperSettings().setNumTracks (looperIndex,
                                                 session.getNumTracks (looperIndex));
        refreshLooperStructure();
        return;
    }

    auto message = juce::String::fromUTF8 ("Track ") + juce::String (trackIndex + 1)
                 + juce::String::fromUTF8 (" (Looper ") + juce::String (looperIndex + 1)
                 + juce::String::fromUTF8 (") enthält noch:");
    if (hasClips)
        message << juce::String::fromUTF8 ("\n\xe2\x80\xa2 Clips");
    if (hasCables)
        message << juce::String::fromUTF8 ("\n\xe2\x80\xa2 Kabel am Looper patch OUT");
    message << juce::String::fromUTF8 (
        "\n\nOK löscht beides (\xe2\x86\xba ~3 min rückgängig).");

    auto dialog = std::make_unique<LooperDeleteConfirmDialog> (
        juce::String::fromUTF8 ("Track entfernen?"), message);
    dialog->onConfirm = [this, looperIndex]
    {
        // Letzter Track fällt — dessen Thumbnails VOR dem Detach parken
        stashLooperThumbnails (looperIndex,
                               engine.getLooperSession().getNumTracks (looperIndex) - 1,
                               -1);
        if (const auto result = engine.forceRemoveLooperTrack (looperIndex);
            result.failed())
            captureToast.show (result.getErrorMessage());
        refreshLooperStructure();
    };
    juce::CallOutBox::launchAsynchronously (
        std::move (dialog),
        looperPage.getPanel (looperIndex).getTrack (trackIndex).getScreenBounds(),
        nullptr);
}

void EngineEditor::handleLooperSlotTap (int looperIndex, int trackIndex, int slotIndex)
{
    auto& session = engine.getLooperSession();
    auto& settings = engine.getLooperSettings();

    // M7: Header-Gesten zuerst — Delete/Save halten (bzw. Latch) macht den
    // Slot-Tap zur Ziel-Auswahl der Geste, nie zum Launch
    if (looperGesture == LooperGesture::deleteClips)
    {
        // Einzel-Clip-Delete wandert in den Papierkorb (User 19.07.2026):
        // Thumbnail VOR dem Detach parken — die Zelle räumt gleich auf
        stashLooperThumbnails (looperIndex, trackIndex, slotIndex);
        if (const auto result = engine.trashClipSlot (looperIndex, trackIndex, slotIndex);
            result.failed())
            captureToast.show (result.getErrorMessage());
        return;
    }

    if (looperGesture == LooperGesture::saveClips)
    {
        // M9: Clip als sample-alignte BWF-Dateien (_l/_r) über die
        // CaptureWriter-Pipeline — Abschluss meldet der Export-Toast
        if (auto* clip = session.clipAt (looperIndex, trackIndex, slotIndex))
        {
            const auto baseName = "looper" + juce::String (looperIndex + 1)
                                + "_clip" + juce::String (clip->clipId);
            const auto result = LooperClipExporter::exportClip (
                engine.getCaptureService(), *clip, baseName, engine.getSampleRate());

            captureToast.show (result.wasOk()
                                   ? baseName + " wird gespeichert " + juce::String::fromUTF8 ("…")
                                   : result.getErrorMessage());
        }
        return;
    }

    // TARGET wird gehalten → nur Aktiv-Auswahl, kein Launch (Übergabe §2)
    if (looperTargetHold[(size_t) juce::jlimit (0, 3, looperIndex)])
    {
        session.setActiveSlot (looperIndex, trackIndex, slotIndex);
        return;
    }

    auto* clip = session.clipAt (looperIndex, trackIndex, slotIndex);
    if (clip == nullptr)
    {
        // Leerer Slot: Target armen/disarmen
        session.armTarget (looperIndex, trackIndex, slotIndex);
        return;
    }

    const auto qBeats = launchQuantBeats (settings.getLaunchQuant());
    juce::Result result = juce::Result::ok();

    if (session.getPlayingSlot (looperIndex, trackIndex) == slotIndex)
    {
        // Tap auf den SPIELENDEN Clip: Verhalten aus dem Menü
        if (settings.getTapMode() == LooperSettings::TapMode::toggleStop)
            session.stopTrack (looperIndex, trackIndex, qBeats);
        else
            result = session.retriggerSlot (looperIndex, trackIndex, slotIndex, qBeats);
    }
    else
    {
        result = session.startSlot (looperIndex, trackIndex, slotIndex, qBeats);
    }

    if (result.failed())
        captureToast.show (result.getErrorMessage());
}

void EngineEditor::captureLooperClipThumbnail (int looperIndex)
{
    // Commit setzt Aktiv-Clip + -Slot (LooperSessionModel-Doku) — der
    // frische Clip liefert die bar-genaue Beat-Range, der Strip rendert
    // seine aktuelle Ansicht (Waveform/Spektrum) als Tinte-Bild. Sofort
    // rendern: History- und Spektrum-Ring halten nur ~16 Takte.
    auto& session = engine.getLooperSession();
    const auto address = session.getActiveSlot (looperIndex);
    auto* clip = session.getActiveClip (looperIndex);
    if (clip == nullptr || ! address.isValid())
        return;

    auto& panel = looperPage.getPanel (looperIndex);

    // Quellfarbe am Clip einfrieren (Patch-OUT-Ports/Bus-Mischung) — auch
    // wenn die Thumbnail-Zelle unten nicht erreichbar ist
    {
        const auto source = panel.getStrip().getSourceColour();
        clip->sourceRgb.store (source.isTransparent()
                                   ? 0u
                                   : (source.getARGB() & 0x00ffffffu),
                               std::memory_order_relaxed);
    }

    if (address.track >= panel.getTrackCount())
        return;

    auto& track = panel.getTrack (address.track);
    if (address.slot >= track.getVisibleSlots())
        return;

    auto& strip = panel.getStrip();

    // Zellfläche = Quellfarbe; farblose Quellen (Master) nutzen das
    // Strip-Default-Grün — die Inversion bleibt konsistent zur Anzeige
    auto background = strip.getSourceColour();
    if (background.isTransparent())
        background = push::colours::ledGreen;

    constexpr int thumbnailWidth  = 256;   // 32 Spalten pro Takt beim 8-Bar-Commit
    constexpr int thumbnailHeight = 64;

    // Quell-Text der Combo („Live / wavetable") friert als Zell-Label ein —
    // die Quelle des Loopers darf danach wechseln (User 09.07.2026)
    track.getSlotCell (address.slot).setThumbnail (
        strip.renderCommitThumbnail (clip->commitEndBeat - clip->contentBeats,
                                     clip->commitEndBeat,
                                     thumbnailWidth, thumbnailHeight),
        background, clip->clipId, panel.getSourceCombo().getText());
}

void EngineEditor::stashLooperThumbnails (int looperIndex, int trackIndex, int slotIndex)
{
    if (looperIndex < 0 || looperIndex >= looperPage.getLooperCount())
        return;

    auto& panel = looperPage.getPanel (looperIndex);

    const auto stashCell = [this] (LooperSlotCell& cell)
    {
        if (! cell.hasThumbnail())
            return;

        // Zellfläche = Quellfarbe der Aufnahme — die Zelle kennt nur die
        // Tinte; die Fläche rekonstruiert der Re-Apply aus dem Stash
        trashedLooperThumbnails[cell.getThumbnailClipId()] = {
            cell.getThumbnailImage(), cell.getThumbnailBackground(),
            cell.getThumbnailSourceLabel()
        };
    };

    const auto firstTrack = trackIndex >= 0 ? trackIndex : 0;
    const auto lastTrack  = trackIndex >= 0 ? trackIndex : panel.getTrackCount() - 1;

    for (int t = firstTrack; t <= lastTrack && t < panel.getTrackCount(); ++t)
    {
        auto& track = panel.getTrack (t);

        if (slotIndex >= 0)
        {
            if (slotIndex < track.getVisibleSlots())
                stashCell (track.getSlotCell (slotIndex));
        }
        else
        {
            for (int s = 0; s < track.getVisibleSlots(); ++s)
                stashCell (track.getSlotCell (s));
        }
    }
}

void EngineEditor::purgeLooperThumbnails()
{
    auto& session = engine.getLooperSession();
    auto& trash = engine.getLooperTrash();

    const auto clipIsAlive = [&] (juce::uint32 clipId)
    {
        for (const auto& entry : trash.getEntries())
            for (const auto& ref : entry.clips)
                if (ref.clipId == clipId)
                    return true;

        // Auch UNSICHTBARE Slots zählen (Restore in Slot > visibleSlots)
        for (int l = 0; l < session.getNumLoopers(); ++l)
            for (int t = 0; t < session.getNumTracks (l); ++t)
                for (int s = 0; s < LooperSessionModel::maxSlots; ++s)
                    if (auto* clip = session.clipAt (l, t, s); clip != nullptr
                        && clip->clipId == clipId)
                        return true;

        return false;
    };

    for (auto it = trashedLooperThumbnails.begin(); it != trashedLooperThumbnails.end();)
    {
        if (clipIsAlive (it->first))
            ++it;
        else
            it = trashedLooperThumbnails.erase (it);
    }
}

juce::uint32 EngineEditor::looperOutChannelRgb (const juce::String& nodeUuid, int channel)
{
    const auto node = rootState.getChildWithName (id::nodes)
                          .getChildWithProperty (id::nodeId, nodeUuid);
    if (! node.isValid())
        return 0;

    const auto specs = LooperPatchOutModule::readOutputConfig (node);
    const auto slot = channel / LooperPatchOutModule::slotWidth;
    if (! juce::isPositiveAndBelow (slot, (int) specs.size()))
        return 0;

    using Kind = LooperPatchOutModule::Kind;
    const auto& spec = specs[(size_t) slot];
    auto& session = engine.getLooperSession();
    auto& settings = engine.getLooperSettings();

    std::vector<juce::uint32> mix;
    const auto collectLooper = [&] (int l)
    {
        for (int t = 0; t < session.getNumTracks (l); ++t)
            if (looperTrackAudible (l, t))
                if (const auto rgb = looperTrackRgb (l, t); rgb != 0)
                    mix.push_back (rgb);
    };

    switch (spec.kind)
    {
        case Kind::track:
            return looperTrackRgb (spec.looper - 1, spec.track - 1);

        case Kind::bus:
            collectLooper (spec.looper - 1);
            break;

        case Kind::send:
            // „aktuell summiert" = spielende Tracks mit gesetztem Send-Bit
            for (int l = 0; l < session.getNumLoopers(); ++l)
                for (int t = 0; t < session.getNumTracks (l); ++t)
                    if (session.getPlayingSlot (l, t) >= 0
                        && (settings.getTrackSends (l, t) & (1 << (spec.send - 1))) != 0)
                        if (const auto rgb = looperTrackRgb (l, t); rgb != 0)
                            mix.push_back (rgb);
            break;

        case Kind::master:
            for (int l = 0; l < session.getNumLoopers(); ++l)
                if (settings.isSendToMaster (l))
                    collectLooper (l);
            break;
    }

    return flow_colours::blendRgb (mix);
}

juce::uint32 EngineEditor::looperTrackRgb (int looperIndex, int trackIndex)
{
    auto& session = engine.getLooperSession();
    if (looperIndex < 0 || looperIndex >= session.getNumLoopers()
        || trackIndex < 0 || trackIndex >= session.getNumTracks (looperIndex))
        return 0;

    const auto clipRgb = [] (LooperClip* clip) -> juce::uint32
    {
        return clip != nullptr ? clip->sourceRgb.load (std::memory_order_relaxed) : 0;
    };

    // Spielender Clip zuerst (Skizze: L1·T2 ist cyan, weil DESSEN Clip von
    // attenuator_2 kam), sonst erster belegter Slot, sonst Looper-Quelle
    if (const auto playing = session.getPlayingSlot (looperIndex, trackIndex); playing >= 0)
        if (const auto rgb = clipRgb (session.clipAt (looperIndex, trackIndex, playing));
            rgb != 0)
            return rgb;

    for (int s = 0; s < LooperSessionModel::maxSlots; ++s)
        if (const auto rgb = clipRgb (session.clipAt (looperIndex, trackIndex, s)); rgb != 0)
            return rgb;

    const auto source = looperSourceColour (
        engine.getLooperSettings().getSourceKey (looperIndex));
    return source.isTransparent() ? 0u : (source.getARGB() & 0x00ffffffu);
}

bool EngineEditor::looperTrackAudible (int looperIndex, int trackIndex)
{
    auto& session = engine.getLooperSession();
    auto& settings = engine.getLooperSettings();

    if (session.getPlayingSlot (looperIndex, trackIndex) < 0
        || settings.isTrackMuted (looperIndex, trackIndex))
        return false;

    // Solo-Filter — Näherung der Bank-Audibility im jeweiligen Scope
    const auto globalScope = settings.getSoloScope()
                          == LooperSettings::SoloScope::globalScope;
    bool anySolo = false;
    for (int l = 0; l < session.getNumLoopers() && ! anySolo; ++l)
    {
        if (! globalScope && l != looperIndex)
            continue;
        for (int t = 0; t < session.getNumTracks (l); ++t)
            if (settings.isTrackSolo (l, t))
            {
                anySolo = true;
                break;
            }
    }

    return ! anySolo || settings.isTrackSolo (looperIndex, trackIndex);
}

void EngineEditor::refreshLooperFlowColoursIfChanged()
{
    auto& session = engine.getLooperSession();
    auto& settings = engine.getLooperSettings();

    // FNV-1a über den farbrelevanten Zustand — billig (≤ 16 Tracks); der
    // Spiel-Zustand lebt in der Engine, es gibt kein Tree-Event dafür
    juce::uint64 hash = 1469598103934665603ULL;
    const auto mixIn = [&hash] (juce::uint64 value)
    {
        hash = (hash ^ value) * 1099511628211ULL;
    };

    mixIn ((juce::uint64) settings.getSoloScope());
    for (int l = 0; l < session.getNumLoopers(); ++l)
    {
        mixIn ((juce::uint64) settings.isSendToMaster (l));

        // Header-Streifen folgt der gewählten Quelle (Quellwechsel = Refresh)
        const auto headerColour = looperSourceColour (settings.getSourceKey (l));
        mixIn (headerColour.isTransparent() ? 0u
                                            : (headerColour.getARGB() & 0x00ffffffu));
        for (int t = 0; t < session.getNumTracks (l); ++t)
        {
            mixIn ((juce::uint64) (session.getPlayingSlot (l, t) + 1));
            mixIn (looperTrackRgb (l, t));
            mixIn ((juce::uint64) settings.isTrackMuted (l, t));
            mixIn ((juce::uint64) settings.isTrackSolo (l, t));
            mixIn ((juce::uint64) settings.getTrackSends (l, t));
        }
    }

    if (hash != looperColourHash)
    {
        looperColourHash = hash;
        canvas.refreshSignalColours();
    }
}

void EngineEditor::restoreLooperTrashFromUi (std::uint32_t entryId)
{
    int skipped = 0;
    if (const auto result = engine.restoreLooperTrashEntry (entryId, &skipped);
        result.failed())
    {
        captureToast.show (result.getErrorMessage());
        return;
    }

    if (skipped > 0)
        captureToast.show (juce::String (skipped)
                           + juce::String::fromUTF8 (" Kabel nicht wiederherstellbar"));
    refreshLooperStructure();
}

void EngineEditor::refreshLooperStatus (bool devMode)
{
    auto& bank = engine.getLooperBank();
    auto& session = engine.getLooperSession();

    // Retire-Quittungen einsammeln (Clip-Freigabe, LooperBank-Doku)
    bank.serviceMessageThread();

    const auto playing = bank.isPlaying();
    transportBar.setLooperStatus (pageHost.getPage() == TransportBar::pageLooper,
                                  playing);
    looperPage.getStopAllTile().setEnabled (playing);

    // Papierkorb-Kachel (Big Out): Countdown + Sichtbarkeit
    auto& trash = engine.getLooperTrash();
    looperPage.setTrashState (trash.secondsRemaining(), trash.hasEntries());

    // Puls-Phase + Abspielposition tickt der VBlank-Pfad monitor-synchron
    // (tickLooperPlayheads) — hier nur Struktur, Labels, Meter

    for (int l = 0; l < looperPage.getLooperCount(); ++l)
    {
        auto& panel = looperPage.getPanel (l);
        bool anyAudible = false;

        for (int t = 0; t < panel.getTrackCount(); ++t)
        {
            auto& track = panel.getTrack (t);
            const auto& meter = bank.getTrackMeter (l, t);
            const auto rmsLeft = meter.getRms (0);
            const auto rmsRight = meter.getRms (1);
            const auto audible = juce::jmax (rmsLeft, rmsRight) > 1.0e-3f;
            anyAudible = anyAudible || audible;
            track.setMeter (rmsLeft, rmsRight,
                            meter.getPeak (0), meter.getPeak (1), audible);

            const auto playingSlot = session.getPlayingSlot (l, t);
            const auto target = session.getTarget (l);
            const auto active = session.getActiveSlot (l);

            float playingProgress = 0.0f;
            double playingLength = 0.0;

            for (int s = 0; s < track.getVisibleSlots(); ++s)
            {
                auto& cell = track.getSlotCell (s);

                LooperSlotCell::State state;
                if (auto* clip = session.clipAt (l, t, s))
                {
                    state.hasClip = true;
                    state.playing = s == playingSlot;
                    state.reversed = clip->stagedReversed.load (std::memory_order_relaxed);
                    state.label = "Clip " + juce::String (clip->clipId) + juce::String::fromUTF8 (" · ")
                                + juce::String (clip->commitBars)
                                + (clip->commitBars == 1 ? " Bar" : " Bars");

                    const auto rate = clip->stagedRate.load (std::memory_order_relaxed);
                    if (std::abs (rate - 1.0) > 1.0e-3)
                        state.rateBadge = juce::String (rate, 2) + juce::String::fromUTF8 ("×");

                    if (state.playing)
                    {
                        state.progress01 = clip->displayPhase01.load (std::memory_order_relaxed);
                        playingProgress = state.progress01;
                        playingLength = clip->stagedLengthBeats.load (std::memory_order_relaxed);
                    }

                    // Thumbnail gehört zu GENAU diesem Clip — nach
                    // Überschreib-Commit/Neuaufbau räumt der Timer auf
                    if (cell.hasThumbnail() && cell.getThumbnailClipId() != clip->clipId)
                        cell.clearThumbnail();

                    // Papierkorb-Restore: geparktes Thumbnail zurückspielen,
                    // sobald der Clip wieder in einer sichtbaren Zelle liegt
                    if (! cell.hasThumbnail())
                    {
                        if (auto it = trashedLooperThumbnails.find (clip->clipId);
                            it != trashedLooperThumbnails.end())
                        {
                            cell.setThumbnail (it->second.ink, it->second.background,
                                               clip->clipId, it->second.sourceLabel);
                            trashedLooperThumbnails.erase (it);
                        }
                    }
                }
                else
                {
                    state.target = target.track == t && target.slot == s;
                    cell.clearThumbnail();   // no-op ohne Thumbnail
                }

                state.active = active.track == t && active.slot == s && state.hasClip;
                cell.setState (state);
            }

            // Takt-Anzeige: "aktueller Takt / Länge" des spielenden Clips
            if (playingSlot >= 0 && playingLength > 0.0)
            {
                const auto totalBars = juce::jmax (1, (int) std::lround (playingLength / 4.0));
                const auto currentBar = juce::jlimit (1, totalBars,
                                                      1 + (int) std::floor (playingProgress
                                                                            * (float) totalBars));
                track.setBarDisplay (currentBar, totalBars, playingProgress);
            }
            else
            {
                track.setBarDisplay (0, 0, 0.0f);
            }
        }

        panel.setAudible (anyAudible);

        // Clip-Controls folgen dem Aktiv-Clip
        auto& controls = panel.getControls();
        if (auto* activeClip = session.getActiveClip (l))
        {
            controls.setClipControlsEnabled (true);
            if (! controls.getVariKnob().isMouseButtonDown())
                controls.setRate (activeClip->stagedRate.load (std::memory_order_relaxed));
            controls.setReversed (activeClip->stagedReversed.load (std::memory_order_relaxed));
            controls.setActiveLabel ("Clip " + juce::String (activeClip->clipId)
                                     + juce::String::fromUTF8 (" · ")
                                     + juce::String (activeClip->commitBars)
                                     + (activeClip->commitBars == 1 ? " Bar" : " Bars"));
        }
        else
        {
            controls.setClipControlsEnabled (false);
            controls.setActiveLabel ({});
        }
    }

    // Statuszeile (Dev-Modus ergänzt Re-Syncs + RAM-Konto der Clips)
    if (playing)
    {
        auto status = juce::String ("spielt");
        if (const auto snaps = bank.getSnapCount(); devMode && snaps > 0)
            status << juce::String::fromUTF8 (" · ") << juce::String (snaps)
                   << (snaps == 1 ? " Re-Sync" : " Re-Syncs");
        if (devMode)
            status << juce::String::fromUTF8 (" · RAM ")
                   << juce::String (bank.getRamBytesUsed() / 1'000'000) << " MB";
        looperPage.setStatus (status);
    }
    else
    {
        auto status = juce::String::fromUTF8 (
            "bereit — Target wählen, Segment-Klick committet die letzten 8/4/2/1 Takte");
        if (devMode && bank.getRamBytesUsed() > 0)
            status << juce::String::fromUTF8 (" · RAM ")
                   << juce::String (bank.getRamBytesUsed() / 1'000'000) << " MB";
        looperPage.setStatus (status);
    }

    // Patch-OUT-Slot-Farben folgen dem Spiel-/Mix-Zustand (kein Tree-Event)
    refreshLooperFlowColoursIfChanged();
}

void EngineEditor::tickLooperPlayheads()
{
    // Nur wenn die Looper-Page wirklich zu sehen ist — sonst kostenlos
    if (pageHost.getPage() != TransportBar::pageLooper)
        return;

    // Target-Puls (1 Hz) monitor-synchron statt in 15-Hz-Stufen
    const auto pulse = (float) (juce::Time::getMillisecondCounter() % 1000u) / 1000.0f;
    looperPage.setPulsePhase (pulse);

    auto& session = engine.getLooperSession();

    for (int l = 0; l < looperPage.getLooperCount(); ++l)
    {
        auto& panel = looperPage.getPanel (l);

        for (int t = 0; t < panel.getTrackCount(); ++t)
        {
            auto& track = panel.getTrack (t);

            const auto playingSlot = session.getPlayingSlot (l, t);
            if (playingSlot < 0 || playingSlot >= track.getVisibleSlots())
                continue;

            auto* clip = session.clipAt (l, t, playingSlot);
            if (clip == nullptr)
                continue;

            // Dieselben Atomics wie der Timer — nur eben pro Frame
            const auto progress = clip->displayPhase01.load (std::memory_order_relaxed);
            track.getSlotCell (playingSlot).setProgress (progress);

            const auto lengthBeats = clip->stagedLengthBeats.load (std::memory_order_relaxed);
            if (lengthBeats > 0.0)
            {
                const auto totalBars = juce::jmax (1, (int) std::lround (lengthBeats / 4.0));
                const auto currentBar = juce::jlimit (1, totalBars,
                                                      1 + (int) std::floor (progress
                                                                            * (float) totalBars));
                track.setBarDisplay (currentBar, totalBars, progress);
            }
        }
    }
}

juce::StringArray EngineEditor::buildOutputPairNames()
{
    auto nodesTree = rootState.getChildWithName (id::nodes);
    auto outNode = nodesTree.getChildWithProperty (id::factoryId, juce::String (audioOutputModuleId));
    if (! outNode.isValid())
        outNode = nodesTree.getChildWithProperty (id::moduleId, juce::String (audioOutputModuleId));

    const auto channels = outNode.isValid()
                        ? (int) outNode.getProperty (id::numInputChannels, 2) : 2;

    juce::StringArray names;
    auto& labels = engine.getChannelNames();

    for (int channel = 0; channel + 1 < channels; channel += 2)
        names.add (labels.getLabel (ChannelNames::Direction::output, channel)
                   + " / " + labels.getLabel (ChannelNames::Direction::output, channel + 1));

    if (names.isEmpty())
        names.add ("Kanal 1 / 2");

    return names;
}

//==============================================================================
void EngineEditor::selectPage (int pageIndex)
{
    pageHost.setPage (pageIndex);
    browserContext.setActivePage (pageIndex);

    // M5b: Dock-Tabs folgen der Page (Page-Masken); ein Page-Wechsel kann
    // den aktiven Tab umschalten (onActiveTabChanged -> refreshDockModes)
    // und die bevorzugte Breite ändern (keine sichtbaren Tabs = 0).
    editorDock.setActivePage (pageIndex);
    resized();

    // M7: Save/Delete-Kacheln nur im Looper-Kontext; beim Verlassen der
    // Page verfallen laufende Gesten (auch der Delete-Latch)
    const auto looperOpen = pageIndex == TransportBar::pageLooper;
    transportBar.setLooperPageContext (looperOpen);
    if (! looperOpen)
    {
        looperGesture = LooperGesture::none;
        looperDeleteLatched = false;
        transportBar.getLooperDeleteTile().setActive (false);
        transportBar.getSaveTile().setActive (false);
    }
}

void EngineEditor::toggleBrowserPanel()
{
    browserPanel.setOpen (! browserPanel.isOpen());
    transportBar.setBrowserPanelOpen (browserPanel.isOpen());
}

void EngineEditor::toggleEditorPanel()
{
    gridPage.setDockPanelOpen (! gridPage.isDockPanelOpen());
    transportBar.setEditorPanelOpen (gridPage.isDockPanelOpen());
}

//==============================================================================
void EngineEditor::launchPresetChooser (bool saving)
{
    const auto defaultDirectory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                      .getChildFile ("Conduit");
    defaultDirectory.createDirectory();

    // Async — Modal-Loops sind projektweit abgeschaltet (13.2)
    presetChooser = std::make_unique<juce::FileChooser> (
        saving ? "Preset speichern" : "Preset laden",
        defaultDirectory,
        "*" + juce::String (EngineProcessor::presetFileExtension));

    // 'chooserFlags' — 'flags' würde juce::Component::flags verschatten (C4458)
    const auto chooserFlags = saving
        ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
              | juce::FileBrowserComponent::warnAboutOverwriting
        : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    presetChooser->launchAsync (chooserFlags, [this, saving] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();

        if (file == juce::File())
            return;  // abgebrochen

        if (saving)
            file = file.withFileExtension (EngineProcessor::presetFileExtension);

        const auto result = saving ? engine.savePreset (file)
                                   : engine.loadPreset (file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    saving ? "Preset speichern" : "Preset laden",
                                                    result.getErrorMessage());
            return;
        }

        if (saving)
            browserModel.refreshFiles();   // PROJEKTE-Liste kennt den neuen Stand
    });
}

//==============================================================================
void EngineEditor::handleExportReport (const CaptureWriter::Report& report)
{
    const auto total = report.numSucceeded + report.numFailed;

    // Pfeil als escaped UTF-8 — MSVC liest BOM-lose Quellen als CP1252
    const auto arrow = juce::String::fromUTF8 (" \xe2\x86\x92 ");
    if (report.numFailed == 0)
        captureToast.show (juce::String (report.numSucceeded)
                           + (report.numSucceeded == 1 ? " Spur" : " Spuren") + arrow
                           + report.directory.getFullPathName());
    else
        captureToast.show (juce::String (report.numSucceeded) + " von "
                           + juce::String (total) + " Spuren exportiert ("
                           + juce::String (report.numFailed) + " fehlgeschlagen)");

    // "Nach Export freigeben": NIE ohne Rückfrage (User-Vorgabe) — nur
    // erfolgreich exportierte Kanäle, die noch im Zustand held sind
    if (! engine.getCaptureSettings().getReleaseAfterExport())
        return;

    std::vector<int> releasable;
    for (const auto& task : report.tasks)
        if (task.success && task.channelIndex >= 0)
            if (const auto* channel = engine.getCaptureService().getChannel (task.channelIndex))
                if (channel->getState() == CaptureChannel::State::held)
                    releasable.push_back (task.channelIndex);

    if (releasable.empty())
        return;

    auto* enginePtr = &engine;  // Processor überlebt den Editor
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::QuestionIcon, "RAM-Puffer freigeben?",
        juce::String (static_cast<int> (releasable.size()))
            + " exportierte Spur(en) sind weiterhin im RAM gehalten. Jetzt freigeben?",
        "Freigeben", "Behalten", this,
        juce::ModalCallbackFunction::create ([enginePtr, releasable] (int result)
        {
            if (result == 1)
                enginePtr->getCaptureService().releaseExportedHeldChannels (releasable);
        }));
}

//==============================================================================
void EngineEditor::timerCallback()
{
    transportBar.refresh();  // Tempo + Peer-Zahl (Repaint nur bei Änderung)

    // Capture-LED an der ⛶-Kachel; Panel-Zeilen nur wenn aufgeklappt
    const auto status = engine.getCaptureService().getUiStatus();
    transportBar.setCaptureStatus (status.anyRecording, status.anyHeld, status.exporting);

    if (capturePanel.isVisible())
        capturePanel.refresh();

    // Looper-Re-Syncs nur im Dev-Modus; das DSP-Meter hat einen EIGENEN
    // Settings-Schalter (User-Entscheidung 04.07.2026)
    const auto devMode = engine.getUiSettings().isDevModeEnabled();

    // Looper-Status (M6): Bank-Service, Tape-LED, Panels, Slots, Meter
    refreshLooperStatus (devMode);

    // Callback-Timing (Settings-Schalter „DSP-Meter"): Durchschnitt wie
    // Abletons CPU-Meter, Peak als XRun-Frühwarner (schlimmster Block des
    // UI-Intervalls), XRuns seit Geräte-Start
    if (engine.getUiSettings().isDspMeterEnabled())
    {
        auto& timing = engine.getTimingMonitor();
        const auto avgPercent  = (timing.consumeAverageLoadPermille() + 5u) / 10u;
        const auto peakPercent = (timing.consumePeakLoadPermille() + 5u) / 10u;
        const auto xruns = timing.getXrunCount();

        transportBar.setDspMeterText ("DSP " + juce::String (avgPercent)
                                       + juce::String::fromUTF8 (" % ⌀ / ")
                                       + juce::String (peakPercent)
                                       + juce::String::fromUTF8 (" % pk · ")
                                       + juce::String (xruns)
                                       + (xruns == 1 ? " XRun" : " XRuns"));
    }
    else
    {
        transportBar.setDspMeterText ({});
    }

    // audioSetupWarning folgt dem Controller (setzt/löscht bei Gerätewechsel)
    const auto warning = rootState.getProperty (id::audioSetupWarning).toString();
    transportBar.setWarningText (warning.isNotEmpty() ? "Audio-Setup: " + warning
                                                      : juce::String());
}

//==============================================================================
void EngineEditor::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);
}

void EngineEditor::resized()
{
    auto bounds = getLocalBounds();
    transportBar.setBounds (bounds.removeFromTop (TransportBar::preferredHeight));

    if (capturePanel.isVisible())
        capturePanel.setBounds (bounds.removeFromTop (CapturePanel::preferredHeight));

    // Browser-Dock rechts (animierte Breite); Clamp hält den Canvas
    // auch in schmalen Fenstern nutzbar
    const auto browserWidth = juce::jmin (browserPanel.currentDockWidth(),
                                          getWidth() / 3);
    if (browserWidth > 0)
        browserPanel.setBounds (bounds.removeFromRight (browserWidth));

    // Editor-Dock rechts daneben (M5b: app-weit, ehemals GridPage-intern) --
    // 0 wenn geschlossen oder auf dieser Page kein Tab sichtbar (dann
    // Bounds leeren, sonst bliebe das offene Panel mit alten Bounds über
    // dem PageHost liegen).
    if (editorDock.getPreferredWidth() > 0)
        editorDock.setBounds (bounds.removeFromRight (editorDock.getPreferredWidth()));
    else
        editorDock.setBounds (juce::Rectangle<int>());

    pageHost.setBounds (bounds);

    // Toast: unten mittig über dem Canvas
    captureToast.setBounds (getLocalBounds()
                                .withSizeKeepingCentre (460, 44)
                                .withY (getHeight() - 60));
}

bool EngineEditor::keyPressed (const juce::KeyPress& key)
{
    const auto modifier = juce::ModifierKeys::commandModifier;

    // Seiten-Navigation der Node-Page (ADR 008 M3b, Tastatur-Parität):
    // Ctrl+Alt+Pfeile wechseln im Seiten-Grid — ins Leere legt eine neue
    // Seite an (wie der 4-Finger-Wisch). Nur auf der Device-Page.
    if (key.getModifiers().isCommandDown() && key.getModifiers().isAltDown()
        && pageHost.getPage() == TransportBar::pageDevice)
    {
        if (key.getKeyCode() == juce::KeyPress::leftKey)  { canvas.navigatePages (-1, 0); return true; }
        if (key.getKeyCode() == juce::KeyPress::rightKey) { canvas.navigatePages (1, 0);  return true; }
        if (key.getKeyCode() == juce::KeyPress::upKey)    { canvas.navigatePages (0, -1); return true; }
        if (key.getKeyCode() == juce::KeyPress::downKey)  { canvas.navigatePages (0, 1);  return true; }

        // M4: Birdeye-Toggle (B) + Seiten-Übersicht (O) — Tastatur-Parität
        // der Ebenen 3/5 (Trackpad: OS konsumiert 3/4/5-Finger, ADR 008)
        if (key.getKeyCode() == 'B') { canvas.toggleBirdeye();      return true; }
        if (key.getKeyCode() == 'O') { canvas.togglePageOverview(); return true; }
    }

    if (key == juce::KeyPress ('z', modifier, 0))
        return undoManager.undo();

    if (key == juce::KeyPress ('y', modifier, 0)
        || key == juce::KeyPress ('z', modifier | juce::ModifierKeys::shiftModifier, 0))
        return undoManager.redo();

    if (key == juce::KeyPress (juce::KeyPress::F11Key))
    {
        // Kiosk-Mode statt setFullScreen: randlos, ohne native Titelleiste
        // und ohne Taskbar (allowMenusAndBars = false) — nur Conduit sichtbar.
        auto& desktop = juce::Desktop::getInstance();
        if (desktop.getKioskModeComponent() == nullptr)
            desktop.setKioskModeComponent (getTopLevelComponent(), false);
        else
            desktop.setKioskModeComponent (nullptr, false);
        return true;
    }

    return false;
}

} // namespace conduit

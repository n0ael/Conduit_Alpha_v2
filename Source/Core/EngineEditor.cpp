#include "EngineEditor.h"

#include <algorithm>

#include "Browser/BrowserPaths.h"
#include "EngineProcessor.h"
#include "Modules/LinkAudioReceiveModule.h"
#include "Modules/LinkAudioSendModule.h"
#include "UI/LinkSendCreateDialog.h"
#include "Core/Looper/LooperClipExporter.h"
#include "UI/LooperSettingsMenu.h"
#include "UI/SettingsWindow.h"
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
              &engineProcessor.getInputLinkSend(), &engineProcessor.getUiSettings()),
      gridPage (engineProcessor.getGridVoiceEngine(), engineProcessor.getGridMidiDeviceTarget(),
               engineProcessor.getGridPanelSettings(), engineProcessor.getUiSettings()),
      browserModel (engineProcessor.getModuleFactory(), browserContext, browserWorker),
      browserPanel (browserModel, engineProcessor.getUiSettings())
{
    // Push-3-Design app-weit: Jost + dunkle Kacheln auch in PopupMenus,
    // Dialogen und dem Settings-Fenster (CLAUDE.md 10)
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

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
                                                      engine.getUiSettings()));
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
    { selectPage (pageIndex); };

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

    // Grid-Editor-Dock-Panel (S2): eigener Toggle, unabhängig vom Browser;
    // LED-Zustand sofort mit der geladenen Persistenz synchronisieren
    // (das Panel kann bereits offen starten, GridPanelSettings::isEditorPanelOpen)
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

    // Looper schließen: enthält er Clips, fragt ein async Dialog nach
    // (Übergabe §10.7; kein Modal-Loop — JUCE_MODAL_LOOPS_PERMITTED=0)
    looperPage.onRemoveLooper = [this]
    {
        auto& session = engine.getLooperSession();
        const auto last = session.getNumLoopers() - 1;
        if (last < 1)
            return;

        const auto removeIt = [this]
        {
            auto& looperSession = engine.getLooperSession();
            if (const auto result = looperSession.removeLastLooper(); result.failed())
            {
                captureToast.show (result.getErrorMessage());
                return;
            }

            auto& settings = engine.getLooperSettings();
            settings.setNumLoopers (settings.getNumLoopers() - 1);
            refreshLooperStructure();
        };

        if (! session.looperHasClips (last))
        {
            removeIt();
            return;
        }

        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            juce::String::fromUTF8 ("Looper schließen"),
            juce::String::fromUTF8 ("Looper ") + juce::String (last + 1)
                + juce::String::fromUTF8 (" enthält Clips — schließen und verwerfen?"),
            juce::String::fromUTF8 ("Schließen"), "Abbrechen", this,
            juce::ModalCallbackFunction::create ([removeIt] (int result)
            {
                if (result != 0)
                    removeIt();
            }));
    };

    looperPage.onOpenSettings = [this]
    {
        auto menu = std::make_unique<LooperSettingsMenu> (engine.getLooperSettings());
        juce::CallOutBox::launchAsynchronously (
            std::move (menu),
            looperPage.getSettingsTile().getScreenBounds(), nullptr);
    };

    looperPage.onStop = [this] { engine.stopLooper(); };

    // Spectrum global: schaltet die Strips ALLER Looper (Settings pro
    // Looper, Kachel wirkt auf alle — Mock-Semantik)
    looperPage.onViewToggled = [this] (bool spectrum)
    {
        auto& settings = engine.getLooperSettings();
        for (int l = 0; l < LooperSettings::maxLoopers; ++l)
            settings.setSpectrumView (l, spectrum);
        looperPage.setSpectrumView (spectrum);
    };

    // Metronom-Ziel-Paare fürs Link-Menü: Labels aus den ChannelNames,
    // Kanalzahl aus dem audio_out-Tree-Node (folgt der Hardware)
    transportBar.metronomeTargetNames = [this] { return buildOutputPairNames(); };

    // Ausgabe-Paar des Loop-Playbacks (global, B6): Auswahl persistiert
    // looperAnchor und routet die Bank sofort um
    looperPage.onOutputPairSelected = [this] (int pairIndex)
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

    // Default-LookAndFeel VOR der Member-Destruktion zurücksetzen
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
void EngineEditor::valueTreePropertyChanged (juce::ValueTree& tree,
                                             const juce::Identifier& property)
{
    juce::ignoreUnused (tree);

    // Link-Kanal-Wahl (Receive-Panel), Node-Farben und I/O-Kanalzahlen
    // ändern Labels/Farben der Looper-Quellauswahl — Tap-Registrierung
    // und Rename decken die CaptureService-Broadcasts bereits ab
    if (property == id::targetPeer || property == id::targetChannel
        || property == id::nodeColour
        || property == id::numOutputChannels || property == id::numInputChannels)
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
        devPanel = std::make_unique<DevPanel> (engine.getUiSettings());
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
    std::vector<LooperPanel::Source> sources;

    // Master zuerst — die Session-Summe (Master-Output-Tap, B2) ist die
    // naheliegendste Looper-Quelle und der Fallback unbekannter Schlüssel
    sources.push_back ({ "master", "Master", juce::Colour(), false });

    // Hardware-Eingangs-Paare: Kanalzahl aus dem audio_in-Tree-Node
    // (folgt der Hardware), Labels aus den ChannelNames — Muster
    // metronomeTargetNames
    auto nodesTree = rootState.getChildWithName (id::nodes);
    auto inNode = nodesTree.getChildWithProperty (id::factoryId, juce::String (audioInputModuleId));
    if (! inNode.isValid())
        inNode = nodesTree.getChildWithProperty (id::moduleId, juce::String (audioInputModuleId));

    const auto channels = inNode.isValid()
                        ? (int) inNode.getProperty (id::numOutputChannels, 2) : 2;
    auto& labels = engine.getChannelNames();

    for (int channel = 0; channel + 1 < channels; channel += 2)
    {
        const auto key = "hw:" + juce::String (channel / 2);
        sources.push_back ({ key,
                             labels.getLabel (ChannelNames::Direction::input, channel)
                                 + " / "
                                 + labels.getLabel (ChannelNames::Direction::input, channel + 1),
                             looperSourceColour (key), false });
    }

    // Ausgangs-Paare hinter dem Master (Kanäle 2p/2p+1, "out:{p}"):
    // damit sind auch Signale loopbar, die nur auf einem Ausgangspaar
    // liegen — z. B. ein Link-Receive-Routing (User-Wunsch 08.07.2026)
    auto outNode = nodesTree.getChildWithProperty (id::factoryId, juce::String (audioOutputModuleId));
    if (! outNode.isValid())
        outNode = nodesTree.getChildWithProperty (id::moduleId, juce::String (audioOutputModuleId));

    const auto outChannels = outNode.isValid()
                           ? (int) outNode.getProperty (id::numInputChannels, 2) : 2;

    for (int channel = 2; channel + 1 < outChannels; channel += 2)
    {
        const auto key = "out:" + juce::String (channel / 2);
        sources.push_back ({ key,
                             "Out: " + labels.getLabel (ChannelNames::Direction::output, channel)
                                 + " / "
                                 + labels.getLabel (ChannelNames::Direction::output, channel + 1),
                             looperSourceColour (key), false });
    }

    // Capture-Taps der Module (virtuelle Slots hinter master_l/_r):
    // _l/_r-Stereo-Paare auf den Basisnamen reduziert, Mono-Taps einzeln.
    // Link-Receive-Taps wandern in eine eigene Gruppe hinter den lokalen
    // Quellen — Label "{peer} / {kanal}" wie im Receive-Panel, getrennt
    // per Separator, ein Abschnitt pro Peer/App (User-Wunsch 09.07.2026)
    struct LinkEntry
    {
        juce::String peer;
        LooperPanel::Source source;
    };
    std::vector<LinkEntry> linkEntries;

    juce::StringArray seenBaseNames, seenLinkLabels;
    const auto& capture = engine.getCaptureService();

    for (int slot = 2; slot < CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
    {
        const auto info = capture.getVirtualChannelUiInfo (slot);
        if (! info.inUse || info.name.isEmpty())
            continue;

        auto baseName = info.name;
        if (baseName.endsWith ("_l") || baseName.endsWith ("_r"))
            baseName = baseName.dropLastCharacters (2);

        // Ausgangs-Taps (out{p}_l/_r) sind oben schon als "Out:"-Paare drin
        if (baseName.length() > 3 && baseName.startsWith ("out")
            && baseName.substring (3).containsOnly ("0123456789"))
            continue;

        if (seenBaseNames.contains (baseName))
            continue;

        seenBaseNames.add (baseName);
        const auto key = "tap:" + baseName;

        const auto node = nodesTree.getChildWithProperty (id::moduleId, baseName);
        const auto isLinkReceive = node.isValid()
            && node.getProperty (id::factoryId).toString()
                   == LinkAudioReceiveModule::staticModuleId;

        if (! isLinkReceive)
        {
            sources.push_back ({ key, "Tap: " + baseName,
                                 looperSourceColour (key), false });
            continue;
        }

        const auto peer    = node.getProperty (id::targetPeer).toString();
        const auto channel = node.getProperty (id::targetChannel).toString();

        // Ohne Kanal-Wunsch bleibt der Modulname ("Link:"-Präfix statt
        // "Tap:" — der Eintrag gehört sichtbar zur Link-Gruppe)
        auto label = channel.isNotEmpty()
                   ? (peer.isNotEmpty() ? peer + " / " + channel : channel)
                   : "Link: " + baseName;

        // Zwei Module auf demselben Peer-Kanal: Modulname disambiguiert
        if (seenLinkLabels.contains (label))
            label << " (" << baseName << ")";
        seenLinkLabels.add (label);

        linkEntries.push_back ({ peer, { key, label, looperSourceColour (key), false } });
    }

    // Link-Gruppe: nach Peer sortiert (ungebundene ans Ende), Separator
    // vor dem Block und an jedem Peer-Wechsel
    std::stable_sort (linkEntries.begin(), linkEntries.end(),
                      [] (const LinkEntry& a, const LinkEntry& b)
    {
        if (a.peer.isEmpty() != b.peer.isEmpty())
            return b.peer.isEmpty();
        return a.peer.compareIgnoreCase (b.peer) < 0;
    });

    juce::String previousPeer;
    for (std::size_t i = 0; i < linkEntries.size(); ++i)
    {
        auto& entry = linkEntries[i];
        entry.source.separatorBefore = i == 0 || entry.peer != previousPeer;
        previousPeer = entry.peer;
        sources.push_back (std::move (entry.source));
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

    if (sourceKey.startsWith ("out:"))
        return pairColour (Direction::output, sourceKey.substring (4).getIntValue() * 2);

    if (sourceKey.startsWith ("tap:"))
    {
        const auto node = rootState.getChildWithName (id::nodes)
                              .getChildWithProperty (id::moduleId, sourceKey.substring (4));
        if (node.isValid())
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
    looperPage.setOutputPairs (buildOutputPairNames(),
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
            track.setGain (settings.getTrackGain (l, t));
            track.setPan (settings.getTrackPan (l, t));
            track.setMute (settings.isTrackMuted (l, t));
            track.setSolo (settings.isTrackSolo (l, t));
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

    looperPage.setSpectrumView (settings.isSpectrumView (0));
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

    if (const auto result = session.removeLastTrack (looperIndex); result.failed())
    {
        captureToast.show (result.getErrorMessage());
        return;
    }

    engine.getLooperSettings().setNumTracks (looperIndex,
                                             session.getNumTracks (looperIndex));
    refreshLooperStructure();
}

void EngineEditor::handleLooperSlotTap (int looperIndex, int trackIndex, int slotIndex)
{
    auto& session = engine.getLooperSession();
    auto& settings = engine.getLooperSettings();

    // M7: Header-Gesten zuerst — Delete/Save halten (bzw. Latch) macht den
    // Slot-Tap zur Ziel-Auswahl der Geste, nie zum Launch
    if (looperGesture == LooperGesture::deleteClips)
    {
        if (const auto result = session.deleteSlot (looperIndex, trackIndex, slotIndex);
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

void EngineEditor::refreshLooperStatus (bool devMode)
{
    auto& bank = engine.getLooperBank();
    auto& session = engine.getLooperSession();

    // Retire-Quittungen einsammeln (Clip-Freigabe, LooperBank-Doku)
    bank.serviceMessageThread();

    const auto playing = bank.isPlaying();
    transportBar.setLooperStatus (pageHost.getPage() == TransportBar::pageLooper,
                                  playing);
    looperPage.getStopTile().setEnabled (playing);

    // Gemeinsame Puls-Phase der Target-Zellen (1-Hz-Puls)
    const auto pulse = (float) (juce::Time::getMillisecondCounter() % 1000u) / 1000.0f;
    looperPage.setPulsePhase (pulse);

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
            track.setMeter (rmsLeft, rmsRight, audible);

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

    pageHost.setBounds (bounds);

    // Toast: unten mittig über dem Canvas
    captureToast.setBounds (getLocalBounds()
                                .withSizeKeepingCentre (460, 44)
                                .withY (getHeight() - 60));
}

bool EngineEditor::keyPressed (const juce::KeyPress& key)
{
    const auto modifier = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('z', modifier, 0))
        return undoManager.undo();

    if (key == juce::KeyPress ('y', modifier, 0)
        || key == juce::KeyPress ('z', modifier | juce::ModifierKeys::shiftModifier, 0))
        return undoManager.redo();

    return false;
}

} // namespace conduit

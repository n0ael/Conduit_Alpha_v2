#include "EngineEditor.h"

#include "EngineProcessor.h"
#include "Modules/LinkAudioSendModule.h"
#include "UI/LinkSendCreateDialog.h"
#include "UI/SettingsWindow.h"

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
      browserModel (engineProcessor.getModuleFactory(), browserContext, browserWorker)
{
    // Push-3-Design app-weit: Jost + dunkle Kacheln auch in PopupMenus,
    // Dialogen und dem Settings-Fenster (CLAUDE.md 10)
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    // -- TransportBar-Hooks ---------------------------------------------------
    transportBar.onUndo = [this] { undoManager.undo(); };
    transportBar.onRedo = [this] { undoManager.redo(); };
    transportBar.onSave = [this] { launchPresetChooser (true); };

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

    // Interim bis M6: PROJEKTE-Zeile "Preset laden…" (Save bleibt auf der
    // Save-Kachel)
    browserPanel.onAction = [this] (const juce::String& actionId)
    {
        if (actionId == "load_preset")
            launchPresetChooser (false);
    };

    // Looper-Quellen (Master + Hardware-Paare + Taps): Auswahl armt den
    // Capture-Kanal und persistiert den Schlüssel (setLooperSource);
    // Tap-/Label-Änderungen bauen die Liste neu (ChangeListener unten)
    looperPage.onSourceSelected = [this] (const juce::String& sourceKey)
    { engine.setLooperSource (sourceKey); };
    rebuildLooperSources();

    // Waveform-Strip (B4): Bins vom Engine-Tap (Konsumentenrolle exklusiv
    // beim Strip), Beat-Achse von der LinkClock (inkl. Clock-Offset —
    // dieselbe Basis wie die Audio-Seite)
    looperPage.getStrip().setDataSource (&engine.getLooperWaveformTap());
    looperPage.getStrip().getBeatNow = [this] { return linkClock.getBeatPosition(); };

    // Segment-Klick = Commit der letzten N Takte (B5): spielt sofort
    // phasenstarr; Fehlerfälle (zu wenig Historie, > 60 s, keine Quelle)
    // kommen als Toast
    looperPage.getStrip().onSegmentClicked = [this] (int bars)
    {
        const auto result = engine.commitLooper (bars);
        if (result.failed())
            captureToast.show (result.getErrorMessage());
    };

    looperPage.onStop = [this] { engine.stopLooper(); };

    // Spektrum-View (S2): persistierter Zustand steuert Strip + Kachel-LED;
    // der Klick schaltet die Page selbst um, hier nur Persistenz
    looperPage.setSpectrumView (engine.getTransportSettings().isLooperSpectrumEnabled());
    looperPage.onViewToggled = [this] (bool spectrum)
    { engine.getTransportSettings().setLooperSpectrumEnabled (spectrum); };

    // Metronom-Ziel-Paare fürs Link-Menü: Labels aus den ChannelNames,
    // Kanalzahl aus dem audio_out-Tree-Node (folgt der Hardware)
    transportBar.metronomeTargetNames = [this] { return buildOutputPairNames(); };

    // Ausgabe-Paar des Loop-Playbacks (B6): dieselbe Paar-Liste (Befüllung
    // übernimmt rebuildLooperSources oben); Auswahl persistiert looperAnchor
    // und routet die Engine sofort um
    looperPage.onOutputPairSelected = [this] (int pairIndex)
    { engine.setLooperAnchor (pairIndex); };

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

    // Dev-Tile + Dev-Panel (nur im Dev Mode)
    transportBar.onToggleDevPanel = [this] { toggleDevPanel(); };
    transportBar.setDevTileVisible (engine.getUiSettings().isDevModeEnabled());

    timerCallback();      // Status sofort befüllen, nicht erst nach 66ms
    startTimerHz (15);    // EIN Editor-Timer — lock-freie Reads, Repaint bei Änderung
}

EngineEditor::~EngineEditor()
{
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
void EngineEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // Looper-Quellen: Tap-Zeilen (CaptureService) und Kanal-Labels
    // (ChannelNames) ändern die Auswahl-Liste der Looper-Page
    if (source == &engine.getCaptureService() || source == &engine.getChannelNames())
    {
        rebuildLooperSources();
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
std::vector<LooperPage::Source> EngineEditor::buildLooperSources()
{
    std::vector<LooperPage::Source> sources;

    // Master zuerst — die Session-Summe (Master-Output-Tap, B2) ist die
    // naheliegendste Looper-Quelle und der Fallback unbekannter Schlüssel
    sources.push_back ({ "master", "Master" });

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
        sources.push_back ({ "hw:" + juce::String (channel / 2),
                             labels.getLabel (ChannelNames::Direction::input, channel)
                                 + " / "
                                 + labels.getLabel (ChannelNames::Direction::input, channel + 1) });

    // Capture-Taps der Module (virtuelle Slots hinter master_l/_r):
    // _l/_r-Stereo-Paare auf den Basisnamen reduziert, Mono-Taps einzeln
    juce::StringArray seenBaseNames;
    const auto& capture = engine.getCaptureService();

    for (int slot = 2; slot < CaptureService::MAX_VIRTUAL_CHANNELS; ++slot)
    {
        const auto info = capture.getVirtualChannelUiInfo (slot);
        if (! info.inUse || info.name.isEmpty())
            continue;

        auto baseName = info.name;
        if (baseName.endsWith ("_l") || baseName.endsWith ("_r"))
            baseName = baseName.dropLastCharacters (2);

        if (seenBaseNames.contains (baseName))
            continue;

        seenBaseNames.add (baseName);
        sources.push_back ({ "tap:" + baseName, "Tap: " + baseName });
    }

    return sources;
}

void EngineEditor::rebuildLooperSources()
{
    looperPage.setSources (buildLooperSources(),
                           engine.getTransportSettings().getLooperSource());

    // Ausgabe-Paare hängen an denselben Broadcasts (ChannelNames/Hardware)
    looperPage.setOutputPairs (buildOutputPairNames(),
                               engine.getTransportSettings().getLooperAnchor());
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
}

void EngineEditor::toggleBrowserPanel()
{
    browserPanel.setOpen (! browserPanel.isOpen());
    transportBar.setBrowserPanelOpen (browserPanel.isOpen());
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
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    saving ? "Preset speichern" : "Preset laden",
                                                    result.getErrorMessage());
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

    // Looper-Status (B5): Tape-LED (Page offen ODER Loop spielt), Stop-
    // Kachel und Statuszeile der Looper-Page
    {
        const auto& looper = engine.getLooperEngine();
        const auto playing = looper.isPlaying();

        transportBar.setLooperStatus (pageHost.getPage() == TransportBar::pageLooper,
                                      playing);
        looperPage.getStopTile().setEnabled (playing);

        if (playing)
        {
            const auto bars = looper.getLoopBars();
            auto looperStatus = "spielt: " + juce::String (bars)
                              + (bars == 1 ? " Bar" : " Bars");

            // Diagnose: Playhead-Re-Syncs (Duck-Snaps) — häufen sie sich,
            // wackelt die Link-Achse oder der Audio-Callback (Engine-Doku)
            if (const auto snaps = looper.getSnapCount(); devMode && snaps > 0)
                looperStatus << juce::String::fromUTF8 (" · ") << juce::String (snaps)
                             << (snaps == 1 ? " Re-Sync" : " Re-Syncs");

            looperPage.setStatus (looperStatus);
        }
        else
        {
            looperPage.setStatus (juce::String::fromUTF8 (
                "bereit — Segment-Klick committet die letzten 8/4/2/1 Takte"));
        }
    }

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

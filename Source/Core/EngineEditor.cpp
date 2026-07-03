#include "EngineEditor.h"

#include "EngineProcessor.h"
#include "Modules/AirwindowsDensityModule.h"
#include "Modules/AirwindowsSlewModule.h"
#include "Modules/AirwindowsSpiralModule.h"
#include "Modules/AirwindowsAir4Module.h"
#include "Modules/AirwindowsCansModule.h"
#include "Modules/AirwindowsCansAWModule.h"
#include "Modules/AirwindowsConsole0BussModule.h"
#include "Modules/AirwindowsConsole0ChannelModule.h"
#include "Modules/AirwindowsConsoleLABussModule.h"
#include "Modules/AirwindowsConsoleMCBussModule.h"
#include "Modules/AirwindowsDeBessModule.h"
#include "Modules/AirwindowsDeBezModule.h"
#include "Modules/AirwindowsDeRez3Module.h"
#include "Modules/AirwindowsDigitalBlackModule.h"
#include "Modules/AirwindowsDiscontapeityModule.h"
#include "Modules/AirwindowsDistance3Module.h"
#include "Modules/AirwindowsDubSub2Module.h"
#include "Modules/AirwindowsDubly3Module.h"
#include "Modules/AirwindowsFatEQModule.h"
#include "Modules/AirwindowsFlutter2Module.h"
#include "Modules/AirwindowsGatelopeModule.h"
#include "Modules/AirwindowsGlitchShifterModule.h"
#include "Modules/AirwindowsHypersoftModule.h"
#include "Modules/AirwindowsInflamerModule.h"
#include "Modules/AirwindowsIsolator3Module.h"
#include "Modules/AirwindowsMackityModule.h"
#include "Modules/AirwindowsOneCornerClipModule.h"
#include "Modules/AirwindowsParametricModule.h"
#include "Modules/AirwindowsPearEQModule.h"
#include "Modules/AirwindowsPockey2Module.h"
#include "Modules/AirwindowsPointyGuitarModule.h"
#include "Modules/AirwindowsPop2Module.h"
#include "Modules/AirwindowsSilkenModule.h"
#include "Modules/AirwindowsSingleEndedTriodeModule.h"
#include "Modules/AirwindowsSmoothModule.h"
#include "Modules/AirwindowsSmoothEQ3Module.h"
#include "Modules/AirwindowsSoftGateModule.h"
#include "Modules/AirwindowsStoneFireCompModule.h"
#include "Modules/AirwindowsStonefireModule.h"
#include "Modules/AirwindowsSweetenModule.h"
#include "Modules/AirwindowsTakeCareModule.h"
#include "Modules/AirwindowsTapeDelay2Module.h"
#include "Modules/AirwindowsTapeDustModule.h"
#include "Modules/AirwindowsTapeHack2Module.h"
#include "Modules/AirwindowsToneSlantModule.h"
#include "Modules/AirwindowsTremoSquareModule.h"
#include "Modules/AirwindowsTrianglizerModule.h"
#include "Modules/AirwindowsTube2Module.h"
#include "Modules/AirwindowsVibratoModule.h"
#include "Modules/AirwindowsWeightModule.h"
#include "Modules/AirwindowsWiderModule.h"
#include "Modules/AirwindowsChamberModule.h"
#include "Modules/AirwindowsGalacticModule.h"
#include "Modules/AirwindowsVerbTinyModule.h"
#include "Modules/AirwindowsKBeyondModule.h"
#include "Modules/AirwindowsKCathedral5Module.h"
#include "Modules/AirwindowsKWoodRoomModule.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/CaptureTapModule.h"
#include "Modules/LfoModule.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/ScopeModule.h"
#include "Modules/StepSequencerModule.h"
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
              &engineProcessor.getInputLinkSend(), &engineProcessor.getUiSettings())
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
    { pageHost.setPage (pageIndex); };

    transportBar.setBrowserItems (buildBrowserItems());

    // Metronom-Ziel-Paare fürs Link-Menü: Labels aus den ChannelNames,
    // Kanalzahl aus dem audio_out-Tree-Node (folgt der Hardware)
    transportBar.metronomeTargetNames = [this]
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
    };

    // -- Capture-Panel + Toast ------------------------------------------------
    capturePanel.setVisible (false);
    capturePanel.onToast = [this] (const juce::String& message)
    { captureToast.show (message); };

    engine.getCaptureService().onExportFinished =
        [this] (const CaptureWriter::Report& report) { handleExportReport (report); };

    addAndMakeVisible (transportBar);
    addChildComponent (capturePanel);    // eingeklappt bis zum Shift-Klick
    addAndMakeVisible (pageHost);        // Device (Canvas) + Platzhalter-Pages
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

    // Dev-Tile + Dev-Panel (nur im Dev Mode)
    transportBar.onToggleDevPanel = [this] { toggleDevPanel(); };
    transportBar.setDevTileVisible (engine.getUiSettings().isDevModeEnabled());

    timerCallback();      // Status sofort befüllen, nicht erst nach 66ms
    startTimerHz (15);    // EIN Editor-Timer — lock-freie Reads, Repaint bei Änderung
}

EngineEditor::~EngineEditor()
{
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
std::vector<ModuleBrowser::Item> EngineEditor::buildBrowserItems()
{
    const auto addModule = [this] (const char* moduleId)
    {
        // Versetzt platzieren, damit gestapelte Nodes greifbar bleiben
        const auto offset = 24 * (canvas.getNumNodeComponents() % 8);
        const auto created = graphManager.addModuleNode (moduleId, { 40 + offset, 40 + offset });
        jassertquiet (created.isValid());
    };

    std::vector<ModuleBrowser::Item> items;

    items.push_back ({ "Attenuator",  [addModule] { addModule (AttenuatorModule::staticModuleId); }, false });
    items.push_back ({ "LFO",         [addModule] { addModule (LfoModule::staticModuleId); }, false });
    items.push_back ({ "Scope",       [addModule] { addModule (ScopeModule::staticModuleId); }, false });
    items.push_back ({ "Sequencer",   [addModule] { addModule (StepSequencerModule::staticModuleId); }, false });
    items.push_back ({ "Capture Tap", [addModule] { addModule (CaptureTapModule::staticModuleId); }, false });
    items.push_back ({ "Density",     [addModule] { addModule (AirwindowsDensityModule::staticModuleId); }, false });
    items.push_back ({ "Slew",        [addModule] { addModule (AirwindowsSlewModule::staticModuleId); }, false });
    items.push_back ({ "Spiral",      [addModule] { addModule (AirwindowsSpiralModule::staticModuleId); }, false });
    items.push_back ({ "Air4",     [addModule] { addModule (AirwindowsAir4Module::staticModuleId); }, false });
    items.push_back ({ "Cans",     [addModule] { addModule (AirwindowsCansModule::staticModuleId); }, false });
    items.push_back ({ "CansAW",     [addModule] { addModule (AirwindowsCansAWModule::staticModuleId); }, false });
    items.push_back ({ "Console0Buss",     [addModule] { addModule (AirwindowsConsole0BussModule::staticModuleId); }, false });
    items.push_back ({ "Console0Channel",     [addModule] { addModule (AirwindowsConsole0ChannelModule::staticModuleId); }, false });
    items.push_back ({ "ConsoleLABuss",     [addModule] { addModule (AirwindowsConsoleLABussModule::staticModuleId); }, false });
    items.push_back ({ "ConsoleMCBuss",     [addModule] { addModule (AirwindowsConsoleMCBussModule::staticModuleId); }, false });
    items.push_back ({ "DeBess",     [addModule] { addModule (AirwindowsDeBessModule::staticModuleId); }, false });
    items.push_back ({ "DeBez",     [addModule] { addModule (AirwindowsDeBezModule::staticModuleId); }, false });
    items.push_back ({ "DeRez3",     [addModule] { addModule (AirwindowsDeRez3Module::staticModuleId); }, false });
    items.push_back ({ "DigitalBlack",     [addModule] { addModule (AirwindowsDigitalBlackModule::staticModuleId); }, false });
    items.push_back ({ "Discontapeity",     [addModule] { addModule (AirwindowsDiscontapeityModule::staticModuleId); }, false });
    items.push_back ({ "Distance3",     [addModule] { addModule (AirwindowsDistance3Module::staticModuleId); }, false });
    items.push_back ({ "DubSub2",     [addModule] { addModule (AirwindowsDubSub2Module::staticModuleId); }, false });
    items.push_back ({ "Dubly3",     [addModule] { addModule (AirwindowsDubly3Module::staticModuleId); }, false });
    items.push_back ({ "FatEQ",     [addModule] { addModule (AirwindowsFatEQModule::staticModuleId); }, false });
    items.push_back ({ "Flutter2",     [addModule] { addModule (AirwindowsFlutter2Module::staticModuleId); }, false });
    items.push_back ({ "Gatelope",     [addModule] { addModule (AirwindowsGatelopeModule::staticModuleId); }, false });
    items.push_back ({ "GlitchShifter",     [addModule] { addModule (AirwindowsGlitchShifterModule::staticModuleId); }, false });
    items.push_back ({ "Hypersoft",     [addModule] { addModule (AirwindowsHypersoftModule::staticModuleId); }, false });
    items.push_back ({ "Inflamer",     [addModule] { addModule (AirwindowsInflamerModule::staticModuleId); }, false });
    items.push_back ({ "Isolator3",     [addModule] { addModule (AirwindowsIsolator3Module::staticModuleId); }, false });
    items.push_back ({ "Mackity",     [addModule] { addModule (AirwindowsMackityModule::staticModuleId); }, false });
    items.push_back ({ "OneCornerClip",     [addModule] { addModule (AirwindowsOneCornerClipModule::staticModuleId); }, false });
    items.push_back ({ "Parametric",     [addModule] { addModule (AirwindowsParametricModule::staticModuleId); }, false });
    items.push_back ({ "PearEQ",     [addModule] { addModule (AirwindowsPearEQModule::staticModuleId); }, false });
    items.push_back ({ "Pockey2",     [addModule] { addModule (AirwindowsPockey2Module::staticModuleId); }, false });
    items.push_back ({ "PointyGuitar",     [addModule] { addModule (AirwindowsPointyGuitarModule::staticModuleId); }, false });
    items.push_back ({ "Pop2",     [addModule] { addModule (AirwindowsPop2Module::staticModuleId); }, false });
    items.push_back ({ "Silken",     [addModule] { addModule (AirwindowsSilkenModule::staticModuleId); }, false });
    items.push_back ({ "SingleEndedTriode",     [addModule] { addModule (AirwindowsSingleEndedTriodeModule::staticModuleId); }, false });
    items.push_back ({ "Smooth",     [addModule] { addModule (AirwindowsSmoothModule::staticModuleId); }, false });
    items.push_back ({ "SmoothEQ3",     [addModule] { addModule (AirwindowsSmoothEQ3Module::staticModuleId); }, false });
    items.push_back ({ "SoftGate",     [addModule] { addModule (AirwindowsSoftGateModule::staticModuleId); }, false });
    items.push_back ({ "StoneFireComp",     [addModule] { addModule (AirwindowsStoneFireCompModule::staticModuleId); }, false });
    items.push_back ({ "Stonefire",     [addModule] { addModule (AirwindowsStonefireModule::staticModuleId); }, false });
    items.push_back ({ "Sweeten",     [addModule] { addModule (AirwindowsSweetenModule::staticModuleId); }, false });
    items.push_back ({ "TakeCare",     [addModule] { addModule (AirwindowsTakeCareModule::staticModuleId); }, false });
    items.push_back ({ "TapeDelay2",     [addModule] { addModule (AirwindowsTapeDelay2Module::staticModuleId); }, false });
    items.push_back ({ "TapeDust",     [addModule] { addModule (AirwindowsTapeDustModule::staticModuleId); }, false });
    items.push_back ({ "TapeHack2",     [addModule] { addModule (AirwindowsTapeHack2Module::staticModuleId); }, false });
    items.push_back ({ "ToneSlant",     [addModule] { addModule (AirwindowsToneSlantModule::staticModuleId); }, false });
    items.push_back ({ "TremoSquare",     [addModule] { addModule (AirwindowsTremoSquareModule::staticModuleId); }, false });
    items.push_back ({ "Trianglizer",     [addModule] { addModule (AirwindowsTrianglizerModule::staticModuleId); }, false });
    items.push_back ({ "Tube2",     [addModule] { addModule (AirwindowsTube2Module::staticModuleId); }, false });
    items.push_back ({ "Vibrato",     [addModule] { addModule (AirwindowsVibratoModule::staticModuleId); }, false });
    items.push_back ({ "Weight",     [addModule] { addModule (AirwindowsWeightModule::staticModuleId); }, false });
    items.push_back ({ "Wider",     [addModule] { addModule (AirwindowsWiderModule::staticModuleId); }, false });
    items.push_back ({ "Chamber",     [addModule] { addModule (AirwindowsChamberModule::staticModuleId); }, false });
    items.push_back ({ "Galactic",     [addModule] { addModule (AirwindowsGalacticModule::staticModuleId); }, false });
    items.push_back ({ "VerbTiny",     [addModule] { addModule (AirwindowsVerbTinyModule::staticModuleId); }, false });
    items.push_back ({ "kBeyond",     [addModule] { addModule (AirwindowsKBeyondModule::staticModuleId); }, false });
    items.push_back ({ "kCathedral5",     [addModule] { addModule (AirwindowsKCathedral5Module::staticModuleId); }, false });
    items.push_back ({ "kWoodRoom",     [addModule] { addModule (AirwindowsKWoodRoomModule::staticModuleId); }, false });

    // Link-Send: Eingangszahl ist fix beim Anlegen (7.2) → kleiner Dialog
    // (Mono-/Stereo-Anzahl), dann Node mit der gewählten Config materialisieren
    items.push_back ({ "Link Send", [this]
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
                                                transportBar.getPlusTile().getScreenBounds(),
                                                nullptr);
    }, false });

    items.push_back ({ juce::String::fromUTF8 ("Preset laden…"),
                       [this] { launchPresetChooser (false); }, true });
    items.push_back ({ juce::String::fromUTF8 ("Preset speichern…"),
                       [this] { launchPresetChooser (true); }, false });

    return items;
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

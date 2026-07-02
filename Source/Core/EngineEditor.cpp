#include "EngineEditor.h"

#include "EngineProcessor.h"
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
              &engineProcessor.getInputLinkSend())
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
                                                      engine.getOscController()));
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
        // Pages-Gerüst folgt in Schritt 6 — bis dahin bleibt die Canvas
        // (Device) aktiv und die anderen Icons melden sich per Toast
        if (pageIndex != TransportBar::pageDevice)
        {
            const char* names[] = { "Grid", "Mixer", "Clip", "Device" };
            captureToast.show (juce::String (names[pageIndex]) + "-Page folgt");
            transportBar.setSelectedPage (TransportBar::pageDevice);
        }
    };

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
    addAndMakeVisible (canvas);
    addChildComponent (captureToast);    // Overlay, zeigt sich selbst

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setSize (1480, 720);

    timerCallback();      // Status sofort befüllen, nicht erst nach 66ms
    startTimerHz (15);    // EIN Editor-Timer — lock-freie Reads, Repaint bei Änderung
}

EngineEditor::~EngineEditor()
{
    // Der Service überlebt den Editor — Callback lösen, sonst zeigte ein
    // späterer Export-Report ins Leere
    engine.getCaptureService().onExportFinished = nullptr;

    // Default-LookAndFeel VOR der Member-Destruktion zurücksetzen
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
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

    canvas.setBounds (bounds);

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

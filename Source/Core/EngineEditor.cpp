#include "EngineEditor.h"

#include "EngineProcessor.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/CaptureTapModule.h"
#include "Modules/LfoModule.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/ScopeModule.h"
#include "Modules/StepSequencerModule.h"
#include "Util/ScaleQuantizer.h"

namespace conduit
{

EngineEditor::EngineEditor (EngineProcessor& engineProcessor)
    : juce::AudioProcessorEditor (engineProcessor),
      engine (engineProcessor),
      rootState (engineProcessor.getRootState()),
      undoManager (engineProcessor.getUndoManager()),
      graphManager (engineProcessor.getGraphManager()),
      linkClock (engineProcessor.getLinkClock()),
      capturePanel (engineProcessor.getCaptureSettings(), engineProcessor.getCaptureService()),
      canvas (rootState, engineProcessor.getGraphManager(), engineProcessor.getNodeUiRegistry())
{
    const auto addModule = [this] (const char* moduleId)
    {
        // Versetzt platzieren, damit gestapelte Nodes greifbar bleiben
        const auto offset = 24 * (canvas.getNumNodeComponents() % 8);
        const auto created = graphManager.addModuleNode (moduleId, { 40 + offset, 40 + offset });
        jassertquiet (created.isValid());
    };

    addButton.onClick      = [addModule] { addModule (AttenuatorModule::staticModuleId); };
    addLfoButton.onClick   = [addModule] { addModule (LfoModule::staticModuleId); };
    addScopeButton.onClick = [addModule] { addModule (ScopeModule::staticModuleId); };
    addSeqButton.onClick   = [addModule] { addModule (StepSequencerModule::staticModuleId); };
    addLinkSendButton.onClick = [addModule] { addModule (LinkAudioSendModule::staticModuleId); };
    addTapButton.onClick      = [addModule] { addModule (CaptureTapModule::staticModuleId); };

    undoButton.onClick = [this] { undoManager.undo(); };
    redoButton.onClick = [this] { undoManager.redo(); };
    saveButton.onClick = [this] { launchPresetChooser (true); };
    loadButton.onClick = [this] { launchPresetChooser (false); };

    // Link-Transport: Slider schreibt in die Session, der Timer pollt zurück
    tempoSlider.setRange (20.0, 300.0, 0.1);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setValue (linkClock.getTempo(), juce::dontSendNotification);
    tempoSlider.onValueChange = [this] { linkClock.setTempo (tempoSlider.getValue()); };

    peersLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    peersLabel.setJustificationType (juce::Justification::centredLeft);

    // Globale Session-Skala (Schema 6.2): schreibt die Root-Properties —
    // bewusst ohne UndoManager (Session-Setting wie Parameter-Sweeps);
    // preset-persistent ist sie über den Tree trotzdem
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

    // -- Capture-UI (Baustein 6) -----------------------------------------------
    captureAllButton.onClick = [this]
    {
        const auto numTracks = engine.getCaptureService().exportAll();
        if (numTracks == 0)
            captureToast.show ("Keine aktive Aufnahme");
    };

    capturePanelToggle.setClickingTogglesState (true);
    capturePanelToggle.onClick = [this]
    {
        capturePanel.setVisible (capturePanelToggle.getToggleState());
        resized();
    };
    capturePanel.setVisible (false);
    capturePanel.onToast = [this] (const juce::String& message)
    { captureToast.show (message); };

    engine.getCaptureService().onExportFinished =
        [this] (const CaptureWriter::Report& report) { handleExportReport (report); };

    // OSC-Status (verbunden in Main::initialise)
    const auto oscPort = engine.getOscController().getConnectedPort();
    oscLabel.setText (oscPort > 0 ? "OSC :" + juce::String (oscPort)
                                  : juce::String ("OSC: aus"),
                      juce::dontSendNotification);
    oscLabel.setColour (juce::Label::textColourId,
                        oscPort > 0 ? juce::Colours::white.withAlpha (0.7f)
                                    : juce::Colours::orange);
    oscLabel.setJustificationType (juce::Justification::centredLeft);

    warningLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    warningLabel.setJustificationType (juce::Justification::centredRight);

    const auto warning = rootState.getProperty (id::audioSetupWarning).toString();

    if (warning.isNotEmpty())
        warningLabel.setText ("Audio-Setup: " + warning, juce::dontSendNotification);

    addAndMakeVisible (addButton);
    addAndMakeVisible (addLfoButton);
    addAndMakeVisible (addScopeButton);
    addAndMakeVisible (addSeqButton);
    addAndMakeVisible (addLinkSendButton);
    addAndMakeVisible (addTapButton);
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (tempoSlider);
    addAndMakeVisible (rootCombo);
    addAndMakeVisible (scaleCombo);
    addAndMakeVisible (peersLabel);
    addAndMakeVisible (oscLabel);
    addAndMakeVisible (warningLabel);
    addAndMakeVisible (captureAllButton);
    addAndMakeVisible (capturePanelToggle);
    addChildComponent (capturePanel);    // eingeklappt bis zum Toggle
    addAndMakeVisible (canvas);
    addChildComponent (captureToast);    // Overlay, zeigt sich selbst

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setSize (1480, 720);

    timerCallback();      // Peer-Label sofort befüllen, nicht erst nach 66ms
    // EIN Editor-Timer mit 15 Hz statt 4 Hz + Capture-Extra-Timer: alle
    // gepollten Getter sind lock-freie Atomics, Repaints nur bei Änderung —
    // die Capture-Statusanzeige braucht ~15 Hz, Link-Polling verträgt das
    startTimerHz (15);
}

EngineEditor::~EngineEditor()
{
    // Der Service überlebt den Editor — Callback lösen, sonst zeigte ein
    // späterer Export-Report ins Leere
    engine.getCaptureService().onExportFinished = nullptr;
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
    // Kein Kampf mit dem User: während des Drags gewinnt der Slider
    if (! tempoSlider.isMouseButtonDown())
        tempoSlider.setValue (linkClock.getTempo(), juce::dontSendNotification);

    const auto numPeers = linkClock.getNumPeers();
    const auto text = numPeers == 0
                          ? juce::String ("Link: keine Peers")
                          : "Link: " + juce::String (numPeers)
                                + (numPeers == 1 ? " Peer" : " Peers");

    if (peersLabel.getText() != text)
        peersLabel.setText (text, juce::dontSendNotification);

    // Capture-Status: Button-Ring immer, Panel nur wenn aufgeklappt —
    // beides repaintet nur bei sichtbarer Änderung
    captureAllButton.setStatus (engine.getCaptureService().getUiStatus());
    if (capturePanel.isVisible())
        capturePanel.refresh();
}

//==============================================================================
void EngineEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff24272c));  // Toolbar-Hintergrund
}

void EngineEditor::resized()
{
    auto bounds = getLocalBounds();
    auto toolbar = bounds.removeFromTop (toolbarHeight).reduced (8, 6);  // Buttons ≥ 44px hoch

    const auto place = [&toolbar] (juce::Component& component, int width, int gapAfter = 8)
    {
        component.setBounds (toolbar.removeFromLeft (width));
        toolbar.removeFromLeft (gapAfter);
    };

    place (addButton,       95);
    place (addLfoButton,    80);
    place (addScopeButton,  90);
    place (addSeqButton,    80);
    place (addLinkSendButton, 110);
    place (addTapButton,    70);
    place (undoButton,      65);
    place (redoButton,      65, 16);
    place (saveButton,      65);
    place (loadButton,      65, 16);
    place (tempoSlider,    130);
    place (captureAllButton, 60);            // neben dem Link-Transport
    place (capturePanelToggle, 85, 16);
    place (rootCombo,       60);
    place (scaleCombo,     110, 16);
    place (peersLabel,     115);
    place (oscLabel,        80);
    warningLabel.setBounds (toolbar);

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

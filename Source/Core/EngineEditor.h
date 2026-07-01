#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "LinkClock.h"
#include "Capture/CaptureWriter.h"
#include "UI/CaptureAllButton.h"
#include "UI/CapturePanel.h"
#include "UI/CaptureToast.h"
#include "UI/NodeCanvas.h"

namespace conduit
{

class EngineProcessor;

//==============================================================================
/**
    Haupt-Editor der App — read/listen-only gegenüber dem Datenmodell
    (CLAUDE.md 6); Patch-Aktionen laufen über GraphManager/UndoManager.

    Layout: Toolbar (Add/Undo/Redo + Link-Transport, Touch-Targets ≥ 44px)
    über dem NodeCanvas; die audioSetupWarning (9.1) erscheint rechts.

    Link-Transport: Tempo und Peer-Zahl kommen aus der Link-Session — NICHT
    aus dem ValueTree (Session-Zustand, kein Patch-Zustand). Der Editor-Timer
    pollt die thread-sicheren LinkClock-Getter, damit Peer-Änderungen aus
    dem Netz in der UI ankommen; der Slider schreibt via setTempo() zurück.

    Capture-UI (Baustein 6): CaptureAllButton neben dem Link-Transport,
    einklappbares CapturePanel unter der Toolbar, nicht-modaler Toast für
    Export-Ergebnisse. Der EINE Editor-Timer läuft mit 15 Hz statt eines
    zweiten Capture-Timers: alle gepollten Quellen (LinkClock, Capture-
    Status-Atomics) sind lock-freie Reads im Nanosekundenbereich, Repaints
    passieren nur bei sichtbarer Änderung — ein zweiter Timer wäre reine
    Verdrahtungs-Komplexität ohne messbare Ersparnis.

    "Nach Export freigeben": auf User-Wunsch wird der RAM-Puffer NIE ohne
    Rückfrage geleert — handleExportReport() zeigt immer erst einen
    Ok/Cancel-Dialog, bevor gehaltene Kanäle freigegeben werden.
*/
class EngineEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    /** deviceManager ist nur im Standalone-Pfad gesetzt (Main.cpp). Im
        Plugin-/Test-Pfad (EngineProcessor::createEditor) bleibt er nullptr —
        dann wird der Audio-Settings-Button ausgeblendet. */
    explicit EngineEditor (EngineProcessor& engineProcessor,
                           juce::AudioDeviceManager* deviceManager = nullptr);
    ~EngineEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void launchPresetChooser (bool saving);
    void handleExportReport (const CaptureWriter::Report& report);

    static constexpr int toolbarHeight = 56;

    EngineProcessor& engine;
    juce::ValueTree rootState;  // ref-counted Handle, read/listen-only
    juce::UndoManager& undoManager;
    GraphManager& graphManager;
    LinkClock& linkClock;
    juce::AudioDeviceManager* deviceManager;  // nullptr im Plugin-/Test-Pfad

    juce::TextButton addButton      { juce::String::fromUTF8 ("\xef\xbc\x8b Atten") };
    juce::TextButton addLfoButton   { juce::String::fromUTF8 ("\xef\xbc\x8b LFO") };
    juce::TextButton addScopeButton { juce::String::fromUTF8 ("\xef\xbc\x8b Scope") };
    juce::TextButton addSeqButton   { juce::String::fromUTF8 ("\xef\xbc\x8b Seq") };
    juce::TextButton addLinkSendButton { juce::String::fromUTF8 ("\xef\xbc\x8b LinkSend") };
    juce::TextButton addTapButton { juce::String::fromUTF8 ("\xef\xbc\x8b Tap") };
    juce::TextButton undoButton   { "Undo" };
    juce::TextButton redoButton   { "Redo" };
    juce::TextButton saveButton   { "Save" };
    juce::TextButton loadButton   { "Load" };
    juce::TextButton audioSettingsButton { "Audio" };
    juce::Slider tempoSlider      { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::ComboBox rootCombo;     // globale Session-Skala (Root-Tree-Properties)
    juce::ComboBox scaleCombo;
    juce::Label peersLabel;
    juce::Label oscLabel;
    juce::Label warningLabel;

    // Muss den async Callback überleben (JUCE_MODAL_LOOPS_PERMITTED=0)
    std::unique_ptr<juce::FileChooser> presetChooser;

    // Capture-UI (Baustein 6)
    CaptureAllButton captureAllButton;
    juce::TextButton capturePanelToggle { "Capture" };
    CapturePanel capturePanel;
    CaptureToast captureToast;

    NodeCanvas canvas;

    // Port-Tooltips der I/O-Endpunkte (ChannelNames-Labels, Maus-Hover)
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineEditor)
};

} // namespace conduit

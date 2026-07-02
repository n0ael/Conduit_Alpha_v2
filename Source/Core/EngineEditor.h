#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "LinkClock.h"
#include "Capture/CaptureWriter.h"
#include "UI/CapturePanel.h"
#include "UI/CaptureToast.h"
#include "UI/NodeCanvas.h"
#include "UI/PushLookAndFeel.h"
#include "UI/TransportBar.h"

namespace conduit
{

class EngineProcessor;

//==============================================================================
/**
    Haupt-Editor der App — read/listen-only gegenüber dem Datenmodell
    (CLAUDE.md 6); Patch-Aktionen laufen über GraphManager/UndoManager.

    Layout: TransportBar im Push-3-Stil (ersetzt die alte Modul-Button-
    Toolbar; Module kommen über den „+"-Browser) über dem NodeCanvas.
    Der PushLookAndFeel (Jost, dunkle Kacheln) ist Default-LookAndFeel der
    App — damit rendern auch PopupMenus, Dialoge und Settings im selben Look.

    Verdrahtung: die Bar besitzt nur UI-Zustand; alle Aktionen laufen über
    ihre Hooks (Undo/Redo, Save, Einstellungen, Capture, Browser-Einträge).
    Der EINE Editor-Timer (15 Hz) speist Status zurück (Tempo/Peers via
    refresh(), Capture-LED, audioSetupWarning) — alle gepollten Quellen sind
    lock-freie Reads, Repaints nur bei sichtbarer Änderung.

    Capture: ⛶-Kachel = Export aller aktiven Aufnahmen, Shift-Klick klappt
    das CapturePanel (Kanal-Zeilen) unter der Bar auf. "Nach Export
    freigeben" fragt IMMER erst nach (User-Vorgabe) — handleExportReport().
*/
class EngineEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    /** deviceManager ist nur im Standalone-Pfad gesetzt (Main.cpp). Im
        Plugin-/Test-Pfad (EngineProcessor::createEditor) bleibt er nullptr —
        dann öffnen die Einstellungen ohne Audio-Geräte-Tab. */
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
    [[nodiscard]] std::vector<ModuleBrowser::Item> buildBrowserItems();

    EngineProcessor& engine;
    juce::ValueTree rootState;  // ref-counted Handle, read/listen-only
    juce::UndoManager& undoManager;
    GraphManager& graphManager;
    LinkClock& linkClock;
    juce::AudioDeviceManager* deviceManager;  // nullptr im Plugin-/Test-Pfad

    // Vor der Bar deklariert — Default-LookAndFeel wird im ctor gesetzt,
    // im dtor zurückgesetzt (Komponenten sterben danach)
    push::PushLookAndFeel lookAndFeel;

    TransportBar transportBar;

    // Muss den async Callback überleben (JUCE_MODAL_LOOPS_PERMITTED=0)
    std::unique_ptr<juce::FileChooser> presetChooser;

    // Capture-UI: Panel klappt unter der Bar auf (Shift-Klick auf ⛶)
    CapturePanel capturePanel;
    CaptureToast captureToast;

    NodeCanvas canvas;

    // Port-Tooltips der I/O-Endpunkte (ChannelNames-Labels, Maus-Hover)
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineEditor)
};

} // namespace conduit

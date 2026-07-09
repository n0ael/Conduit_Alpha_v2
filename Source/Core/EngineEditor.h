#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "LinkClock.h"
#include "Browser/BrowserContextProvider.h"
#include "Browser/BrowserModel.h"
#include "Capture/CaptureWriter.h"
#include "UI/Browser/BrowserPanel.h"
#include "UI/CapturePanel.h"
#include "UI/CaptureToast.h"
#include "UI/DevPanel.h"
#include <array>

#include "UI/GridPage.h"
#include "UI/LooperPage.h"
#include "UI/NodeCanvas.h"
#include "UI/PageHost.h"
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
                           public juce::DragAndDropContainer,   // Browser-Drag → Canvas
                           private juce::Timer,
                           private juce::ChangeListener,
                           private juce::ValueTree::Listener
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

    // UiSettings → Anwendung (Skalierung/FontScale live) — die Settings-
    // Klasse speichert nur, HIER wird angewendet (headless Tests bleiben
    // frei von globalem Desktop-Zustand)
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    // Looper-Quellenliste folgt Link-Kanal-Wahl (targetPeer/targetChannel),
    // Node-Farben und I/O-Kanalzahlen live — die Tap-Registrierung selbst
    // broadcastet bereits über den CaptureService (read/listen-only, 6)
    void valueTreePropertyChanged (juce::ValueTree& tree,
                                   const juce::Identifier& property) override;

    void launchPresetChooser (bool saving);
    void handleExportReport (const CaptureWriter::Report& report);
    void toggleDevPanel();
    void closeDevPanelAsync();

    /** ZENTRALER Page-Wechsel — jeder Pfad (Page-Icons, Tape-Kachel)
        läuft hierüber, damit der BrowserContextProvider nie desynct. */
    void selectPage (int pageIndex);

    void toggleBrowserPanel();
    void toggleEditorPanel();   // Grid-Editor-Dock-Panel (S2), unabhängig vom Browser

    // Looper-Page (B3/M6): Quellen-Liste (Master + Hardware-Paare + Outs +
    // Taps) neu aufbauen — bei Start, Tap-Änderungen und ChannelNames-Broadcasts
    [[nodiscard]] std::vector<LooperPanel::Source> buildLooperSources();
    void rebuildLooperSources();

    /** Farbe einer Looper-Quelle (08.07.2026): hw:/out: → Kanal-Farbe aus
        ChannelNames, tap: → explizite nodeColour des Moduls; transparent =
        keine (Strip nutzt Default). */
    [[nodiscard]] juce::Colour looperSourceColour (const juce::String& sourceKey) const;

    /** M6: Struktur der Page aus den LooperSettings ziehen (Looper-Zahl,
        Tracks, sichtbare Slots, Quellen, Mix-Werte) — bei Start und jedem
        LooperSettings-Broadcast. */
    void refreshLooperStructure();

    /** Panel-Hooks (Quelle/Commit/Slots/Mix/Clip-Controls) verdrahten —
        nach jedem Panel-Neuaufbau (LooperPage::onPanelsChanged). */
    void wireLooperPanels();

    /** Slot-Tap-Semantik (Übergabe §2/§3): leer = Target armen, belegt =
        Launch/Retrigger/Stop nach Settings; TARGET-Halten = nur Aktiv-
        Auswahl. */
    void handleLooperSlotTap (int looperIndex, int trackIndex, int slotIndex);

    /** Direkt nach erfolgreichem Commit: die committeten Takte aus dem
        Strip als Tinte-Bild schnappen (aktuelle View) und der Ziel-Zelle
        geben — sie zeigt sie invertiert auf Quellfarbe (09.07.2026). */
    void captureLooperClipThumbnail (int looperIndex);

    /** Looper-Status in die Page spiegeln (Editor-Timer, 15 Hz):
        Struktur, Labels, Meter, Thumbnail-Aufräumen. */
    void refreshLooperStatus (bool devMode);

    /** Monitor-synchroner Leichtgewichts-Pfad (VBlank, User 09.07.2026):
        NUR Abspielposition der spielenden Clips (Zell-Sweep + Takt-Pie)
        und Target-Puls — lock-freie Atomic-Reads, no-op wenn die
        Looper-Page nicht sichtbar ist. Alles andere bleibt beim Timer. */
    void tickLooperPlayheads();

    /** Labels der Hardware-Ausgangs-Paare (Kanäle 2n/2n+1) aus dem
        audio_out-Tree-Node + ChannelNames — Metronom-Ausgang (Link-Menü)
        und Looper-Output-Selektor (B6) teilen sich die Liste. */
    [[nodiscard]] juce::StringArray buildOutputPairNames();

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

    // Retro-Looper-Page (B3/M6) — hinter der Tape-Kachel, VOR dem PageHost
    // deklariert (der hält eine Referenz darauf)
    LooperPage looperPage;

    // Grid-Page (Ω, M1 Teil 3 — erster spielbarer Ton): GridVoiceEngine +
    // MidiDeviceTarget kommen als Referenzen vom EngineProcessor (Muster
    // Looper/Metronom, 4.2 ITouchMacro) — nach engine, VOR dem PageHost
    // deklariert (der hält eine Referenz darauf). Initialisierung im .cpp-
    // Ctor (EngineProcessor ist hier nur vorwärtsdeklariert, 4.6-Muster
    // wie canvas/capturePanel).
    GridPage gridPage;

    // TARGET-Halten pro Looper (Aktiv-Auswahl statt Launch, Übergabe §2)
    std::array<bool, 4> looperTargetHold {};

    // M7: Header-Gesten der Looper-Page (Delete/Save halten + Ziel
    // antippen; Delete optional als Latch — Menü-Option für Nicht-Touch)
    enum class LooperGesture { none, deleteClips, saveClips };
    LooperGesture looperGesture = LooperGesture::none;
    bool looperDeleteLatched = false;

    /** Track entfernen (Delete-Geste auf Header / Long-Press) — nur der
        letzte Track, nur leer & gestoppt (M4-Entscheidung). */
    void removeLooperTrack (int looperIndex, int trackIndex);

    // Nach Canvas + LooperPage + GridPage deklariert (hält Referenzen
    // darauf): die Pages hinter den Push-Icons — Device = Canvas, Grid =
    // GridPage, Rest Platzhalter
    PageHost pageHost { canvas, looperPage, gridPage };

    // Browser-Panel (rechts angedockt): Kontext ← selectPage, Modell hält
    // seinen EIGENEN ValueTree (nie im Patch) — Reihenfolge: Worker-Pool
    // VOR Provider/Modell/Panel (der Pool stirbt zuletzt und joint dabei
    // laufende Index-/Scan-Jobs; Ergebnisse verwirft das Alive-Flag)
    juce::ThreadPool browserWorker { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };
    BrowserContextProvider browserContext;
    BrowserModel browserModel;   // Init in der Ctor-Liste (braucht EngineProcessor-Definition)
    BrowserPanel browserPanel;   // dito (UiSettings kommt vom Processor)

    // Port-Tooltips der I/O-Endpunkte (ChannelNames-Labels, Maus-Hover)
    juce::TooltipWindow tooltipWindow { this };

    // Zuletzt ANGEWENDETE Oberflächen-Werte — der UiSettings-Broadcast
    // meldet jede Änderung (auch devMode); nur echte Scale-/Font-Deltas
    // lösen setGlobalScaleFactor bzw. die teure LnF-Kaskade aus
    float appliedUiScale   = 1.0f;
    float appliedFontScale = 1.0f;

    // Schwebendes Dev-Panel (nur im Dev Mode) — Toggle über das Dev-Tile
    // der Bar; Dev Mode aus schließt es automatisch
    std::unique_ptr<DevPanel> devPanel;

    // Letzter Member: tickt erst mit Peer, stirbt vor allen Referenzen
    juce::VBlankAttachment looperPlayheadVBlank { this, [this] { tickLooperPlayheads(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineEditor)
};

} // namespace conduit

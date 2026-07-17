#pragma once

#include <array>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "CallbackTimingMonitor.h"
#include "Capture/CaptureService.h"
#include "Capture/LevelMeter.h"
#include "ChannelNames.h"
#include "Looper/BarSampleAnchors.h"
#include "Looper/LooperBank.h"
#include "Looper/LooperSessionModel.h"
#include "Looper/LooperWaveformTap.h"
#include "Metronome.h"
#include "GridVoiceEngine.h"
#include "MidiPortHub.h"
#include "ControllerProfileLibrary.h"
#include "MidiProfileLibrary.h"
#include "MidiRigSettings.h"
#include "TouchLive/LiveRemoteBridge.h"
#include "TouchLive/LiveSetModel.h"
#include "TouchLive/LiveSpectrumTap.h"
#include "TouchLive/TouchLiveClient.h"
#include "TouchLive/TouchLiveMeterBus.h"
#include "TouchLive/TouchLiveSettings.h"
#include "MpeMidiSink.h"
#include "GraphFader.h"
#include "GraphManager.h"
#include "GridPanelSettings.h"
#include "InputLinkSend.h"
#include "LinkClock.h"
#include "MeterSettings.h"
#include "ModuleUiDefaults.h"
#include "UiSettings.h"
#include "NodeUiRegistry.h"
#include "OscController.h"
#include "OscSendService.h"
#include "OscSendSettings.h"
#include "TransportSettings.h"
#include "LooperSettings.h"
#include "RemoteModuleBinder.h"
#include "Interfaces/IClockSource.h"
#include "Modules/ConduitModule.h"
#include "Modules/ModuleFactory.h"
#include "Util/SpscQueue.h"

namespace conduit
{

//==============================================================================
/**
    Zentraler AudioProcessor der App.

    Besitzt den Root-ValueTree (Single Source of Truth für Zustand und
    Serialisierung, CLAUDE.md 6) sowie den juce::AudioProcessorGraph als
    DSP-Engine (CLAUDE.md 5.1). Der Editor ist read/listen-only.

    Graph-Mutationen (addNode / removeConnection) laufen ausschließlich auf
    dem Message Thread — später koordiniert durch den GraphManager.

    Globale Session-Skala (scaleRoot/scaleType am Root, Schema 6.2): ein
    privater ValueTree-Listener spiegelt die Properties in zwei Atomics
    [Message → Audio], processBlock() schreibt sie pro Block in den ClockBus.
*/
class EngineProcessor final : public juce::AudioProcessor,
                              private juce::ValueTree::Listener,
                              private juce::ChangeListener
{
public:
    EngineProcessor();

    /** Test-Injektionspunkt für die Settings-Persistenz: leitet ALLE
        Settings-Dateien des Processors (Capture, ChannelNames, Meter, UI,
        ModuleUiDefaults, Transport, OscSend) in den angegebenen Ordner um —
        Tests schreiben damit in ein Temp-Verzeichnis statt in die echten
        AppData-Dateien des Users. Ein ungültiges File (Default-Konstruktor
        oben) bedeutet: unverändert die Produktions-Pfade der jeweiligen
        defaultOptions(). Dateinamen (applicationName + Suffix) bleiben
        identisch, nur der Speicherort wechselt. */
    explicit EngineProcessor (const juce::File& settingsFolder);

    ~EngineProcessor() override;

    //==========================================================================
    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** Standalone-Multichannel (CLAUDE.md 9): akzeptiert jede diskrete
        I/O-Kanalzahl, damit der AudioProcessorPlayer die echte Device-Zahl
        durchreicht (findMostSuitableLayout probiert sie zuerst) und der Graph
        via graph.setPlayConfigDetails() adaptiert. 0 Eingänge (reines
        Ausgabe-Interface, 9.1) ist zulässig. */
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    /** Preset-System (CLAUDE.md 5.4). Message Thread.

        Save flusht vorher ausstehende OSC-Werte (isDirty-Guard 6.1).
        Load ersetzt den Root-Tree undo-fähig in EINER Transaktion —
        GraphManager und Canvas rebuilden über den Container-Swap-Pfad,
        CalibrationProfiles kommen mit und gelten sofort. */
    juce::Result savePreset (const juce::File& file);
    juce::Result loadPreset (const juce::File& file);

    static constexpr const char* presetFileExtension = ".conduit";

    //==========================================================================
    // Datenmodell — Mutationen NUR auf dem Message Thread (CLAUDE.md 6)
    [[nodiscard]] juce::ValueTree getRootState() noexcept;
    [[nodiscard]] juce::UndoManager& getUndoManager() noexcept;

    /** Patch-Aktionen (Delete-Requests) und UI-Bindungs-Registry. */
    /** Koppelt die reservierten I/O-Tree-Nodes (audio_in/audio_out) an die
        echte Device-Kanalzahl (CLAUDE.md 9, Schritt B): audio_in bekommt
        deviceInputs Ausgangs-Ports, audio_out deviceOutputs Eingangs-Ports —
        die Port-UI folgt der Hardware. Idempotent (schreibt nur bei
        Abweichung). Geräte-getrieben und daher NICHT undo-fähig (Umgebungs-
        Zustand wie ensureIONodeStates, nicht Teil des Patches). Aufgerufen
        vom AudioDeviceController bei Start und jedem Gerätewechsel.
        Message Thread. */
    void syncHardwareIOChannels (int deviceInputs, int deviceOutputs);

    [[nodiscard]] GraphManager& getGraphManager() noexcept;
    [[nodiscard]] NodeUiRegistry& getNodeUiRegistry() noexcept;

    /** Statische Modul-Metadaten (Descriptors) fürs Browser-Panel. */
    [[nodiscard]] ModuleFactory& getModuleFactory() noexcept;
    [[nodiscard]] OscController& getOscController() noexcept;
    [[nodiscard]] LinkClock& getLinkClock() noexcept;

    /** Takt-Anker der Session-Beat-Achse (Looper B1): lookup() und
        latestBoundaryBar() sind von jedem Thread lesbar — der Looper-Commit
        [Message Thread] adressiert damit vergangene Takte sample-exakt im
        Capture-Ring. */
    [[nodiscard]] const BarSampleAnchors& getBarAnchors() const noexcept;
    [[nodiscard]] const CaptureService& getCaptureService() const noexcept;
    [[nodiscard]] CaptureService& getCaptureService() noexcept;  // UI: ChangeListener (RAM-Warnung)
    [[nodiscard]] CaptureSettings& getCaptureSettings() noexcept;
    [[nodiscard]] ChannelNames& getChannelNames() noexcept;

    /** Sicht-Metering der Hardware-I/O (Ableton-Style, CLAUDE.md 10) — die
        audio_in/audio_out-Kacheln lesen Peak/RMS/Clip pro Kanal. Owner:
        EngineProcessor; Werte werden lock-free im processBlock publiziert. */
    [[nodiscard]] LevelMeter& getInputLevels() noexcept;
    [[nodiscard]] LevelMeter& getOutputLevels() noexcept;

    /** Einstellungen der Pegelanzeigen (Clip-Reset-Modus) — App-Zustand,
        von der Settings-UI editiert; der EngineProcessor speist daraus die
        LevelMeter (Auto-Clear). */
    [[nodiscard]] MeterSettings& getMeterSettings() noexcept;

    /** Eingebetteter Link-Send des Hardware-Eingangs (7.2) — die Send-UI
        an den audio_in-Kanal-Zeilen liest den Status daraus. */
    [[nodiscard]] InputLinkSend& getInputLinkSend() noexcept;

    /** Transport-/Link-Einstellungen des Push-Headers (Start/Stop-Sync,
        Clock-Offset, Looper-Toggles) — App-Zustand; der EngineProcessor
        lauscht und speist die LinkClock (applyTransportSettings). */
    [[nodiscard]] TransportSettings& getTransportSettings() noexcept;

    /** Looper-Page-Zustand (M5): Struktur, Quellen, Mixer, Menü-Optionen —
        App-Zustand in eigener Datei (Conduit/Looper.settings); der
        EngineProcessor lauscht und spiegelt in Bank/Modell
        (applyLooperSettings). */
    [[nodiscard]] LooperSettings& getLooperSettings() noexcept { return looperSettings; }

    /** MIDI-Rig-Registry (ADR 006 M1): registrierte Klangerzeuger-/
        Controller-Geräte, App-Zustand in eigener Datei
        (Conduit/MidiRig.settings). Noch ohne UI (folgt M3). */
    [[nodiscard]] MidiRigSettings& getMidiRigSettings() noexcept { return midiRigSettings; }

    /** MIDI-Rig-Hub (ADR 006 M1): besitzt die offenen MIDI-Ports der
        Registry (Queues pro Port, 60-Hz-Drain, Fassaden). */
    [[nodiscard]] MidiPortHub& getMidiPortHub() noexcept { return midiPortHub; }

    /** Klangerzeuger-Profile (ADR 006 E1, M2): Factory-CSVs + User-Ordner
        Conduit/Devices — Macro-Panel-Hardware-Picker + MIDI-Settings-
        Report binden daran. */
    [[nodiscard]] MidiProfileLibrary& getMidiProfileLibrary() noexcept { return midiProfileLibrary; }

    /** Controller-Profile (ADR 006 E2, M4): Factory-Profil (Xone:K1) +
        User-Ordner Conduit/Controllers -- MIDI-Settings-Picker/Report und
        GridPage::onFeedbackEcho binden daran. */
    [[nodiscard]] ControllerProfileLibrary& getControllerProfileLibrary() noexcept { return controllerProfileLibrary; }

    //==========================================================================
    /** Looper-Quelle (B3/M4) [Message Thread]: Quell-Schlüssel
        ("master" | "hw:{paar}" | "out:{paar}" | "tap:{name}") PRO LOOPER auflösen und
        armen. Das Arming ist refcount-artig über die VEREINIGUNG aller
        Looper-Quellen: teilen sich zwei Looper eine Quelle, bleibt das
        Gate offen, bis der letzte sie verlässt. Looper 0 persistiert
        weiterhin in den TransportSettings (Migration → LooperSettings M5).
        Re-Apply nach prepareToPlay übernimmt der Processor selbst. */
    void setLooperSource (int looperIndex, const juce::String& sourceKey);
    void setLooperSource (const juce::String& sourceKey) { setLooperSource (0, sourceKey); }

    /** Aufgelöste Capture-Indizes der Looper-Quellen (links/rechts; Mono =
        beide gleich, −1 = nicht auflösbar). Message Thread. */
    [[nodiscard]] int getLooperLeftIndex (int looperIndex = 0) const noexcept
    {
        return looperIndex >= 0 && looperIndex < LooperBank::maxLoopers
             ? looperLeftIndex[static_cast<std::size_t> (looperIndex)] : -1;
    }
    [[nodiscard]] int getLooperRightIndex (int looperIndex = 0) const noexcept
    {
        return looperIndex >= 0 && looperIndex < LooperBank::maxLoopers
             ? looperRightIndex[static_cast<std::size_t> (looperIndex)] : -1;
    }

    /** Waveform-Datenpfad der Looper-Page (B4/M4): ein Tap pro Looper —
        jeder Strip holt die Bins seines Taps per pop() ab
        (Konsumentenrolle exklusiv pro Strip). */
    [[nodiscard]] LooperWaveformTap& getLooperWaveformTap (int looperIndex = 0) noexcept
    {
        return looperWaveformTaps[static_cast<std::size_t> (
            juce::jlimit (0, LooperBank::maxLoopers - 1, looperIndex))];
    }

    /** Looper-Commit (B5) [Message Thread]: Paritäts-Pfad des alten UI —
        committet auf Looper 0/Track 0 (ersetzt dessen Loop). */
    [[nodiscard]] juce::Result commitLooper (int bars);

    /** Slot-Modell-Commit (M4): in den Target-Slot des Loopers, über das
        LooperSessionModel — Pfad der neuen Page (M6) und der OSC-Actions
        (M8). */
    [[nodiscard]] juce::Result commitToTarget (int looperIndex, int bars);

    /** Session-Modell (Slots/Target/Aktiv-Clip) — UI/OSC binden hier an. */
    [[nodiscard]] LooperSessionModel& getLooperSession() noexcept { return looperSession; }

    /** [Message Thread] Loop-Playback mit 5-ms-Fade beenden. */
    void stopLooper() noexcept { looperBank.stopAll(); }

    /** Ausgabe-Paar des Loop-Playbacks (B6) [Message Thread]: persistiert
        looperAnchor (Clamp in den Settings) und routet die Engine sofort
        auf die Kanäle 2n/2n+1. */
    void setLooperAnchor (int pairIndex);

    /** Status fürs UI (Tape-LED, Stop-Kachel, Statuszeile) — außerdem
        Ziel des serviceMessageThread()-Aufrufs im Editor-Timer
        (Retire-Quittungen der Bank, LooperBank-Doku). */
    [[nodiscard]] LooperBank& getLooperBank() noexcept { return looperBank; }

    /** Callback-Timing-Diagnose (XRuns/Load) für die Dev-Modus-Anzeige. */
    [[nodiscard]] CallbackTimingMonitor& getTimingMonitor() noexcept { return timingMonitor; }

    /** OSC-Send-Pfad (7.3): Ziel-Host/Port/Enable (App-Zustand, Settings-UI)
        und der Snapshot-Diff-Sender selbst. */
    [[nodiscard]] OscSendSettings& getOscSendSettings() noexcept;
    [[nodiscard]] OscSendService& getOscSendService() noexcept;

    /** Oberflächen-Einstellungen (UI-Skalierung, Schriftgröße, Dev Mode) —
        App-Zustand; der Processor lauscht NICHT darauf, die Anwendung
        übernehmen Main.cpp und der EngineEditor. */
    [[nodiscard]] UiSettings& getUiSettings() noexcept;

    /** Grid-Voice-Kette (M1 Teil 3, CLAUDE.md 14 ADR Grid-Page): reine
        Message-Thread-Logik (ITouchMacro, 4.2) — NIE vom Audio-Thread
        aufrufen. GridKeyboardComponent ruft die Engine direkt; MIDI-Ports
        (MPE-Out, Controller-In, Noten-Echo) besitzt der MidiPortHub
        (ADR 006 M1b), die Grid-Page abonniert dort. */
    [[nodiscard]] grid::GridVoiceEngine& getGridVoiceEngine() noexcept { return gridVoiceEngine; }
    /** Block D1 (Settings-Tab Expression Mode): direkter Zugriff, da
        IVoiceSink bewusst keine MPE-Spezifika kennt (setExpressionMode
        ist MpeMidiSink-spezifisch, kein Interface-Mitglied). */
    [[nodiscard]] grid::MpeMidiSink& getMpeMidiSink() noexcept { return mpeMidiSink; }

    /** Chrome-Zustand des rechten Editor-Dock-Panels der Grid-Page (S2,
        App-Zustand) — der Processor lauscht nicht darauf, GridPage lädt/
        speichert direkt (Muster getUiSettings). */
    [[nodiscard]] GridPanelSettings& getGridPanelSettings() noexcept { return gridPanelSettings; }

    /** TouchLive (Ableton-Live-Remote, docs/TouchLive.md M1b/M1c): reine
        Message-Thread-Objekte, der Audio-Thread ist NIE beteiligt. Die
        TouchLive-Page bindet an Modell + Client + Settings. */
    [[nodiscard]] TouchLiveSettings& getTouchLiveSettings() noexcept { return touchLiveSettings; }
    [[nodiscard]] LiveSetModel& getLiveSetModel() noexcept { return liveSetModel; }
    [[nodiscard]] TouchLiveMeterBus& getTouchLiveMeterBus() noexcept { return touchLiveMeterBus; }
    [[nodiscard]] TouchLiveClient& getTouchLiveClient() noexcept { return touchLiveClient; }
    [[nodiscard]] LiveSpectrumTap& getLiveSpectrumTap() noexcept { return liveSpectrumTap; }

private:
    /** Legt die reservierten I/O-Tree-Nodes (audio_input/audio_output) an,
        falls sie fehlen — frischer Patch oder Preset ohne I/O. Idempotent. */
    void ensureIONodeStates();

    /** Schritt C: entfernt Kabel, die einen jetzt verschwundenen I/O-Kanal
        referenzieren (Kanal >= validChannels), wenn ein kleineres Interface
        gewählt/ausgesteckt wird. asSource = der Endpunkt ist die Quelle
        (audio_in); sonst das Ziel (audio_out). Geräte-getrieben → kein Undo
        (Umgebungs-Zustand, nicht Teil des Patches, verhindert Phantom-
        Connections beim Preset-Save). Message Thread. */
    void pruneEndpointConnections (const juce::String& nodeId, bool asSource, int validChannels);

    /** Default-Properties der globalen Session-Skala (6.2) + Atomics-Refresh. */
    void ensureSessionScaleDefaults();
    void refreshScaleAtomics();

    // juce::ValueTree::Listener [Message Thread] — nur die Skalen-Properties
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    // juce::ChangeListener [Message Thread] — MeterSettings → LevelMeter,
    // ChannelNames → Input-Link-Sends (Enable/Pairing/Labels)
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void applyMeterSettings();
    void applyTransportSettings();  // TransportSettings → LinkClock
    void applyLooperSettings();     // LooperSettings → Bank/Modell/Arming (M5)

    /** Looper-Quelle (B3): Schlüssel aus den TransportSettings in
        Capture-Indizes auflösen und Arming nachziehen — bei Quellwahl und
        nach jedem prepareToPlay (Puffersatz-Swap ändert die Tap-Indizes). */
    void applyLooperSourceArming();

    /** Zieht die Input-Link-Sends diff-basiert aus dem ChannelNames-Zustand
        nach (Enable-Flags, Pairing, Labels) — bei jedem ChannelNames-
        Broadcast und nach syncHardwareIOChannels (Kanal-Schrumpfen retired).
        Message Thread. */
    void rebuildInputSends();

    juce::ValueTree rootState;
    juce::UndoManager undoManager;

    // Globale Skala + Session-Swing: Message Thread schreibt, Audio Thread
    // liest (→ ClockBus)
    std::atomic<int>    scaleRootAtomic { 0 };
    std::atomic<int>    scaleTypeAtomic { 0 };
    std::atomic<double> globalSwingAtomic { 0.0 };

    // true, solange processBlock() gerade läuft (Eintritt/Austritt) — Guard
    // für releaseResources(): der SPSC-Consumer-Wechsel der oscToAudioQueue
    // auf den Message Thread ist nur bei gestopptem Callback zulässig.
    std::atomic<bool> audioCallbackActive { false };

    // Clock-Master (Ableton-Link-Session) + Takt-Verteiler (4.2):
    // clockBus wird einmal pro Block auf dem Audio Thread gefüllt,
    // IClockSlaves lesen im selben Callback.
    // VOR dem Graph deklariert — Module im Graph halten Link-Audio-Sinks
    // (7.2), die LinkClock muss deshalb die Graph-Destruktion überleben.
    LinkClock linkClock;
    ClockBus clockBus;

    // Capture VOR dem Graph deklariert — CaptureTapModule-Instanzen im Graph
    // halten einen CaptureService-Pointer (ICaptureTapClient) und
    // deregistrieren sich im Destruktor; der Service muss die
    // Graph-Destruktion deshalb überleben (gleiche Lektion wie linkClock).
    // Settings: App-Zustand via ApplicationProperties, KEIN Patch-Zustand —
    // loadPreset() ersetzt den Root-Tree, Capture bleibt unberührt
    // (gleiche Trennung wie das Link-Tempo, siehe EngineEditor-Doku).
    CaptureSettings captureSettings;

    // Kanal-Namen der Hardware-I/O — App-Zustand wie die CaptureSettings
    // (eigene Settings-Datei, überlebt Preset-Load, kein Undo). Main.cpp
    // setzt den aktiven Device-Kontext nach initAudio().
    ChannelNames channelNames;

    // Eingebetteter Link-Send des Hardware-Eingangs (7.2). NACH der
    // linkClock deklariert — hält Sinks, die vor der Clock sterben müssen;
    // der Zustand (Enable/Pairing/Namen) kommt aus channelNames.
    InputLinkSend inputLinkSend;

    // Capture-Fundament (SampleClock + Input-Metering + Ring-Allokation):
    // processInputTap() läuft als ERSTE Operation in processBlock auf dem
    // rohen Hardware-Input. Nach den Settings deklariert (Konstruktor-Ref);
    // die Host-Verdrahtung für die Resize-Policy passiert im Konstruktor.
    CaptureService captureService { captureSettings };

    // Master-Output-Tap (Looper B2): Registry-Slots der Session-Summe
    // ("master_l"/"master_r"), im Konstruktor registriert und danach
    // unveränderlich — der Audio Thread liest sie deshalb direkt
    CaptureService::VirtualChannelHandle masterTapLeft;
    CaptureService::VirtualChannelHandle masterTapRight;

    // Ausgangs-Paar-Taps (Looper-Quellen "out:{paar}", 08.07.2026): Paare
    // 1..N hinter dem Master (Kanäle 2p/2p+1), Namen "out{p}_l/_r" — in
    // prepareToPlay an die Device-Kanalzahl angeglichen (Audio steht,
    // danach bis zum nächsten prepare unveränderlich → Audio Thread liest
    // direkt, Muster masterTap). Damit sind auch Signale loopbar, die nur
    // auf einem Ausgangspaar liegen (z. B. Link-Receive-Routings).
    static constexpr int maxOutputTapPairs = 7;
    std::array<CaptureService::VirtualChannelHandle,
               static_cast<std::size_t> (maxOutputTapPairs) * 2> outputTapHandles;
    int numOutputTapPairs = 0;

    // Looper-Quelle (B3): aufgelöste Capture-Indizes der gearmten Kanäle
    // (nur Message Thread; −1 = nicht auflösbar, z. B. vor prepare)
    std::array<int, static_cast<std::size_t> (LooperBank::maxLoopers)> looperLeftIndex {
        -1, -1, -1, -1 };
    std::array<int, static_cast<std::size_t> (LooperBank::maxLoopers)> looperRightIndex {
        -1, -1, -1, -1 };

    // Zuletzt gearmter Kanalsatz (Vereinigung aller Looper-Quellen) —
    // Basis des Diff beim Re-Arming (geteilte Quellen bleiben offen)
    std::vector<int> looperArmedIndices;

    // Waveform-Binner der Looper-Page (B4): läuft am Block-Ende nach dem
    // Master-Tap-Write; die Quelle speist applyLooperSourceArming
    std::array<LooperWaveformTap, static_cast<std::size_t> (LooperBank::maxLoopers)>
        looperWaveformTaps;

    // Loop-Playback (B5): nach Master-Tap/Binner, vor dem Metronom auf
    // das Anker-Paar der TransportSettings (looperAnchor)
    LooperBank looperBank;

    // Slot-/Target-Modell über der Bank (M4) — reiner MT-Zustand
    LooperSessionModel looperSession { looperBank };

    // Callback-Timing-Diagnose (Dev-Modus): XRun-/Load-Messung um den
    // gesamten processBlock — begin als erste, end als letzte Operation
    CallbackTimingMonitor timingMonitor;

    // Sicht-Metering (Ableton-Style) für die audio_in/audio_out-Kacheln —
    // getrennt vom capture-InputMeter; processBlock speist beide.
    LevelMeter inputLevels;
    LevelMeter outputLevels;

    // Clip-Reset-Modus der Pegelanzeigen (App-Zustand); speist die LevelMeter
    MeterSettings meterSettings;

    // Oberflächen-Einstellungen (App-Zustand) — reines UI-Anliegen
    UiSettings uiSettings;

    // Chrome-Zustand des Grid-Editor-Dock-Panels (S2, App-Zustand)
    GridPanelSettings gridPanelSettings;

    // Modul-Typ-Defaults des Dev-Modus (4.6, App-Zustand) — der GraphManager
    // wendet sie bei Neu-Anlagen als Overlay an
    ModuleUiDefaults moduleUiDefaults;

    // Transport-/Link-Einstellungen des Push-Headers (App-Zustand); der
    // ChangeListener speist LinkClock (Start/Stop-Sync, Clock-Offset) und
    // Metronom (Enable, Ziel-Anker)
    TransportSettings transportSettings;

    // Looper-Page-Zustand (M5): Struktur/Quellen/Mixer/Menü-Optionen in
    // eigener Datei; Quell-Schlüssel aller Looper leben HIER
    LooperSettings looperSettings;

    // MIDI-Rig-Registry (ADR 006 M1) — App-Zustand in eigener Datei;
    // der Hub braucht eine Referenz, deshalb VOR midiPortHub deklariert.
    // Die Profile-Bibliothek (M2) leitet ihren User-Ordner aus der
    // Settings-Datei ab (Conduit/Devices — folgt Test-Redirects).
    MidiRigSettings midiRigSettings;
    MidiPortHub midiPortHub { midiRigSettings };
    MidiProfileLibrary midiProfileLibrary { midiRigSettings.settingsFile().getSiblingFile ("Devices") };

    // Controller-Profile (M4) -- gleiches Muster, eigener Nachbar-Ordner
    // Conduit/Controllers (ADR 006 E2, flach statt {Hersteller}/-verschachtelt).
    ControllerProfileLibrary controllerProfileLibrary { midiRigSettings.settingsFile().getSiblingFile ("Controllers") };

    // Link-synchroner Click — läuft nach dem GraphFader auf die Anker-Kanäle
    Metronome metronome;

    // Sample-genaue Takt-Anker der Beat-Achse (Looper B1): der Audio Thread
    // ankert Takt-Überquerungen direkt nach dem ClockBus-Fill; der
    // Looper-Commit liest sie vom Message Thread (Atomics-Ring, lock-free)
    BarSampleAnchors barAnchors;

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr audioInputNode;
    juce::AudioProcessorGraph::Node::Ptr audioOutputNode;

    // Master-Fade für glitch-freie Graph-Swaps (CLAUDE.md 5.2)
    GraphFader graphFader;

    // Default-Module werden im Konstruktor registriert
    ModuleFactory moduleFactory;

    // Zombie-UI-Schutz für das zweiphasige Delete (CLAUDE.md 5.3)
    NodeUiRegistry nodeUiRegistry;

    // Nach allen Abhängigkeiten deklariert — Initialisierungsreihenfolge!
    GraphManager graphManager { rootState, graph, graphFader,
                                moduleFactory, undoManager, nodeUiRegistry };

    // OSC-Send-Pfad (7.3): Snapshot-Diff-Timer auf dem Message Thread.
    // VOR dem OscController deklariert — dessen onRemoteValueApplied-Callback
    // referenziert den Service, der Controller muss zuerst sterben.
    OscSendSettings oscSendSettings;
    OscSendService oscSendService { rootState, oscSendSettings };

    // Announce-Bindung (7.4): find-or-create über remoteId — ebenfalls VOR
    // dem OscController (dessen onAnnounce-Callback referenziert den Binder)
    RemoteModuleBinder remoteModuleBinder { rootState, graphManager, moduleFactory };

    // OSC-Dual-State (CLAUDE.md 6.1): Netzwerk → Audio (lock-free) und
    // Netzwerk → ValueTree (async). Nach dem GraphManager deklariert —
    // der OscController löst Endpoints über ihn auf und wird zuerst zerstört.
    SpscQueue<ParameterUpdate> oscToAudioQueue { 1024 };
    OscController oscController { rootState, graphManager, oscToAudioQueue };

    // Grid-Voice-Kette (M1 Teil 3): reine Message-Thread-Logik, vom Audio-
    // Graph unabhängig (kein processBlock-Zugriff, CLAUDE.md 4.2 ITouchMacro).
    // Der MPE-Ausgang läuft über die Rollen-Fassade des MidiPortHub
    // (ADR 006 M1b) — sie löst das Grid-Ausgangs-Gerät bei jedem send()
    // live aus der Registry auf; der Hub ist weiter oben deklariert und
    // stirbt daher NACH dem Sink.
    grid::MpeMidiSink      mpeMidiSink      { midiPortHub.gridOutputTarget() };
    grid::GridVoiceEngine  gridVoiceEngine  { mpeMidiSink };

    // TouchLive-Remote (docs/TouchLive.md): Message-Thread-only, vom
    // Audio-Graph unabhängig. Settings + Modell + MeterBus VOR dem Client
    // (nimmt sie als Referenz); Client startet nur, wenn Settings enabled.
    TouchLiveSettings touchLiveSettings;
    LiveSetModel liveSetModel;
    TouchLiveMeterBus touchLiveMeterBus;
    TouchLiveClient touchLiveClient { liveSetModel, touchLiveMeterBus, touchLiveSettings };

    // Live-Remote-Bridge (07/2026): AlphaTrack als Ableton-Fernbedienung --
    // headless, Message Thread. NACH touchLiveClient deklariert (Seams
    // referenzieren den Client, die Bridge stirbt zuerst); Hub/Registry/
    // Profile stehen weiter oben. Seam-Verdrahtung in initLiveRemoteBridge()
    // (EngineProcessor-Ctor).
    LiveRemoteBridge liveRemoteBridge { midiPortHub, midiRigSettings,
                                        controllerProfileLibrary, liveSetModel };

    // Spektrum-Zubringer der EQ-Anzeige (§10k): hält die LinkClock als
    // WeakReference, stirbt VOR ihr (nach linkClock deklariert);
    // processBlock speist ihn nur im audioInput-Modus (atomic-gated)
    LiveSpectrumTap liveSpectrumTap { &linkClock };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineProcessor)
};

} // namespace conduit

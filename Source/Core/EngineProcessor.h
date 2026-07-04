#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Capture/CaptureService.h"
#include "Capture/LevelMeter.h"
#include "ChannelNames.h"
#include "Looper/BarSampleAnchors.h"
#include "Looper/LooperEngine.h"
#include "Looper/LooperWaveformTap.h"
#include "Metronome.h"
#include "GraphFader.h"
#include "GraphManager.h"
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

    //==========================================================================
    /** Looper-Quelle (B3) [Message Thread]: persistiert den Quell-Schlüssel
        ("master" | "hw:{paar}" | "tap:{name}") in den TransportSettings,
        löst ihn in Capture-Indizes auf und armt die Kanäle (Vorgänger
        entwaffnet) — das Gate der Quelle bleibt damit garantiert offen (B1).
        Re-Apply nach prepareToPlay (Puffersatz-Indizes) übernimmt der
        Processor selbst. */
    void setLooperSource (const juce::String& sourceKey);

    /** Aufgelöste Capture-Indizes der Looper-Quelle (links/rechts; Mono =
        beide gleich, −1 = nicht auflösbar) — Waveform-Tap (B4) und
        Commit (B5) lesen sie; Tests prüfen das Arming. Message Thread. */
    [[nodiscard]] int getLooperLeftIndex() const noexcept  { return looperLeftIndex; }
    [[nodiscard]] int getLooperRightIndex() const noexcept { return looperRightIndex; }

    /** Waveform-Datenpfad der Looper-Page (B4): der LooperWaveformStrip
        holt die Bins per pop() ab (Konsumentenrolle exklusiv). */
    [[nodiscard]] LooperWaveformTap& getLooperWaveformTap() noexcept { return looperWaveformTap; }

    /** Looper-Commit (B5) [Message Thread]: die letzten `bars` kompletten
        Takte der gewählten Quelle committen und sofort phasenstarr
        abspielen — Fehlermeldung für den Toast bei fail. */
    [[nodiscard]] juce::Result commitLooper (int bars);

    /** [Message Thread] Loop-Playback mit 5-ms-Fade beenden. */
    void stopLooper() noexcept { looperEngine.stop(); }

    /** Ausgabe-Paar des Loop-Playbacks (B6) [Message Thread]: persistiert
        looperAnchor (Clamp in den Settings) und routet die Engine sofort
        auf die Kanäle 2n/2n+1. */
    void setLooperAnchor (int pairIndex);

    /** Status fürs UI (Tape-LED, Stop-Kachel, Statuszeile). */
    [[nodiscard]] const LooperEngine& getLooperEngine() const noexcept { return looperEngine; }

    /** OSC-Send-Pfad (7.3): Ziel-Host/Port/Enable (App-Zustand, Settings-UI)
        und der Snapshot-Diff-Sender selbst. */
    [[nodiscard]] OscSendSettings& getOscSendSettings() noexcept;
    [[nodiscard]] OscSendService& getOscSendService() noexcept;

    /** Oberflächen-Einstellungen (UI-Skalierung, Schriftgröße, Dev Mode) —
        App-Zustand; der Processor lauscht NICHT darauf, die Anwendung
        übernehmen Main.cpp und der EngineEditor. */
    [[nodiscard]] UiSettings& getUiSettings() noexcept;

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

    // Looper-Quelle (B3): aufgelöste Capture-Indizes der gearmten Kanäle
    // (nur Message Thread; −1 = nicht auflösbar, z. B. vor prepare)
    int looperLeftIndex  = -1;
    int looperRightIndex = -1;

    // Waveform-Binner der Looper-Page (B4): läuft am Block-Ende nach dem
    // Master-Tap-Write; die Quelle speist applyLooperSourceArming
    LooperWaveformTap looperWaveformTap;

    // Loop-Playback (B5): nach Master-Tap/Binner, vor dem Metronom auf
    // das Anker-Paar der TransportSettings (looperAnchor)
    LooperEngine looperEngine;

    // Sicht-Metering (Ableton-Style) für die audio_in/audio_out-Kacheln —
    // getrennt vom capture-InputMeter; processBlock speist beide.
    LevelMeter inputLevels;
    LevelMeter outputLevels;

    // Clip-Reset-Modus der Pegelanzeigen (App-Zustand); speist die LevelMeter
    MeterSettings meterSettings;

    // Oberflächen-Einstellungen (App-Zustand) — reines UI-Anliegen
    UiSettings uiSettings;

    // Modul-Typ-Defaults des Dev-Modus (4.6, App-Zustand) — der GraphManager
    // wendet sie bei Neu-Anlagen als Overlay an
    ModuleUiDefaults moduleUiDefaults;

    // Transport-/Link-Einstellungen des Push-Headers (App-Zustand); der
    // ChangeListener speist LinkClock (Start/Stop-Sync, Clock-Offset) und
    // Metronom (Enable, Ziel-Anker)
    TransportSettings transportSettings;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineProcessor)
};

} // namespace conduit

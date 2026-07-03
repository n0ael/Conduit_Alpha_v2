#pragma once

#include <functional>
#include <map>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "GraphFader.h"
#include "Interfaces/IClockSource.h"
#include "Modules/ConduitModule.h"

namespace conduit
{

class CaptureService;
class ChannelNames;
class LinkClock;
class ModuleFactory;
class ModuleUiDefaults;
class NodeUiRegistry;
class ProcessorModule;

//==============================================================================
/**
    Koordiniert Topologie-Änderungen des AudioProcessorGraph (CLAUDE.md 5).

    Batch-Coalescing (5.5): alle topologie-relevanten ValueTree-Änderungen
    eines Frames werden via AsyncUpdater gesammelt und zu einem einzigen
    Graph-Swap zusammengefasst — gilt für Undo, Redo, Preset-Load,
    Bulk-Delete und Copy-Paste.

    Ablauf pro Swap (5.2):
      Schritt 1: Async Prepare — neue Module via ModuleFactory instanziieren
                 und prepareForGraph() VOR dem Fade-Out abschließen;
                 Fehler → nodeError im ValueTree, kein Retry-Loop
      Schritt 2: fader.beginFadeOut() — Topologie bleibt unverändert
      Schritt 3: Self-Re-Dispatch via triggerAsyncUpdate() bis der Audio
                 Thread Stille meldet (kein Busy-Poll, kein Timer), dann
                 Tree → Graph synchronisieren: verschwundene Nodes entfernen,
                 vorbereitete Module einfügen, Connections abgleichen
      Schritt 4: fader.beginFadeIn()
    Änderungen, die während des Fade-Outs eintreffen, landen im selben Swap.
    Läuft Audio nicht (Fader unprepared), wird ohne Fade direkt geswappt.

    Zweiphasiges Delete (5.3):
      Phase 1: requestNodeDelete() setzt nodeState → Deleting. UI-Components
               reagieren über ihren Listener, entkoppeln sich und geben ihre
               NodeUiRegistry-Referenz frei; moduleId wird sofort gecacht
               (der künftige OscController deregistriert darüber, 7.1)
      Phase 2: im nächsten Loop-Durchlauf, erst wenn der UI-Refcount 0 ist —
               Subtree + zugehörige Kabel werden in einer UndoManager-
               Transaktion entfernt; die Graph-Entfernung folgt über den
               normalen Fade-Swap

    Die I/O-Nodes des EngineProcessor sind nicht tree-verwaltet und bleiben
    von der Synchronisation unangetastet.

    Parameter-Wert-Änderungen (OSC-Dual-State, 6.1) lösen bewusst KEINEN
    Rebuild aus — nur Child-Add/Remove unter Nodes[]/Connections[] sowie
    der Austausch ganzer Container (Preset-Load) markieren das Delta.

    Läuft vollständig auf dem Message Thread — daher ist topologyDirty
    bewusst kein std::atomic. Kein UI-Code, kein DSP-Code (der Fade selbst
    lebt im GraphFader auf dem Audio Thread).
*/
class GraphManager final : private juce::ValueTree::Listener,
                           private juce::AsyncUpdater
{
public:
    /** rootTree ist ein ref-counted ValueTree-Handle, processorGraph das
        Swap-Ziel, faderToUse der Master-Fade im Audio-Pfad, factoryToUse
        erzeugt Module aus persistierten moduleIds, undoManagerToUse macht
        Deletes undo-fähig, uiRegistryToUse liefert den Zombie-UI-Schutz.
        Registriert sich als Listener. */
    GraphManager (juce::ValueTree rootTree,
                  juce::AudioProcessorGraph& processorGraph,
                  GraphFader& faderToUse,
                  ModuleFactory& factoryToUse,
                  juce::UndoManager& undoManagerToUse,
                  NodeUiRegistry& uiRegistryToUse);

    /** Deregistriert den Listener und verwirft ausstehende Async-Updates. */
    ~GraphManager() override;

    //==========================================================================
    /** Patch-Aktion: erzeugt das Modul über die Factory, baut seinen State
        (createState VOR addNode, 4.4) und hängt ihn in einer eigenen
        UndoManager-Transaktion an Nodes[] — die Materialisierung folgt über
        den normalen Swap. Invalides ValueTree bei unbekannter moduleId.

        configure (optional) läuft nach createState() und vor dem Einhängen —
        für Module mit Anlege-Konfiguration (z.B. Multi-Input Link Audio Send:
        Eingangszahl/Modi). Message Thread. */
    juce::ValueTree addModuleNode (const juce::String& factoryKey, juce::Point<int> position,
                                   const std::function<void (juce::ValueTree&)>& configure = {});

    /** Patch-Aktion: benennt die named_id (OSC-Pfad, UI-Titel) eines Nodes
        undo-fähig um. Der Name wird OSC-pfadtauglich saniert (lowercase,
        [a-z0-9_]); false bei leerem Ergebnis, vergebenem Namen oder
        unbekanntem Node. Message Thread. */
    bool renameNode (const juce::String& nodeUuid, const juce::String& requestedName);

    /** Patch-Aktion (FX-Chassis 4.6): Link-Send-Tap eines Processor-Nodes
        an/aus — undo-fähig; der Property-Listener leitet live ans Modul
        weiter (create/retire ohne Rebuild). false bei unbekanntem Node.
        Message Thread. */
    bool setLinkSendEnabled (const juce::String& nodeUuid, bool enabled);

    /** Patch-Aktion (Dev-Modus 4.6): User-Regelbereich eines Parameters —
        Fader nutzt [userMin, userMax], der DSP clamped weiter auf die
        Hard-Range. Validiert userMin < userMax innerhalb der Hard-Range;
        der aktuelle Wert wird in derselben Undo-Transaktion in den neuen
        Bereich geclamped. Message Thread. */
    bool setParameterUserRange (const juce::String& nodeUuid, const juce::String& paramId,
                                double userMin, double userMax);

    /** Patch-Aktion (Dev-Modus 4.6): blendet einen dsp-Parameter aus/ein.
        Ausblenden trennt bestehende CV-Kabel des Parameters in DERSELBEN
        Undo-Transaktion (keine Phantom-Modulation); das Bus-Layout /
        numInputChannels bleibt IMMER unverändert. Message Thread. */
    bool setParameterHidden (const juce::String& nodeUuid, const juce::String& paramId,
                             bool hidden);

    /** Patch-Aktion (Dev-Modus 4.6): Fader-Response-Kurve eines dsp-
        Parameters — "x1 y1 x2 y2" (Bezier, ChassisSchema) oder leer für
        linear (Property entfernt). Undo-fähig; reines UI-Mapping, DSP/OSC
        unberührt. false bei unbekanntem/nicht-dsp-Parameter oder
        unlesbarem Kurven-String. Message Thread. */
    bool setParameterCurve (const juce::String& nodeUuid, const juce::String& paramId,
                            const juce::String& curveText);

    /** Dev-Modus 4.6: aktuelle dsp-Overrides des Nodes (userMin/userMax/
        uiHidden/curve) als Modul-Typ-Defaults sichern — greift bei künftigen
        Neu-Anlagen dieses factoryIds. false ohne ModuleUiDefaults/Node. */
    bool captureModuleUiDefaults (const juce::String& nodeUuid);

    /** Phase 1 des zweiphasigen Deletes (5.3): setzt nodeState → Deleting.
        false, wenn kein Node mit dieser nodeId existiert oder der Node ein
        externer Endpunkt ist (I/O-Nodes sind nicht löschbar). Message Thread. */
    [[nodiscard]] bool requestNodeDelete (const juce::String& nodeUuid);

    //==========================================================================
    /** Patch-Aktion: legt ein Kabel als Connection-Child an (Schema 6.2),
        undo-fähig. false bei unbekanntem Endpunkt, Selbstverbindung oder
        Duplikat. Die Kanal-Validierung übernimmt der Graph beim Swap. */
    bool addConnection (const juce::String& sourceUuid, int sourceChannel,
                        const juce::String& destUuid, int destChannel);

    /** Patch-Aktion: entfernt das passende Kabel undo-fähig.
        false, wenn keines existiert. */
    bool removeConnection (const juce::String& sourceUuid, int sourceChannel,
                           const juce::String& destUuid, int destChannel);

    /** Stereo-Paar-Kabel in EINER Undo-Transaktion: (sourceChannel →
        destChannel) plus (sourceChannel+1 → destChannel+1). Das zweite Kabel
        entsteht nur, wenn destChannel+1 am Ziel existiert (numInputChannels)
        und die Verbindung noch frei ist — sonst bleibt nur das erste
        (Mono-Fallback, dokumentierter Randfall). false, wenn schon das
        erste Kabel ungültig wäre. */
    bool addConnectionPair (const juce::String& sourceUuid, int sourceChannel,
                            const juce::String& destUuid, int destChannel);

    /** Trennt beide Kabel eines Stereo-Paars in EINER Undo-Transaktion
        (das zweite nur, falls vorhanden). false, wenn keines existiert. */
    bool removeConnectionPair (const juce::String& sourceUuid, int sourceChannelA,
                               const juce::String& destUuid, int destChannelA,
                               int sourceChannelB, int destChannelB);

    //==========================================================================
    /** Mappt eine reservierte moduleId (audio_input/audio_output) auf einen
        extern verwalteten Graph-Node des EngineProcessor. Tree-Nodes mit
        dieser moduleId werden nicht factory-materialisiert, nicht gelöscht
        und ihr Graph-Node beim Verschwinden nicht entfernt. */
    void registerExternalEndpoint (const juce::String& moduleId,
                                   juce::AudioProcessorGraph::NodeID graphNodeId);

    [[nodiscard]] bool isExternalEndpoint (const juce::String& moduleId) const noexcept;

    /** Factory-Schlüssel mit Migrations-Fallback: alte Bestände (vor der
        factoryId/moduleId-Trennung) tragen den Schlüssel in moduleId.
        Public — auch die UI unterscheidet Modultypen darüber. */
    [[nodiscard]] static juce::String factoryKeyOf (const juce::ValueTree& nodeTree);

    //==========================================================================
    /** Takt-Verteiler für IClockSlave-Module — wird bei der Materialisierung
        injiziert (5.2 Schritt 1, vor der Graph-Aufnahme). Der Bus muss jedes
        Modul überdauern (Owner: EngineProcessor). nullptr → Slaves laufen
        frei (Tests). Message Thread, vor den ersten addModuleNode-Aufrufen. */
    void setClockBus (const ClockBus* bus) noexcept;

    /** Link-Audio-Kontext für ILinkAudioClient-Module (7.2) — wird bei der
        Materialisierung VOR prepareForGraph injiziert (der Sink entsteht in
        prepareToPlay und braucht Clock + moduleId). Die Clock muss jedes
        Modul überdauern (Owner: EngineProcessor). nullptr → Module bleiben
        ohne Session-Kanal (Tests). Message Thread. */
    void setLinkClock (LinkClock* clock) noexcept;

    /** Capture-Kontext für ICaptureTapClient-Module — wird bei der
        Materialisierung VOR prepareForGraph injiziert (die Kanal-
        Registrierung passiert dort, Spurname == moduleId). Der Service muss
        jedes Modul überdauern (Owner: EngineProcessor, VOR dem Graph
        deklariert). nullptr → Module bleiben reines Pass-Through (Tests).
        Message Thread. */
    void setCaptureService (CaptureService* service) noexcept;

    /** Kanal-Namen (App-Zustand) für das Auto-Naming der Send-Kanäle: die
        Quelle eines Eingangs, die am audio_input-Endpunkt hängt, liefert ihr
        ChannelNames-Label. nullptr → Fallback "In N" (Tests). Message Thread. */
    void setChannelNames (ChannelNames* names) noexcept;

    /** Modul-Typ-Defaults (Dev-Modus 4.6, Owner: EngineProcessor) —
        addModuleNode wendet sie bei Neu-Anlagen als Overlay an.
        nullptr → keine Defaults (Tests). Message Thread. */
    void setModuleUiDefaults (ModuleUiDefaults* defaults) noexcept;

    //==========================================================================
    /** Auto-Naming (7.2 Schritt 3): übernimmt für ALLE Eingänge eines Send-
        Nodes den aktuellen Quell-Namen als autoName (userName bleibt), in
        einer Undo-Transaktion. false bei unbekanntem/fremdem Node.
        Message Thread. */
    bool refreshAutoNames (const juce::String& nodeUuid);

    //==========================================================================
    /** Live-Modul-Instanz zu einer Tree-nodeId — nullptr solange das Modul
        noch nicht materialisiert ist (5.2 Schritt 1–3) oder nodeError gesetzt
        wurde. NUR Message Thread; der Pointer ist bis zum nächsten Swap
        gültig (der OscController nutzt ihn zur Endpoint-Auflösung, 7.1). */
    [[nodiscard]] ConduitModule* getModuleFor (const juce::String& nodeUuid) const;

    //==========================================================================
    /** true, solange ein gesammeltes Frame-Delta auf den Rebuild wartet. */
    [[nodiscard]] bool isTopologyDirty() const noexcept;

    /** true zwischen Fade-Out-Start (Schritt 2) und Topologie-Swap (Schritt 3). */
    [[nodiscard]] bool isWaitingForSilence() const noexcept;

    /** Anzahl bisher ausgeführter (coalesced) Rebuilds — für Tests/Diagnose. */
    [[nodiscard]] int getRebuildCount() const noexcept;

    /** Führt einen ausstehenden Verarbeitungsschritt sofort synchron aus.
        Entspricht dem nächsten Message-Loop-Durchlauf — für Tests/Shutdown. */
    void flushPendingTopologyUpdate();

private:
    //==========================================================================
    // juce::ValueTree::Listener — Topologie-Änderungen + Phase 1 des Deletes
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int formerIndex) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;

    // juce::AsyncUpdater — Fade-Zustandsmaschine (5.2) + Coalescing (5.5)
    void handleAsyncUpdate() override;

    //==========================================================================
    /** Phase 2 des Deletes (5.3): entfernt Subtree + Kabel, sobald die UI
        ihre Referenzen freigegeben hat (Refcount 0). */
    void processPendingDeletes();

    /** Schritt 1: instanziiert + prepariert alle neuen Tree-Nodes VOR dem
        Fade-Out. Fehler → nodeError, das Modul wird übersprungen. */
    void prepareNewModules();

    /** Factory-Create + prepareForGraph für einen Tree-Node.
        Bei Fehler: setzt nodeError und gibt nullptr zurück. */
    [[nodiscard]] std::unique_ptr<ConduitModule> materializeModule (juce::ValueTree nodeTree);

    /** Schritt 3: konsumiert das gesamte Frame-Delta in einem Swap. */
    void performTopologySwap();

    void removeVanishedNodes();
    void addNewNodes();
    void syncConnections();

    /** Tree → Atomic (Dual-State-Gegenstück zu 6.1): spiegelt paramValue auf
        das Echtzeit-Target des live-Moduls — bedient UI-Slider, Preset-Load
        und Undo. Kein Rebuild, nur ein atomic store. */
    void syncParameterValue (juce::ValueTree parameterTree);

    /** Tree → Atomic (Dev-Modus 4.6): spiegelt den effektiven User-
        Regelbereich (userMin/userMax, Fallback Hard-Range) ins Chassis —
        Wirkbereich der CV-Modulation. Kein Rebuild. */
    static void syncParameterUserRange (const juce::ValueTree& parameterTree,
                                        ProcessorModule& processor);

    [[nodiscard]] bool isManagedGraphNode (juce::AudioProcessorGraph::NodeID nodeId) const;
    [[nodiscard]] bool isExternalGraphNode (juce::AudioProcessorGraph::NodeID nodeId) const;

    [[nodiscard]] juce::ValueTree findConnectionTree (const juce::String& sourceUuid, int sourceChannel,
                                                      const juce::String& destUuid, int destChannel) const;

    /** Gemeinsame Kabel-Validierung von addConnection/addConnectionPair
        (Endpunkte existieren, keine Selbstverbindung, kein Duplikat). */
    [[nodiscard]] bool canConnect (const juce::String& sourceUuid, int sourceChannel,
                                   const juce::String& destUuid, int destChannel) const;

    /** Hängt das Connection-Child ein — OHNE eigene Transaktion (der Aufrufer
        klammert; addConnectionPair bündelt zwei Kabel in einer). */
    void appendConnectionChild (const juce::String& sourceUuid, int sourceChannel,
                                const juce::String& destUuid, int destChannel);

    /** Auto-Naming-Snapshot: setzt für den Eingang, der destChannel enthält,
        autoName aus der Quelle — nur wenn userName UND autoName leer sind
        (Snapshot, kein Live-Follow). Non-undoable (abgeleiteter Zustand). */
    void snapshotAutoName (juce::ValueTree sendNodeTree, int destChannel);

    /** true für die Container Nodes[] und Connections[] (Schema 6.2). */
    [[nodiscard]] static bool isTopologyContainer (const juce::ValueTree& tree) noexcept;

    /** Migration: fehlendes factoryId aus moduleId ergänzen (alter Zustand). */
    static void normalizeNode (juce::ValueTree nodeTree);

    [[nodiscard]] static juce::String sanitizeModuleName (const juce::String& raw);
    [[nodiscard]] bool isModuleNameTaken (const juce::String& name) const;
    [[nodiscard]] juce::String makeUniqueModuleName (const juce::String& factoryKey) const;

    void markTopologyDirty();

    //==========================================================================
    enum class SwapPhase
    {
        idle,               // kein Swap aktiv
        waitingForSilence   // Fade-Out läuft, Swap wartet auf den Audio Thread
    };

    juce::ValueTree rootState;          // ref-counted Handle
    juce::AudioProcessorGraph& graph;
    GraphFader& fader;
    ModuleFactory& factory;
    juce::UndoManager& undoManager;
    NodeUiRegistry& uiRegistry;

    // Nur Message Thread — bewusst kein std::atomic (kein Cross-Thread-Zugriff)
    SwapPhase swapPhase = SwapPhase::idle;
    bool topologyDirty = false;
    int pendingChangeCount = 0;
    int rebuildCount = 0;

    // Tree-nodeId (Uuid-String) → Graph-NodeID der tree-verwalteten Nodes
    std::map<juce::String, juce::AudioProcessorGraph::NodeID> treeToGraphNode;

    // In Schritt 1 vorbereitete Instanzen, warten auf den Swap
    std::map<juce::String, std::unique_ptr<ConduitModule>> preparedModules;

    // Deleting-Nodes (Phase 1 abgeschlossen): nodeId → gecachte moduleId
    std::map<juce::String, juce::String> pendingDeletes;

    // Reservierte moduleIds → extern verwaltete Graph-Nodes (I/O)
    std::map<juce::String, juce::AudioProcessorGraph::NodeID> externalEndpoints;

    // Takt-Verteiler für IClockSlaves (Owner: EngineProcessor)
    const ClockBus* clockBus = nullptr;

    // Link-Audio-Kontext für ILinkAudioClients (Owner: EngineProcessor)
    LinkClock* linkClock = nullptr;

    // Capture-Kontext für ICaptureTapClients (Owner: EngineProcessor)
    CaptureService* captureService = nullptr;

    // Kanal-Namen für Send-Auto-Naming (Owner: EngineProcessor)
    ChannelNames* channelNames = nullptr;

    // Modul-Typ-Defaults des Dev-Modus (Owner: EngineProcessor)
    ModuleUiDefaults* uiDefaults = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphManager)
};

} // namespace conduit

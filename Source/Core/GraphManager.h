#pragma once

#include <map>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "GraphFader.h"
#include "Modules/ConduitModule.h"

namespace conduit
{

class ModuleFactory;
class NodeUiRegistry;

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
    /** Phase 1 des zweiphasigen Deletes (5.3): setzt nodeState → Deleting.
        false, wenn kein Node mit dieser nodeId existiert. Message Thread. */
    [[nodiscard]] bool requestNodeDelete (const juce::String& nodeUuid);

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

    [[nodiscard]] bool isManagedGraphNode (juce::AudioProcessorGraph::NodeID nodeId) const;

    /** true für die Container Nodes[] und Connections[] (Schema 6.2). */
    [[nodiscard]] static bool isTopologyContainer (const juce::ValueTree& tree) noexcept;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphManager)
};

} // namespace conduit

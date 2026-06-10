#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Modules/ConduitModule.h"

namespace conduit
{

//==============================================================================
/**
    Koordiniert Topologie-Änderungen des AudioProcessorGraph (CLAUDE.md 5).

    Batch-Coalescing (5.5): alle topologie-relevanten ValueTree-Änderungen
    eines Frames werden via AsyncUpdater gesammelt und zu einem einzigen
    Graph-Swap zusammengefasst — gilt für Undo, Redo, Preset-Load,
    Bulk-Delete und Copy-Paste.

    Parameter-Wert-Änderungen (OSC-Dual-State, 6.1) lösen bewusst KEINEN
    Rebuild aus — nur Child-Add/Remove unter Nodes[]/Connections[] sowie
    der Austausch ganzer Container (Preset-Load) markieren das Delta.

    Läuft vollständig auf dem Message Thread — daher ist topologyDirty
    bewusst kein std::atomic. Kein UI-Code, kein DSP-Code.

    Nächste Ausbaustufen: Fade-Zyklen (5.2) und zweiphasiges Delete (5.3)
    im bisherigen handleAsyncUpdate()-Platzhalter.
*/
class GraphManager final : private juce::ValueTree::Listener,
                           private juce::AsyncUpdater
{
public:
    /** rootTree ist ein ref-counted ValueTree-Handle, processorGraph das
        Swap-Ziel ab Ausbaustufe 5.2. Registriert sich als Listener. */
    GraphManager (juce::ValueTree rootTree, juce::AudioProcessorGraph& processorGraph);

    /** Deregistriert den Listener und verwirft ausstehende Async-Updates. */
    ~GraphManager() override;

    /** true, solange ein gesammeltes Frame-Delta auf den Rebuild wartet. */
    [[nodiscard]] bool isTopologyDirty() const noexcept;

    /** Anzahl bisher ausgeführter (coalesced) Rebuilds — für Tests/Diagnose. */
    [[nodiscard]] int getRebuildCount() const noexcept;

    /** Führt einen ausstehenden Rebuild sofort synchron aus.
        Entspricht dem nächsten Message-Loop-Durchlauf — für Tests/Shutdown. */
    void flushPendingTopologyUpdate();

private:
    //==========================================================================
    // juce::ValueTree::Listener — nur Topologie-Änderungen markieren das Delta
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int formerIndex) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;

    // juce::AsyncUpdater — ein Rebuild für das gesamte Frame-Delta (5.5)
    void handleAsyncUpdate() override;

    /** true für die Container Nodes[] und Connections[] (Schema 6.2). */
    [[nodiscard]] static bool isTopologyContainer (const juce::ValueTree& tree) noexcept;

    void markTopologyDirty();

    //==========================================================================
    juce::ValueTree rootState;                          // ref-counted Handle
    [[maybe_unused]] juce::AudioProcessorGraph& graph;  // Swap-Ziel ab Ausbaustufe 5.2

    // Nur Message Thread — bewusst kein std::atomic (kein Cross-Thread-Zugriff)
    bool topologyDirty = false;
    int pendingChangeCount = 0;
    int rebuildCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphManager)
};

} // namespace conduit

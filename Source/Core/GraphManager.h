#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "GraphFader.h"
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

    Fade-Zyklus (5.2) pro Swap:
      Schritt 2: fader.beginFadeOut() — Topologie bleibt unverändert
      Schritt 3: Self-Re-Dispatch via triggerAsyncUpdate() bis der Audio
                 Thread Stille meldet (kein Busy-Poll, kein Timer), dann
                 Topologie-Swap auf Stille
      Schritt 4: fader.beginFadeIn()
    Änderungen, die während des Fade-Outs eintreffen, landen im selben Swap.
    Läuft Audio nicht (Fader unprepared), wird ohne Fade direkt geswappt.

    Parameter-Wert-Änderungen (OSC-Dual-State, 6.1) lösen bewusst KEINEN
    Rebuild aus — nur Child-Add/Remove unter Nodes[]/Connections[] sowie
    der Austausch ganzer Container (Preset-Load) markieren das Delta.

    Läuft vollständig auf dem Message Thread — daher ist topologyDirty
    bewusst kein std::atomic. Kein UI-Code, kein DSP-Code (der Fade selbst
    lebt im GraphFader auf dem Audio Thread).

    Nächste Ausbaustufen: echte Graph-Mutationen aus dem ValueTree-Delta
    in performTopologySwap(), Async Prepare (5.2 Schritt 1) und das
    zweiphasige Delete (5.3).
*/
class GraphManager final : private juce::ValueTree::Listener,
                           private juce::AsyncUpdater
{
public:
    /** rootTree ist ein ref-counted ValueTree-Handle, processorGraph das
        Swap-Ziel, faderToUse der Master-Fade im Audio-Pfad.
        Registriert sich als Listener. */
    GraphManager (juce::ValueTree rootTree,
                  juce::AudioProcessorGraph& processorGraph,
                  GraphFader& faderToUse);

    /** Deregistriert den Listener und verwirft ausstehende Async-Updates. */
    ~GraphManager() override;

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
    // juce::ValueTree::Listener — nur Topologie-Änderungen markieren das Delta
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int formerIndex) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;

    // juce::AsyncUpdater — Fade-Zustandsmaschine (5.2) + Coalescing (5.5)
    void handleAsyncUpdate() override;

    /** Schritt 3: konsumiert das gesamte Frame-Delta in einem Swap. */
    void performTopologySwap();

    /** true für die Container Nodes[] und Connections[] (Schema 6.2). */
    [[nodiscard]] static bool isTopologyContainer (const juce::ValueTree& tree) noexcept;

    void markTopologyDirty();

    //==========================================================================
    enum class SwapPhase
    {
        idle,               // kein Swap aktiv
        waitingForSilence   // Fade-Out läuft, Swap wartet auf den Audio Thread
    };

    juce::ValueTree rootState;                          // ref-counted Handle
    [[maybe_unused]] juce::AudioProcessorGraph& graph;  // Mutations-Ziel (nächste Ausbaustufe)
    GraphFader& fader;

    // Nur Message Thread — bewusst kein std::atomic (kein Cross-Thread-Zugriff)
    SwapPhase swapPhase = SwapPhase::idle;
    bool topologyDirty = false;
    int pendingChangeCount = 0;
    int rebuildCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphManager)
};

} // namespace conduit

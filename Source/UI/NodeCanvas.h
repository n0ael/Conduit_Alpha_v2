#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "UI/NodeComponent.h"
#include "UI/PortComponent.h"

namespace conduit
{

//==============================================================================
/**
    Patching-Fläche: spiegelt Nodes[] des Root-Trees als NodeComponents —
    rein ValueTree-getrieben (read/listen + Patch-Aktionen über den
    GraphManager), analog zum Topologie-Sync der Engine.

    Sync-Regeln:
      - childAdded (Node)        → Component erzeugen (außer nodeState == Deleting)
      - nodeState → Active       → Component nachziehen (Undo-Restore: der
                                   Subtree kommt mit nodeState == Deleting zurück,
                                   erst der Swap setzt ihn auf Active)
      - childRemoved (Node)      → Component sofort zerstören (Fallback: Redo
                                   eines Deletes/Preset-Load entfernen ohne
                                   Deleting-Phase)
      - Container-Austausch /
        redirected (Preset-Load) → Full-Rebuild

    Doppelklick/Doppel-Tap auf freie Fläche legt ein Modul an der Klickposition
    an (bis zur Modul-Palette fest: Attenuator).

    Drag-and-drop aus dem Browser-Panel (M3): DragAndDropTarget für
    Descriptions "conduit.module:{factoryKey}" — Drop legt das Modul an
    der Drop-Position an (derselbe undo-fähige addModuleNode-Pfad wie der
    Tap); während des Hovers zeigt ein Akzent-Rahmen die Drop-Fläche.
*/
class NodeCanvas final : public juce::Component,
                         public juce::DragAndDropTarget,
                         private juce::ValueTree::Listener,
                         private juce::ChangeListener
{
public:
    /** channelNamesToUse darf nullptr sein (Tests) — die NodeComponents
        der I/O-Endpunkte zeigen dann keine Port-Labels. inputLevels/
        outputLevels (nullptr in Tests) speisen die Pegelanzeigen der
        I/O-Endpunkte; inputSendToUse (nullptr in Tests) den Status der
        Link-Send-Toggles an den audio_in-Zeilen (7.2). */
    NodeCanvas (juce::ValueTree rootTree,
                GraphManager& graphManagerToUse,
                NodeUiRegistry& uiRegistryToUse,
                ChannelNames* channelNamesToUse = nullptr,
                LevelMeter* inputLevelsToUse = nullptr,
                LevelMeter* outputLevelsToUse = nullptr,
                InputLinkSend* inputSendToUse = nullptr,
                UiSettings* uiSettingsToUse = nullptr);
    ~NodeCanvas() override;

    [[nodiscard]] int getNumNodeComponents() const noexcept;
    [[nodiscard]] NodeComponent* findNodeComponent (const juce::String& nodeUuid) const;

    //==========================================================================
    // Kabel-Gesten — aufgerufen von den PortComponents (Canvas-Koordinaten)
    void beginCableDrag (const PortInfo& fromPort, juce::Point<int> position);
    void updateCableDrag (juce::Point<int> position);
    void endCableDrag (juce::Point<int> position);

    /** Dwell-Geste: erzwingt ein Einzel-(Mono-)Kabel trotz Stereo-Quelle. */
    void setCableDragMono();

    /** Kabel unter dem Punkt (Toleranz ~8px), invalides ValueTree wenn keins.
        Public für Tests. */
    [[nodiscard]] juce::ValueTree findConnectionAt (juce::Point<int> position) const;

    //==========================================================================
    // juce::DragAndDropTarget — Browser-Drag (Payload: BrowserDragPayload.h)
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    //==========================================================================
    // juce::ValueTree::Listener [Message Thread]
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int formerIndex) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;

    // juce::ChangeListener [Message Thread] — ChannelNames-Farben (Input-Kabel)
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** Quellfarbe eines Kabels: Input-Kanal-Farbe (ChannelNames) am audio_in
        bzw. effektive (ggf. geerbte) Farbe des Quellmoduls; 0/keine →
        Default-Kabelfarbe. Liest den vorberechneten flowColours-Cache. */
    [[nodiscard]] juce::Colour cableColourFor (const juce::String& sourceUuid,
                                               int sourceChannel) const;

    //==========================================================================
    // Signal-Flow-Farbvererbung (Anzeige): Module OHNE eigene Farbe erben die
    // (gemischte) Farbe ihrer Eingänge und geben sie weiter — explizite Farbe
    // gewinnt IMMER, Feedback-Schleifen per visiting-Set abgefangen. Rein
    // abgeleitet, kein Patch-Zustand.

    /** Baut flowColours neu (effektive Farbe pro Node) und schiebt sie in die
        Header-Punkte; danach repaint der Kabel. Bei Topologie-/Farb-Änderung. */
    void refreshFlowColours();

    /** Effektive Node-Farbe (explizit → sonst gemischte Eingänge), memoisiert
        in flowColours; visiting bricht Zyklen. audio_in → 0 (Quelle pro Kanal). */
    juce::uint32 computeEffectiveRgb (const juce::String& nodeUuid,
                                      std::set<juce::String>& visiting);

    /** Quellfarbe eines Kanals für die Vererbung: audio_in → ChannelNames pro
        Kanal, sonst computeEffectiveRgb des Quellmoduls. */
    juce::uint32 sourceChannelRgb (const juce::String& sourceUuid, int channel,
                                   std::set<juce::String>& visiting);

    /** Cache-Lookup (const, nach refreshFlowColours) für die Kabel-/Punktfarbe. */
    [[nodiscard]] juce::uint32 lookupSourceRgb (const juce::String& sourceUuid,
                                                int channel) const;

    /** audio_in-Kanalfarbe (ChannelNames), aufgelöst auf den Stereo-Paar-Anker:
        beide Kanäle eines Paars liefern die Farbe des ERSTEN Kanals. */
    [[nodiscard]] juce::uint32 inputChannelRgb (int channel) const;

    /** Mittelt die Farben (0x00RRGGBB) komponentenweise; {} → 0. */
    [[nodiscard]] static juce::uint32 blendRgb (const std::vector<juce::uint32>& colours);

    void rebuildAll();
    void addComponentFor (juce::ValueTree nodeTree);
    void removeComponentFor (const juce::String& nodeUuid);

    [[nodiscard]] std::optional<juce::Point<int>> getPortCentreInCanvas (const juce::String& nodeUuid,
                                                                         bool isInput, int channel) const;
    [[nodiscard]] static juce::Path makeCablePath (juce::Point<float> start, juce::Point<float> end);

    //==========================================================================
    juce::ValueTree rootState;  // ref-counted Handle
    GraphManager& graphManager;
    NodeUiRegistry& uiRegistry;
    ChannelNames* channelNames;  // nullptr in Tests
    LevelMeter* inputLevels;     // Sicht-Metering audio_in (nullptr in Tests)
    LevelMeter* outputLevels;    // Sicht-Metering audio_out (nullptr in Tests)
    InputLinkSend* inputSend;    // Send-LED-Status audio_in (nullptr in Tests)
    UiSettings* uiSettings;      // gatet die DEV-Toggles (nullptr in Tests)

    std::vector<std::unique_ptr<NodeComponent>> nodeComponents;

    // Effektive (ggf. geerbte) Farbe pro Node — abgeleitet, kein Patch-Zustand
    std::map<juce::String, juce::uint32> flowColours;

    struct CableDrag
    {
        PortInfo from;
        juce::Point<int> currentPosition;
        bool forceMono = false;  // Dwell-Geste: Einzel-Kabel trotz Stereo-Quelle
    };

    std::optional<CableDrag> activeCableDrag;

    bool dropHighlight = false;   // Browser-Drag schwebt über der Fläche

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeCanvas)
};

} // namespace conduit

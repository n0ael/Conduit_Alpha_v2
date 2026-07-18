#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/CanvasGestureRecognizer.h"
#include "Core/CanvasViewport.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "UI/NodeCanvasContent.h"
#include "UI/NodeComponent.h"
#include "UI/PortComponent.h"

namespace conduit
{

class PageManager;

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

    Viewport (ADR 008 M3a): Alle NodeComponents leben in der
    NodeCanvasContent-Zwischenschicht, deren AffineTransform Zoom + Pan
    trägt (canvas_view::ViewState, Clamps 0.1–2.0). Eingabepfade Ebene 2:
    2-Finger-Pinch/Pan (CanvasGestureRecognizer, Leerraum-Regel — nur
    Touches, die auf dem Canvas-Hintergrund beginnen), mouseMagnify
    (Trackpad-Pinch), Scroll = Pan / Cmd+Scroll = Zoom, mittlere
    Maustaste = Pan. Viewport persistiert in den Page-Properties der
    aktiven Seite (M1: Pages[0]); Zoom < UiSettings::interactionMinZoom
    sperrt die Modul-Interaktion (Module = reine Navigationsziele).
    Ebenen 3/4/5 werden erkannt, ihre Aktionen folgen in M3b/M4.
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
                UiSettings* uiSettingsToUse = nullptr,
                PageManager* pageManagerToUse = nullptr);
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
    // Viewport (ADR 008 M3a) — public für Tests und die M4-Menü-Pegel
    [[nodiscard]] canvas_view::ViewState getViewState() const noexcept { return view; }
    void setViewState (canvas_view::ViewState newView);

    //==========================================================================
    // Seiten-Navigation (ADR 008 M3b) — Tastatur-/Modifier-Parität + Tests

    /** Wechselt zur Nachbarseite (gridX+dx, gridY+dy) der aktiven Seite;
        existiert dort keine, wird sie ANGELEGT (undo-fähig — paritätisch
        zum Wisch ins Leere). No-op ohne PageManager. Der Rebuild läuft
        über den activePage-Property-Listener. */
    void navigatePages (int dx, int dy);

    /** Commit-Schwelle des 4-Finger-Swipes als Anteil der Canvas-Breite. */
    static constexpr double pageSwipeCommitFraction = 0.15;

    //==========================================================================
    // Birdeye (ADR 008 M4): 3-Finger-HOLD — transiente Übersicht der
    // AKTIVEN Seite; die Karte bewegt sich unter dem fixen Mittel-Target,
    // Loslassen zoomt auf den Arbeits-Pegel an dieser Stelle.

    /** Tastatur-Parität (Ctrl/Cmd+Alt+B): Toggle statt Hold. */
    void toggleBirdeye();

    [[nodiscard]] bool isBirdeyeActive() const noexcept { return birdeyeActive; }

    /** 5-Finger/Tastatur (Ctrl/Cmd+Alt+O): Seiten-Übersicht ein-/ausblenden. */
    void togglePageOverview();

    [[nodiscard]] bool isPageOverviewVisible() const noexcept;

    /** true, wenn die Modul-Interaktion gesperrt ist (Zoom < Schwelle). */
    [[nodiscard]] bool isInteractionLocked() const noexcept;

    //==========================================================================
    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify (const juce::MouseEvent& event, float scaleFactor) override;

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

    //==========================================================================
    // Viewport (ADR 008 M3a)

    /** Kabel + Drag-Vorschau — läuft im Content-Kontext (Content-Koordinaten
        == Node-Koordinaten des Trees; Clip-Culling bleibt identisch). */
    void paintCables (juce::Graphics& g);

    /** view → Content-Transform + Interaktions-Sperre + repaint. */
    void applyViewTransform();

    /** view in die Page-Properties der aktiven Seite schreiben (ohne Undo —
        View-State, Muster Node-Drag). Am Gesten-Ende bzw. nach diskreten
        Events (Wheel/Magnify). */
    void persistViewState();

    /** view aus den Page-Properties der aktiven Seite laden (Ctor,
        Preset-Load/Container-Austausch). */
    void restoreViewState();

    /** Aktive Seite: via PageManager (M3b); Fallback ohne PageManager
        (Tests): Pages[0]. Repariert ggf. die activePage-Property. */
    [[nodiscard]] juce::ValueTree activePageTree();

    /** Seiten-Filter (M3b): true, wenn der Node auf der aktiven Seite
        liegt — Nodes ohne pageUuid (Alt-Rigs) sind immer sichtbar. */
    [[nodiscard]] bool isOnActivePage (const juce::ValueTree& nodeTree);

    /** 4-Finger-Swipe beendet: Commit (dominante Achse über der Schwelle —
        Wisch links = Seite rechts) oder Snap-back. */
    void handleSwipeEnd();

    /** Canvas-Punkt → Content-Koordinaten (durch den Transform). */
    [[nodiscard]] juce::Point<int> toContentPosition (juce::Point<int> canvasPosition) const;

    /** Pinch-Schwelle aus den UiSettings in den Recognizer speisen
        (Ctor + jeder Settings-Broadcast). */
    void applyRecognizerTuning();

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

    // Transform-Träger (ADR 008 M3a) — VOR nodeComponents deklariert: die
    // Node-Components (Kinder des content) werden zuerst zerstört
    NodeCanvasContent content;

    std::vector<std::unique_ptr<NodeComponent>> nodeComponents;

    // Effektive (ggf. geerbte) Farbe pro Node — abgeleitet, kein Patch-Zustand
    std::map<juce::String, juce::uint32> flowColours;

    struct CableDrag
    {
        PortInfo from;
        juce::Point<int> currentPosition;
        bool forceMono = false;  // Dwell-Geste: Einzel-Kabel trotz Stereo-Quelle
    };

    std::optional<CableDrag> activeCableDrag;   // Positionen in Content-Koordinaten

    bool dropHighlight = false;   // Browser-Drag schwebt über der Fläche

    //==========================================================================
    // Viewport (ADR 008 M3a)
    CanvasGestureRecognizer recognizer;      // Ebene 2 aktiv, 4 = Seiten-Swipe
    canvas_view::ViewState view;
    std::optional<juce::Point<float>> panDragLast;   // mittlere Maustaste

    // Seiten-Navigation (M3b)
    PageManager* pageManager = nullptr;      // nullptr in Alt-Tests (Filter aus)
    bool swipeActive = false;                // Ebene-4-Geste läuft
    juce::Point<double> swipeDelta;          // akkumuliert (Peek-Versatz)
    double wheelSwipeAccum = 0.0;            // Alt+Scroll-Seitenwechsel

    // Birdeye (M4)
    bool birdeyeActive = false;
    void startBirdeye();
    void endBirdeye();
    [[nodiscard]] double workZoomLevel() const;
    [[nodiscard]] double birdeyeZoomLevel() const;

    // Seiten-Übersicht (M4) — als Overlay-Kind, lazy erzeugt
    std::unique_ptr<juce::Component> pageOverview;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeCanvas)
};

} // namespace conduit

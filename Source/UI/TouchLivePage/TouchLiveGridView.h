#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TouchLive/LiveSetModel.h"
#include "TouchLive/TouchLiveClient.h"
#include "TouchLive/TouchLiveSettings.h"

namespace conduit
{

//==============================================================================
/**
    GRID-Sub-Tab der TouchLive-Page (docs/TouchLive.md §5, Session-View):
    Spalten = Live-Tracks mit farbigem Header, Zeilen = Scenes, rechte
    Spalte = Scene-Fire, unterste Zeile = Clip-Stop pro Track. Zellen sind
    paint-only (kein Component pro Zelle) mit manuellem Hit-Test.

    Zustände pro Zelle (aus der session-Domain): leer · stopped · playing ·
    triggered (blinkend) · recording. Blink-Phase über VBlankAttachment
    (~2 Hz zeitbasiert; Link-Beat-Kopplung ist M2-Feinschliff).

    Interaktion: Tap = Aktion (Clip/Scene/Stop feuern), Drag > Schwelle =
    Pan über das Grid (1-Finger, docs §5); Header/Scene-Spalte/Stop-Zeile
    bleiben angepinnt. Spaltenbreite folgt der Kanalbreiten-Einstellung
    (TouchLiveSettings).

    Struktur-/Zustands-Änderungen aus dem Modell rebuilden den Zellen-Cache
    coalesced (AsyncUpdater, Test-Seam flushPendingRebuild).
*/
class TouchLiveGridView final : public juce::Component,
                                private juce::ValueTree::Listener,
                                private juce::ChangeListener,
                                private juce::AsyncUpdater
{
public:
    TouchLiveGridView (TouchLiveClient& clientToUse, LiveSetModel& modelToUse,
                       TouchLiveSettings& settingsToUse);
    ~TouchLiveGridView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

    //==========================================================================
    // Test-Seams
    void flushPendingRebuild();

    /** Tap-Aktion an einer View-Position ausführen (Kernpfad von mouseUp). */
    void tapAt (juce::Point<int> position);

    [[nodiscard]] int getColumnCount() const noexcept { return (int) columns.size(); }
    [[nodiscard]] int getSceneCount() const noexcept { return (int) scenes.size(); }

    //==========================================================================
    static constexpr int headerHeight  = 30;
    static constexpr int cellHeight    = 44;   // Touch-Minimum (CLAUDE.md 10.0)
    static constexpr int sceneColumn   = 84;
    static constexpr int stopRowHeight = 32;

private:
    //==========================================================================
    struct Column
    {
        juce::String key;
        juce::String name;
        juce::Colour colour;
        juce::Array<juce::var> slots;   // Scene-Reihenfolge; void = leer
    };

    struct Scene
    {
        juce::String name;
        juce::Colour colour;
    };

    //==========================================================================
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void handleAsyncUpdate() override;

    void rebuildCache();
    void clampScroll();

    [[nodiscard]] int columnWidth() const;
    [[nodiscard]] juce::Rectangle<int> gridArea() const;

    void paintCell (juce::Graphics& g, const juce::Rectangle<float>& cell,
                    const juce::var& slot) const;

    void fireClip (int columnIndex, int sceneIndex);
    void fireScene (int sceneIndex);
    void stopTrack (int columnIndex);

    //==========================================================================
    TouchLiveClient& client;
    LiveSetModel& model;
    TouchLiveSettings& settings;

    // Listener-Handle als Member (Lektion M1b: hängt an der Instanz)
    juce::ValueTree modelState;

    std::vector<Column> columns;
    std::vector<Scene> scenes;
    bool anyTriggered = false;

    int scrollX = 0, scrollY = 0;
    juce::Point<int> dragStartScroll;
    bool panning = false;

    // Blink-Phase (~2 Hz) für triggered-Zellen
    juce::VBlankAttachment vblank;
    bool blinkPhase = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchLiveGridView)
};

} // namespace conduit

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "LevelMeterBar.h"
#include "Modules/LooperPatchOutModule.h"

namespace conduit
{

//==============================================================================
/**
    Anzeige-Panel eines LooperPatchOutModule — lebt im NodeComponent
    (Muster LooperPatchInPanel-Zeilenraster). Die Slots folgen automatisch
    der Looper-Struktur (syncLooperPatchOutConfigs); bedienbar sind NUR
    die Einklapp-Dreiecke der Überschrifts-Zeilen.

    Zeilenaufbau (User-Feedback-Runden 19.07.2026): Sektions-Überschriften
    „Looper 1–4", „Busse", „Sends" mit ▸/▾-Dreieck (Tap klappt die Sektion
    ein/aus — VIEW-Zustand `id::outCollapsed`-Bitmask am Node, ohne Undo);
    darunter kompakte Slot-Zeilen „Track x" (global im 4er-Raster),
    „Bus 1–4", „Send 1–4"; Master immer sichtbar. Links pro Slot-Zeile ein
    kleiner Farbstreifen (Optik des audio_in-Kanalstreifens) mit der
    aufgelösten Slot-Farbe, Text linksbündig, direkt dahinter das kompakte
    Stereo-Meter (LevelMeterBar, 2 Lanes, 120 px wie die I/O-Endpunkte;
    Kanäle stabil via meterChannelOf). Eingeklappte Slots: Meter versteckt,
    die Ports des NodeComponent ankern gesammelt an der Überschrifts-Zeile
    und sind unsichtbar (= nicht patchbar).

    Bindung an den Subtree (5.3); der LevelMeter* ist die dokumentierte
    Meter-Ausnahme (Owner EngineProcessor überlebt den Editor). nullptr in
    Tests → keine Meter.
*/
class LooperPatchOutPanel final : public juce::Component,
                                  private juce::ValueTree::Listener
{
public:
    explicit LooperPatchOutPanel (juce::ValueTree nodeTreeToBind,
                                  LevelMeter* looperLevelsToUse = nullptr);
    ~LooperPatchOutPanel() override;

    /** Teardown-Hook (Phase 1, 5.3). */
    void stopUpdates();

    //==========================================================================
    // Sektionen der Collapse-Bitmask (id::outCollapsed):
    // Bits 0–3 = Looper 1–4, 4 = Busse, 5 = Sends
    static constexpr int sectionBusses = 4;
    static constexpr int sectionSends  = 5;

    /** Default OHNE gespeicherte Property: ALLES eingeklappt (User
        20.07.2026 — voll ausgeklappt frisst je nach Bildschirm enorm
        Platz); gilt für neue Module UND Alt-Patches ohne Maske. */
    static constexpr int defaultCollapsedMask = 0b111111;

    /** Punkt, Strang-Linie und Kabel-Anker liegen 1 px UNTER der
        Zeilenmitte — optische Linie der Großbuchstaben (User-Lineal
        20.07.2026, Feinjustage Runde 10); NodeComponent nutzt denselben
        Versatz. */
    static constexpr int strandYOffset = 1;

    /** Sektion einer Spec (Track → looper−1, Bus/Send → 4/5, Master −1). */
    [[nodiscard]] static int sectionOfSpec (const LooperPatchOutModule::OutputSpec& spec) noexcept;

    /** Eine sichtbare Zeile: Überschrift (slotIndex −1) oder Slot. */
    struct Row
    {
        juce::String label;
        int slotIndex = -1;      // −1 = Überschrifts-Zeile
        int section = -1;        // Header: geschaltete Sektion; Master −1
        bool collapsed = false;  // nur Header: ▸ statt ▾
        [[nodiscard]] bool isHeader() const noexcept { return slotIndex < 0; }
    };

    /** Sichtbare Zeilenliste aus Spec-Liste + Collapse-Bitmask (pure,
        testbar): eingeklappte Sektionen zeigen nur ihre Überschrift. */
    [[nodiscard]] static std::vector<Row> buildRows (
        const std::vector<LooperPatchOutModule::OutputSpec>& specs, int collapsedMask);

    /** Höhe der Kachel-Innenfläche (inkl. Überschriften, ohne die
        eingeklappten Zeilen) — NodeComponent-Sizing. */
    [[nodiscard]] static int heightForSpecs (
        const std::vector<LooperPatchOutModule::OutputSpec>& specs, int collapsedMask);

    /** Vertikale Mitte der SLOT-Zeile slotIndex (panel-lokal) — bei
        eingeklappter Sektion die Mitte ihrer ÜBERSCHRIFT (alle Kabel
        verlassen das Modul dort gesammelt). Die Ports des NodeComponent
        fluchten damit. */
    [[nodiscard]] static int rowCentreYForSlot (
        const std::vector<LooperPatchOutModule::OutputSpec>& specs, int collapsedMask,
        int slotIndex);

    /** Instanz-Varianten auf den aktuell gelesenen Specs. Die Maske kommt
        FRISCH aus dem Tree: der NodeComponent-Listener (Port-Layout) feuert
        VOR dem Panel-Listener — der Cache wäre dort noch alt. */
    [[nodiscard]] int slotCentreY (int slotIndex) const
    {
        return rowCentreYForSlot (specs, currentMask(), slotIndex);
    }
    [[nodiscard]] bool isSlotCollapsed (int slotIndex) const noexcept
    {
        return juce::isPositiveAndBelow (slotIndex, (int) specs.size())
            && sectionOfSpec (specs[(size_t) slotIndex]) >= 0
            && (currentMask() & (1 << sectionOfSpec (specs[(size_t) slotIndex]))) != 0;
    }

    [[nodiscard]] int getNumRows() const noexcept { return (int) specs.size(); }

    /** [Canvas, refreshFlowColours] Aufgelöste Slot-Farben (0x00RRGGBB
        pro Slot, 0 = keine) für die Farbstreifen nachziehen; die
        Looper-Überschriften zeigen zusätzlich die Farbe der GEWÄHLTEN
        Quelle ihres Loopers als PUNKT hinter dem Text (resolveHeaderRgb,
        Looper-Index 0–3). resolveHasCable (linker Slot-Kanal) meldet, ob
        ein Kabel am Slot hängt — eingeklappte Sektionen mit Kabeln
        zeichnen die Strang-Linie auf der Textzeile (User-Sketch
        19.07.2026). Repaint nur bei Änderung. */
    void refreshSlotColours (const std::function<juce::uint32 (int channel)>& resolveRgb,
                             const std::function<juce::uint32 (int looperIndex)>& resolveHeaderRgb,
                             const std::function<bool (int channel)>& resolveHasCable);

    /** Eingeklappte Sektionen MIT Kabeln: y (panel-lokal, Textmitte der
        Überschrift) + Strang-Farbe — der NodeComponent zeichnet die
        Linie vom Panel-Rand weiter bis zum Modulrand. */
    struct CollapsedStrand
    {
        int y = 0;
        juce::uint32 rgb = 0;
    };
    [[nodiscard]] std::vector<CollapsedStrand> getCollapsedStrands() const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    // juce::ValueTree::Listener [Message Thread] — Slot-Struktur + Collapse
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;

    void rebuildSpecs();

    /** Hat ein Slot der Sektion ein Kabel? / Strang-Farbe = Mischung der
        verkabelten Slot-Farben (Fallback Standard-Kabelgrün). */
    [[nodiscard]] bool sectionHasCables (int section) const;
    [[nodiscard]] juce::uint32 sectionStrandRgb (int section) const;

    [[nodiscard]] int currentMask() const noexcept
    {
        return (int) nodeTree.getProperty (id::outCollapsed, defaultCollapsedMask);
    }

    static constexpr int rowHeight     = 30;
    static constexpr int topPadding    = 6;
    static constexpr int bottomPadding = 6;

    juce::ValueTree nodeTree;   // Subtree (5.3)
    LevelMeter* looperLevels;   // Meter-Ausnahme (nullptr in Tests)
    std::vector<LooperPatchOutModule::OutputSpec> specs;
    std::vector<Row> rows;                                  // sichtbare Zeilen
    std::vector<std::unique_ptr<LevelMeterBar>> meterBars;  // 1:1 zu specs
    std::vector<juce::uint32> slotColours;                  // 1:1 zu specs
    std::vector<char> slotHasCable;                         // 1:1 zu specs (Kabel am Slot)
    std::array<juce::uint32, 4> headerColours {};           // Quellfarbe je Looper
    int collapsedMask = 0;
    bool frozen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPatchOutPanel)
};

} // namespace conduit

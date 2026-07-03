#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/GraphManager.h"
#include "UI/GainFaderMeter.h"

namespace conduit
{

//==============================================================================
/**
    Pflicht-Oberfläche aller Processor-Nodes (FX-Chassis, CLAUDE.md 4.6) —
    ersetzt das generische ParameterPanel für type == "Processor":

      [IN-Fader+Meter] | P1 | P2 | … | Pn | [OUT-Fader+Meter]

    Links der input_gain-Kanalzug, rechts output_gain (GainFaderMeter),
    dazwischen pro sichtbarem DSP-Parameter (role == "dsp") eine vertikale
    Fader-Spalte: Titel oben, langer Fader darunter. Chassis- und
    cvAmount-Parameter erscheinen NICHT als Spalten — der Attenuverter-Knob
    und der CV-Port ziehen in M3 unter die Fader.

    Bindung (5.3): schreibt nur paramValue ohne UndoManager (Muster
    ParameterPanel); externe Änderungen kommen über den ValueTree-Listener.
*/
class FxModulePanel final : public juce::Component,
                            private juce::ValueTree::Listener
{
public:
    FxModulePanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse);
    ~FxModulePanel() override;

    /** Teardown-Hook (Phase 1, 5.3): Interaktion + Meter sofort stoppen. */
    void stopUpdates();

    //==========================================================================
    // Layout-Konstanten — zentral, damit NodeComponent-Sizing und Tests
    // dieselbe Quelle nutzen
    static constexpr int columnWidth = 56;
    static constexpr int titleHeight = 18;
    static constexpr int panelHeight = 216;

    /** Panel-Breite für n sichtbare DSP-Spalten (plus zwei Gain-Züge). */
    [[nodiscard]] static int widthForColumns (int numDspColumns) noexcept
    {
        return 2 * GainFaderMeter::preferredWidth
             + juce::jmax (0, numDspColumns) * columnWidth + 16;
    }

    [[nodiscard]] int getNumColumns() const noexcept { return static_cast<int> (columns.size()); }

    void resized() override;

    //==========================================================================
    // Eine DSP-Parameter-Spalte — Controls public, UI-Tests treiben sie direkt
    struct ParameterColumn
    {
        juce::String paramId;
        juce::Label  titleLabel;
        juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    };

    std::vector<std::unique_ptr<ParameterColumn>> columns;

    // Gain-Züge (immer vorhanden — Chassis-Standard)
    std::unique_ptr<GainFaderMeter> inputFader;
    std::unique_ptr<GainFaderMeter> outputFader;

private:
    //==========================================================================
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    void buildColumns();

    [[nodiscard]] juce::ValueTree parametersTree() const;
    [[nodiscard]] juce::ValueTree paramTreeFor (const juce::String& paramId) const;

    //==========================================================================
    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)
    GraphManager& graphManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxModulePanel)
};

} // namespace conduit

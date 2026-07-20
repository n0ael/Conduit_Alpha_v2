#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Modules/ConduitModule.h"

namespace conduit
{

//==============================================================================
/**
    Generisches Bedien-Panel für Module mit mehreren Parametern — lebt im
    NodeComponent (Muster LinkAudioSendPanel) für jedes
    Modul ohne eigene Bedienoberfläche. Ersetzt den früheren einzelnen
    generischen Slider (der nur den ERSTEN Parameter zeigte) durch eine Zeile
    pro Parameter im Tree, betitelt mit der paramId.

    Bindung an den ValueTree-Subtree (5.3): schreibt nur paramValue, ohne
    UndoManager — Parameter-Sweeps sind keine patchbaren Aktionen (gleiches
    Verhalten wie der OSC-Pfad 6.1). Externe Änderungen (OSC-Nachzug, Undo,
    Preset-Load) kommen über den ValueTree-Listener zurück.
*/
class ParameterPanel final : public juce::Component,
                             private juce::ValueTree::Listener
{
public:
    explicit ParameterPanel (juce::ValueTree nodeTreeToBind);
    ~ParameterPanel() override;

    /** Teardown-Hook (Phase 1, 5.3): Interaktion sofort stoppen. */
    void stopUpdates();

    /** Höhe der Kachel-Innenfläche für n Parameter (NodeComponent-Sizing). */
    [[nodiscard]] static int heightForParameters (int numParameters) noexcept
    {
        return juce::jmax (1, numParameters) * rowHeight;
    }

    [[nodiscard]] int getNumRows() const noexcept { return static_cast<int> (rows.size()); }

    void resized() override;

    //==========================================================================
    // Zeile eines Parameters — Controls public, UI-Tests treiben sie direkt
    struct ParameterRow
    {
        juce::String paramId;
        juce::Label  nameLabel;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    };

    std::vector<std::unique_ptr<ParameterRow>> rows;

private:
    //==========================================================================
    // juce::ValueTree::Listener [Message Thread]
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    void buildRows();

    [[nodiscard]] juce::ValueTree parametersTree() const;
    [[nodiscard]] juce::ValueTree paramTreeFor (const juce::String& paramId) const;
    [[nodiscard]] juce::Rectangle<int> rowBounds (int index) const;

    static constexpr int rowHeight = 30;

    juce::ValueTree nodeTree;   // NUR der Subtree (5.3)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterPanel)
};

} // namespace conduit

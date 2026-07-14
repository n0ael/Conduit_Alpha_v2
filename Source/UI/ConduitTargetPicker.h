#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/MacroBindings.h"
#include "Modules/ConduitModule.h"
#include "PushTiles.h"

namespace conduit
{

//==============================================================================
/**
    Picker der Macro-Zielkategorie „Conduit" (MIDI-Rig M5c) — CallOutBox-
    Inhalt (Muster HardwareTargetPicker: schließt sich nach Auswahl selbst
    über findParentComponentOfClass<CallOutBox>()->dismiss()).

    Zwei Ebenen: Wurzel = Patch-Module (aus rootState Nodes[], Name =
    moduleId) + darunter die Grid-Controls (flach, vom Besitzer geliefert);
    Modul antippen → dsp-Parameter des Moduls (role "dsp", uiHidden
    gefiltert). Custom-painted Zeilenliste im Viewport (44 px, Touch-Regel),
    Breadcrumb-Kopfzeile mit Zurück. Message Thread, liest NUR den Tree
    (nie ein Modul, Zombie-Regel 5.3).
*/
class ConduitTargetPicker final : public juce::Component
{
public:
    using GridControlEntry = std::pair<grid::MacroControlKey, juce::String>;

    ConduitTargetPicker (juce::ValueTree rootStateToUse,
                         std::vector<GridControlEntry> gridControlsToUse);

    /** Modul-Parameter gewählt: nodeUuid + paramId + Anzeigename
        ("moduleId: paramId"). */
    std::function<void (const juce::String& nodeUuid, const juce::String& paramId,
                        const juce::String& displayName)> onParamChosen;

    /** Grid-Control gewählt. */
    std::function<void (const grid::MacroControlKey& key,
                        const juce::String& displayName)> onControlChosen;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    struct Row
    {
        enum class Kind { header, module, parameter, gridControl };

        Kind kind = Kind::header;
        juce::String label;
        juce::String nodeUuid;             // module/parameter
        juce::String paramId;              // parameter
        grid::MacroControlKey controlKey;  // gridControl
    };

    void buildRootRows();
    void buildParameterRows (const juce::String& nodeUuid, const juce::String& moduleName);
    void rowTapped (const Row& row);
    void dismissSelf();

    //==========================================================================
    /** Custom-painted Zeilenliste (Muster HardwareTargetPicker). */
    class RowList final : public juce::Component
    {
    public:
        explicit RowList (ConduitTargetPicker& ownerToUse) : owner (ownerToUse) {}

        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& event) override;

        static constexpr int kRowHeight = 44;

    private:
        ConduitTargetPicker& owner;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RowList)
    };

    juce::ValueTree rootState;
    std::vector<GridControlEntry> gridControls;

    std::vector<Row> rows;
    bool atRoot = true;
    juce::String currentModuleUuid, currentModuleName;

    juce::Label breadcrumbLabel;
    push::TextTile backTile { juce::String::fromUTF8 ("\xe2\x80\xb9") };   // ‹
    juce::Viewport viewport;
    RowList rowList { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConduitTargetPicker)
};

} // namespace conduit

#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ChannelNames.h"
#include "Core/InputLinkSend.h"
#include "UI/InputSendButton.h"

namespace conduit
{

//==============================================================================
/**
    Konsolidiertes Attribut-Panel eines audio_in-Kanals (CLAUDE.md 10, M-A) —
    Inhalt einer CallOutBox, geöffnet per Long-Press auf die Kanalzeile des
    Input-Nodes. Fasst die bislang verstreuten Attribute an EINER Stelle am
    Quellmodul zusammen:

      - Name    → ChannelNames::setUserLabel (verlagert aus dem CapturePanel)
      - Farbe   → ChannelNames::setColour (Preset-Swatches der Push-Palette)
      - Stereo  → ChannelNames::setPortPairedWithNext (mit dem nächsten Kanal)
      - Send    → eingebetteter InputSendButton (7.2)

    Alle Writes gehen NUR in ChannelNames (App-Zustand am physischen
    Geräte-Kanal); der ChangeBroadcast zieht Engine und Node-UI nach. Das Panel
    ist kurzlebig (CallOutBox) und hält keinen Processor-Pointer (5.3).

    portIndex = komprimierter Port-Index des Kanals (wie pairToggles/labels);
    hasNextNeighbour blendet den Stereo-Toggle aus, wenn kein Partner existiert.
    sendService darf nullptr sein (Tests) — dann bleibt die Send-LED grau.
*/
class ChannelAttributePanel final : public juce::Component
{
public:
    ChannelAttributePanel (ChannelNames& channelNamesToUse,
                           const InputLinkSend* sendServiceToUse,
                           int portIndexToUse,
                           bool hasNextNeighbour);
    ~ChannelAttributePanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;

private:
    /** Farb-Swatch: Zielfarbe (0 = „keine"). */
    struct Swatch
    {
        juce::uint32 colour = 0;         // 0x00RRGGBB, 0 = keine
        juce::Rectangle<int> bounds;
    };

    [[nodiscard]] juce::uint32 currentColour() const;

    ChannelNames& channelNames;
    const int portIndex;
    const bool hasNextNeighbour;

    juce::Label titleLabel;
    juce::Label nameCaption;
    juce::Label nameEditor;            // editierbar → setUserLabel
    juce::Label colourCaption;
    juce::ToggleButton stereoToggle;   // nur sichtbar mit Partner-Kanal
    juce::Label sendCaption;
    std::unique_ptr<InputSendButton> sendButton;

    std::vector<Swatch> swatches;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelAttributePanel)
};

} // namespace conduit

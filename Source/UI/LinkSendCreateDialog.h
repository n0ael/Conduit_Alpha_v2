#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Modules/LinkAudioSendModule.h"

namespace conduit
{

//==============================================================================
/**
    Kompakter Anlege-Dialog für ein Multi-Input LinkAudioSendModule: die
    Eingangszahl ist FIX beim Anlegen (CLAUDE.md 7.2 / Plan), also wird sie
    hier einmal gewählt — Anzahl Mono- und Stereo-Eingänge (Reihenfolge:
    Monos, dann Stereos; deckt sich mit applyInputConfig).

    Kein Modal-Loop (13.2): wird per CallOutBox angezeigt, „Erstellen" ruft
    onCreate und schließt sich selbst. Controls public für headless Tests.
*/
class LinkSendCreateDialog final : public juce::Component
{
public:
    LinkSendCreateDialog();

    /** Wird mit der gewählten Eingangs-Konfiguration aufgerufen (mindestens
        ein Eingang) — der Aufrufer legt den Node damit an. */
    std::function<void (std::vector<LinkAudioSendModule::InputMode>)> onCreate;

    /** Mono- dann Stereo-Eingänge; garantiert ≥ 1 Eingang. */
    [[nodiscard]] std::vector<LinkAudioSendModule::InputMode> buildModes() const;

    void resized() override;

    static constexpr int maxPerType = 8;

    // public für Tests
    int monoCount   = 0;
    int stereoCount = 1;

    juce::TextButton monoMinus   { juce::String::fromUTF8 ("\xe2\x88\x92") };  // −
    juce::TextButton monoPlus    { "+" };
    juce::TextButton stereoMinus { juce::String::fromUTF8 ("\xe2\x88\x92") };
    juce::TextButton stereoPlus  { "+" };
    juce::TextButton createButton { juce::String::fromUTF8 ("Erstellen") };

private:
    void updateValueLabels();

    juce::Label titleLabel;
    juce::Label monoCaption, stereoCaption;
    juce::Label monoValue, stereoValue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinkSendCreateDialog)
};

} // namespace conduit

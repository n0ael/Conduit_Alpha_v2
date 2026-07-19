#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace conduit
{

//==============================================================================
/**
    Bestätigungs-Dialog für erzwungenes Looper-/Track-Löschen (Big Out
    07/2026): Meldung nennt Clips/Kabel, ✕ schließt ohne Aktion, OK feuert
    onConfirm (Force-Delete → Papierkorb, ~3 min rückgängig).

    Kein Modal-Loop (13.2): wird per CallOutBox angezeigt, beide Buttons
    schließen den Dialog selbst (Muster LinkSendCreateDialog). Controls
    public für headless Tests.
*/
class LooperDeleteConfirmDialog final : public juce::Component
{
public:
    LooperDeleteConfirmDialog (const juce::String& title, const juce::String& message);

    /** OK — der Aufrufer führt das erzwungene Löschen aus. */
    std::function<void()> onConfirm;

    void resized() override;

    // public für Tests
    juce::TextButton cancelButton { juce::String::fromUTF8 ("\xe2\x9c\x95") };  // ✕
    juce::TextButton okButton { "OK" };

private:
    juce::Label titleLabel;
    juce::Label messageLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperDeleteConfirmDialog)
};

} // namespace conduit

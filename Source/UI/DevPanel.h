#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/UiSettings.h"

namespace conduit
{

//==============================================================================
/**
    Schwebendes Dev-Panel (User-Wunsch 07/2026): kleines always-on-top-Fenster
    für Live-UI-Tweaks — Inhalt ist dieselbe UiSettingsComponent wie der
    „Oberfläche"-Settings-Tab (derselbe UiSettings-Broadcaster hält beide
    synchron). Nur im Dev Mode erreichbar (Dev-Tile in der TransportBar).

    Ownership: unique_ptr im EngineEditor. closeButtonPressed meldet nur
    onClose — der Editor destruiert ASYNC (callAsync + SafePointer, ein
    Fenster darf sich nicht aus dem eigenen Callback zerstören) und schließt
    das Panel automatisch, wenn der Dev Mode deaktiviert wird.
*/
class DevPanel final : public juce::DocumentWindow
{
public:
    explicit DevPanel (UiSettings& uiSettingsToUse);

    std::function<void()> onClose;

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevPanel)
};

} // namespace conduit

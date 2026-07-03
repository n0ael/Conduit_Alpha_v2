#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace conduit
{

//==============================================================================
/**
    Modul-Typ-Defaults des Dev-Modus (CLAUDE.md 4.6) — App-Zustand, KEIN
    Patch-Zustand (eigene juce::PropertiesFile "Conduit/ModuleUiDefaults.
    settings", Muster MeterSettings).

    „Als Standard speichern" im Dev-Modus schreibt die aktuellen dsp-
    Parameter-Overrides einer Kachel (userMin/userMax/uiHidden/curve) unter
    ihrem factoryId ab; GraphManager::addModuleNode wendet sie bei der
    NEU-Anlage als Overlay an. Presets/Patches gewinnen immer — geladene
    Nodes bleiben unberührt (Overlay greift nur bei Neu-Anlage).

    Threading: alle Methoden auf dem Message Thread.
*/
class ModuleUiDefaults : public juce::ChangeBroadcaster
{
public:
    /** Eigene Datei neben Meter.settings / OscSend.settings. */
    [[nodiscard]] static juce::PropertiesFile::Options defaultOptions();

    /** Tests injizieren eigene Options (Temp-Verzeichnis). */
    explicit ModuleUiDefaults (const juce::PropertiesFile::Options& options = defaultOptions());
    ~ModuleUiDefaults() override;

    //==========================================================================
    /** Speichert die dsp-Overrides (userMin/userMax/uiHidden/curve) des
        Nodes unter seinem factoryId. Keine Overrides gesetzt → Eintrag
        wird gelöscht (Reset). */
    void captureFromNode (const juce::ValueTree& nodeTree);

    /** Overlay auf einen FRISCH erzeugten Node (vor dem Einhängen):
        setzt gespeicherte Overrides auf passende dsp-Parameter.
        Non-const: PropertiesFile-Zugriff (lazy) ist nicht const. */
    void applyTo (juce::ValueTree& nodeTree);

    [[nodiscard]] bool hasDefaultsFor (const juce::String& factoryId);
    void clearDefaultsFor (const juce::String& factoryId);

private:
    juce::ApplicationProperties applicationProperties;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleUiDefaults)
};

} // namespace conduit

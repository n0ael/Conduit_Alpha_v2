#include <catch2/catch_test_macros.hpp>

#include "UI/EditorDockPanel.h"

namespace
{
    struct ProbeComponent final : public juce::Component {};
}

//==============================================================================
TEST_CASE ("EditorDockPanel: addTab/setActiveTab zeigt genau einen Content sichtbar", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;

    auto first  = std::make_unique<ProbeComponent>();
    auto second = std::make_unique<ProbeComponent>();
    auto* firstPtr  = first.get();
    auto* secondPtr = second.get();

    panel.addTab ("a", "A", std::move (first));
    panel.addTab ("b", "B", std::move (second));

    // Erster hinzugefügter Tab wird automatisch aktiv
    REQUIRE (firstPtr->isVisible());
    REQUIRE_FALSE (secondPtr->isVisible());

    panel.setActiveTab ("b");
    REQUIRE_FALSE (firstPtr->isVisible());
    REQUIRE (secondPtr->isVisible());
}

TEST_CASE ("EditorDockPanel: geschlossen liefert getPreferredWidth 0", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;
    panel.addTab ("a", "A", std::make_unique<ProbeComponent>());   // M5b: ohne sichtbaren Tab ist die Breite immer 0
    panel.setPanelWidth (300);
    panel.setPanelOpen (false);

    REQUIRE (panel.getPreferredWidth() == 0);

    panel.setPanelOpen (true);
    REQUIRE (panel.getPreferredWidth() == 300);
}

TEST_CASE ("EditorDockPanel: onActiveTabChanged feuert nur bei tatsaechlichem Wechsel", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;

    juce::StringArray fired;
    panel.onActiveTabChanged = [&fired] (const juce::String& id) { fired.add (id); };

    // Auto-Aktivierung des ersten Tabs feuert (Callback ist schon gesetzt)
    panel.addTab ("a", "A", std::make_unique<ProbeComponent>());
    REQUIRE (fired.size() == 1);
    REQUIRE (fired[0] == "a");
    REQUIRE (panel.getActiveTabId() == "a");

    // Zweiter Tab aktiviert sich nicht selbst
    panel.addTab ("b", "B", std::make_unique<ProbeComponent>());
    REQUIRE (fired.size() == 1);

    panel.setActiveTab ("b");
    REQUIRE (fired.size() == 2);
    REQUIRE (fired[1] == "b");
    REQUIRE (panel.getActiveTabId() == "b");

    // Gleicher Tab erneut / unbekannte id: kein Callback, kein Wechsel
    panel.setActiveTab ("b");
    panel.setActiveTab ("unbekannt");
    REQUIRE (fired.size() == 2);
    REQUIRE (panel.getActiveTabId() == "b");
}

TEST_CASE ("EditorDockPanel: setPanelWidth klemmt auf [kMinWidth, kMaxWidth]", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;
    panel.addTab ("a", "A", std::make_unique<ProbeComponent>());   // M5b: Breite zählt nur mit sichtbarem Tab
    panel.setPanelOpen (true);

    panel.setPanelWidth (10000);
    REQUIRE (panel.getPreferredWidth() == conduit::EditorDockPanel::kMaxWidth);

    panel.setPanelWidth (-500);
    REQUIRE (panel.getPreferredWidth() == conduit::EditorDockPanel::kMinWidth);
}

//==============================================================================
// MIDI-Rig M5b: Page-Masken + removeTab

TEST_CASE ("EditorDockPanel: Page-Maske blendet Tabs um, aktiver Tab springt auf sichtbar", "[ui][midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;
    panel.setPanelOpen (true);

    auto gridContent = std::make_unique<ProbeComponent>();
    auto mapContent  = std::make_unique<ProbeComponent>();
    auto* gridPtr = gridContent.get();
    auto* mapPtr  = mapContent.get();

    panel.addTab ("mpe", "MPE", std::move (gridContent), 1 << 0);   // nur Page 0 (Grid)
    panel.addTab ("map", "Map", std::move (mapContent));            // alle Pages

    juce::StringArray fired;
    panel.onActiveTabChanged = [&fired] (const juce::String& id) { fired.add (id); };

    // Auf Page 0: erster Tab aktiv, beide Buttons denkbar.
    panel.setActivePage (0);
    REQUIRE (panel.getActiveTabId() == "mpe");
    REQUIRE (gridPtr->isVisible());
    REQUIRE_FALSE (mapPtr->isVisible());

    // Wechsel auf Page 3 (Device): mpe unsichtbar -> map uebernimmt (feuert).
    panel.setActivePage (3);
    REQUIRE (panel.getActiveTabId() == "map");
    REQUIRE (fired.size() == 1);
    REQUIRE (fired[0] == "map");
    REQUIRE (mapPtr->isVisible());
    REQUIRE_FALSE (gridPtr->isVisible());
    REQUIRE (panel.getPreferredWidth() > 0);

    // Zurueck auf Page 0: map bleibt aktiv (weiterhin sichtbar, kein Zwang).
    panel.setActivePage (0);
    REQUIRE (panel.getActiveTabId() == "map");
    REQUIRE (fired.size() == 1);
}

TEST_CASE ("EditorDockPanel: ohne sichtbaren Tab auf der Page ist die Breite 0", "[ui][midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;
    panel.setPanelOpen (true);
    panel.addTab ("mpe", "MPE", std::make_unique<ProbeComponent>(), 1 << 0);

    panel.setActivePage (0);
    REQUIRE (panel.getPreferredWidth() > 0);

    panel.setActivePage (3);
    REQUIRE (panel.getPreferredWidth() == 0);
    REQUIRE (panel.getActiveTabId() == "mpe");   // Auswahl bleibt, nur unsichtbar
}

TEST_CASE ("EditorDockPanel: removeTab entfernt still und aktiviert den ersten sichtbaren", "[ui][midirig]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::EditorDockPanel panel;

    auto second = std::make_unique<ProbeComponent>();
    auto* secondPtr = second.get();

    panel.addTab ("a", "A", std::make_unique<ProbeComponent>());
    panel.addTab ("b", "B", std::move (second));

    juce::StringArray fired;
    panel.onActiveTabChanged = [&fired] (const juce::String& id) { fired.add (id); };

    // Aktiven Tab entfernen: b wird still aktiv (kein Callback -- der
    // Aufrufer ist typischerweise ein Destruktor).
    panel.removeTab ("a");
    REQUIRE (panel.getActiveTabId() == "b");
    REQUIRE (fired.isEmpty());
    REQUIRE (secondPtr->isVisible());

    panel.removeTab ("unbekannt");   // kein Effekt
    REQUIRE (panel.getActiveTabId() == "b");

    panel.removeTab ("b");
    REQUIRE (panel.getActiveTabId().isEmpty());
    REQUIRE (panel.getPreferredWidth() == 0);
}

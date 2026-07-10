#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace conduit
{

//==============================================================================
/**
    App-weit wiederverwendbarer Farbwähler im Push-Stil (Grid-Page v2) —
    gedacht als Inhalt einer juce::CallOutBox (launchAsynchronously, kein
    Modal-Loop). Aufbau vertikal: SV-Fläche (horizontal Sättigung, vertikal
    Value), Hue-Slider (Regenbogen), Preset-Raster 8×5 (Design-Mock-Werte).
    onColourChanged feuert LIVE bei jeder Bewegung (SV-Drag, Hue-Drag,
    Preset-Tap) — der Besitzer wendet die Farbe sofort an.

    Interner Zustand ist HSV (nicht juce::Colour), damit der Hue bei s=0
    oder v=0 nicht verloren geht. HSV↔RGB als pure, testbare statics —
    bewusst eigene Implementierung statt juce::Colour::getHue, damit der
    Roundtrip deterministisch (8-bit-exakt) testbar ist. Hue-Konvention:
    h01 in [0,1), 0 = Rot, wrappt.

    Maus + Touch (normale mouseDown/Drag-Pfade). Message Thread.
*/
class ConduitColorPicker final : public juce::Component
{
public:
    ConduitColorPicker();

    /** Initialfarbe (z. B. aktuelle Achsfarbe) — löst KEINEN Callback aus. */
    void setColour (juce::Colour newColour);
    [[nodiscard]] juce::Colour getColour() const noexcept;

    /** Feuert live bei jeder Änderung (SV-/Hue-Drag, Preset-Tap). */
    std::function<void (juce::Colour)> onColourChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp   (const juce::MouseEvent& event) override;

    //==========================================================================
    // HSV↔RGB als pure statics (Catch2-getestet). h01 in [0,1) (0 = Rot,
    // Werte außerhalb wrappen), s/v in [0,1] (werden geklemmt).

    /** HSV → opake Farbe. */
    [[nodiscard]] static juce::Colour fromHsv (float h01, float s, float v) noexcept;

    /** Farbe → HSV. Grau (s == 0): h01 = 0 (Konvention). */
    static void toHsv (juce::Colour colour, float& h01, float& s, float& v) noexcept;

    static constexpr int kPreferredWidth   = 226;
    static constexpr int kNumPresetColumns = 8;
    static constexpr int kNumPresetRows    = 5;

private:
    enum class DragZone { none, svField, hueSlider };

    void updateFromSv (juce::Point<float> pos);
    void updateFromHue (float x);
    void applyPresetAt (juce::Point<int> pos);
    void notifyChange();

    [[nodiscard]] juce::Rectangle<int> presetCellBounds (int index) const noexcept;

    static constexpr int   kPadding           = 10;
    static constexpr int   kSvHeight          = 180;
    static constexpr int   kHueHeight         = 14;
    static constexpr int   kHueInsetX         = 4;
    static constexpr int   kSectionGap        = 12;
    static constexpr int   kPresetGap         = 4;
    static constexpr float kCornerRadius      = 6.0f;
    static constexpr float kSvCornerRadius    = 4.0f;
    static constexpr float kPresetCornerRadius = 3.0f;
    static constexpr float kSvHandleDiameter  = 14.0f;
    static constexpr float kHueHandleDiameter = 16.0f;
    static constexpr float kHandleStroke      = 2.5f;

    float hue = 0.0f, saturation = 1.0f, value = 1.0f;
    DragZone activeZone = DragZone::none;

    juce::Rectangle<int> svBounds, hueBounds, presetBounds;   // gesetzt in resized()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConduitColorPicker)
};

} // namespace conduit

#include "ConduitColorPicker.h"

#include <array>
#include <cmath>

#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    // Preset-Raster 8×5 (Design-Mock Grid-Page v2) — reine Daten (0xRRGGBB),
    // zeilenweise wie im Mock.
    constexpr std::array<juce::uint32, 40> presetColours {
        0xff8a9eu, 0xff5d52u, 0xffb300u, 0xffe14du, 0xaaff4du, 0x4dff6eu, 0x4dffd2u, 0xbfe8ffu,
        0xff4066u, 0xff2e1fu, 0xff8c00u, 0xffd400u, 0x7ddc1fu, 0x00d95fu, 0x00cfc4u, 0x7fb2ffu,
        0xd92662u, 0xe01f1fu, 0xe07000u, 0xe0b000u, 0x58b010u, 0x00a848u, 0x00a4b0u, 0x4d7fe0u,
        0x8f1d45u, 0x991414u, 0x994d00u, 0x997a00u, 0x3a7a0au, 0x00702fu, 0x007078u, 0x33549eu,
        0xb56576u, 0xa0522du, 0x8a6d3bu, 0x7a7a52u, 0x557a55u, 0x4f7a72u, 0x5a6e8cu, 0xf0f0f0u,
    };

    [[nodiscard]] juce::Colour opaque (juce::uint32 rgb) noexcept
    {
        return juce::Colour (0xff000000u | (rgb & 0x00ffffffu));
    }
}

//==============================================================================
ConduitColorPicker::ConduitColorPicker()
{
    // Feste bevorzugte Größe: Breite aus dem Mock, Höhe aus dem vertikalen
    // Aufbau (SV-Fläche + Hue-Slider + Preset-Raster mit quadratischen
    // Zellen aus der Inhaltsbreite abgeleitet).
    const auto contentWidth = kPreferredWidth - 2 * kPadding;
    const auto cellSize     = (contentWidth - (kNumPresetColumns - 1) * kPresetGap) / kNumPresetColumns;
    const auto presetHeight = kNumPresetRows * cellSize + (kNumPresetRows - 1) * kPresetGap;

    setSize (kPreferredWidth,
             kPadding + kSvHeight + kSectionGap + kHueHeight + kSectionGap + presetHeight + kPadding);
}

void ConduitColorPicker::setColour (juce::Colour newColour)
{
    toHsv (newColour, hue, saturation, value);
    repaint();
}

juce::Colour ConduitColorPicker::getColour() const noexcept
{
    return fromHsv (hue, saturation, value);
}

//==============================================================================
juce::Colour ConduitColorPicker::fromHsv (float h01, float s, float v) noexcept
{
    s = juce::jlimit (0.0f, 1.0f, s);
    v = juce::jlimit (0.0f, 1.0f, v);

    const auto wrapped = h01 - std::floor (h01);   // [0,1)
    const auto h       = wrapped * 6.0f;           // [0,6)
    const auto sector  = juce::jlimit (0, 5, (int) h);
    const auto f       = h - (float) sector;

    const auto p = v * (1.0f - s);
    const auto q = v * (1.0f - s * f);
    const auto t = v * (1.0f - s * (1.0f - f));

    float r = v, g = v, b = v;
    switch (sector)
    {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;   // 5
    }

    return juce::Colour::fromFloatRGBA (r, g, b, 1.0f);
}

void ConduitColorPicker::toHsv (juce::Colour colour, float& h01, float& s, float& v) noexcept
{
    const auto r = colour.getFloatRed();
    const auto g = colour.getFloatGreen();
    const auto b = colour.getFloatBlue();

    const auto mx    = juce::jmax (r, g, b);
    const auto mn    = juce::jmin (r, g, b);
    const auto delta = mx - mn;

    v = mx;
    s = mx > 0.0f ? delta / mx : 0.0f;

    if (delta <= 0.0f)
    {
        h01 = 0.0f;   // Grau: Hue undefiniert → 0 (Konvention)
        return;
    }

    float h = 0.0f;
    if (mx == r)
        h = (g - b) / delta;            // kann negativ sein → unten wrappen
    else if (mx == g)
        h = (b - r) / delta + 2.0f;
    else
        h = (r - g) / delta + 4.0f;

    h01 = h / 6.0f;
    if (h01 < 0.0f)
        h01 += 1.0f;
}

//==============================================================================
void ConduitColorPicker::resized()
{
    auto area = getLocalBounds().reduced (kPadding);

    svBounds = area.removeFromTop (kSvHeight);
    area.removeFromTop (kSectionGap);
    hueBounds = area.removeFromTop (kHueHeight).reduced (kHueInsetX, 0);
    area.removeFromTop (kSectionGap);
    presetBounds = area;
}

juce::Rectangle<int> ConduitColorPicker::presetCellBounds (int index) const noexcept
{
    const auto cellSize = (presetBounds.getWidth() - (kNumPresetColumns - 1) * kPresetGap)
                              / kNumPresetColumns;
    const auto column = index % kNumPresetColumns;
    const auto row    = index / kNumPresetColumns;

    return { presetBounds.getX() + column * (cellSize + kPresetGap),
             presetBounds.getY() + row * (cellSize + kPresetGap),
             cellSize, cellSize };
}

void ConduitColorPicker::paint (juce::Graphics& g)
{
    // Fläche + Kontur (Push-Kachel-Stil)
    const auto tile = getLocalBounds().toFloat();
    g.setColour (push::colours::tile);
    g.fillRoundedRectangle (tile, kCornerRadius);
    g.setColour (push::colours::outline);
    g.drawRoundedRectangle (tile.reduced (0.5f), kCornerRadius, 1.0f);

    // SV-Fläche: zwei übereinandergelegte Gradients — horizontal weiß →
    // voller Hue, vertikal transparent → schwarz. Clip auf runde Ecken.
    const auto sv = svBounds.toFloat();
    {
        juce::Graphics::ScopedSaveState clipState (g);
        juce::Path clip;
        clip.addRoundedRectangle (sv, kSvCornerRadius);
        g.reduceClipRegion (clip);

        juce::ColourGradient horizontal (juce::Colours::white, sv.getTopLeft(),
                                         fromHsv (hue, 1.0f, 1.0f), sv.getTopRight(), false);
        g.setGradientFill (horizontal);
        g.fillRect (sv);

        juce::ColourGradient vertical (juce::Colours::transparentBlack, sv.getTopLeft(),
                                       juce::Colours::black, sv.getBottomLeft(), false);
        g.setGradientFill (vertical);
        g.fillRect (sv);
    }

    // SV-Handle: Füllung = aktuelle Farbe, weiße Kontur, dünner dunkler
    // Außenring (Mock: 1 px rgba(0,0,0,.6)).
    const auto handleCentre = juce::Point<float> (sv.getX() + saturation * sv.getWidth(),
                                                  sv.getY() + (1.0f - value) * sv.getHeight());
    const auto svHandle = juce::Rectangle<float> (kSvHandleDiameter, kSvHandleDiameter)
                              .withCentre (handleCentre);

    g.setColour (getColour());
    g.fillEllipse (svHandle);
    g.setColour (push::colours::ledWhite);
    g.drawEllipse (svHandle, kHandleStroke);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (svHandle.expanded (kHandleStroke * 0.5f + 0.5f), 1.0f);

    // Hue-Slider: Regenbogen (rot→gelb→grün→cyan→blau→magenta→rot) als
    // Pille, Handle = reiner Hue mit weißer Kontur.
    const auto hueTrack = hueBounds.toFloat();
    {
        juce::Graphics::ScopedSaveState clipState (g);
        juce::Path clip;
        clip.addRoundedRectangle (hueTrack, hueTrack.getHeight() * 0.5f);
        g.reduceClipRegion (clip);

        juce::ColourGradient rainbow (fromHsv (0.0f, 1.0f, 1.0f), hueTrack.getTopLeft(),
                                      fromHsv (0.0f, 1.0f, 1.0f), hueTrack.getTopRight(), false);
        for (int i = 1; i < 6; ++i)
            rainbow.addColour ((double) i / 6.0, fromHsv ((float) i / 6.0f, 1.0f, 1.0f));

        g.setGradientFill (rainbow);
        g.fillRect (hueTrack);
    }

    const auto hueCentre = juce::Point<float> (hueTrack.getX() + hue * hueTrack.getWidth(),
                                               hueTrack.getCentreY());
    const auto hueHandle = juce::Rectangle<float> (kHueHandleDiameter, kHueHandleDiameter)
                               .withCentre (hueCentre);

    g.setColour (fromHsv (hue, 1.0f, 1.0f));
    g.fillEllipse (hueHandle);
    g.setColour (push::colours::ledWhite);
    g.drawEllipse (hueHandle, kHandleStroke);

    // Preset-Raster: aktive Zelle (== aktuelle Farbe) mit weißer Kontur.
    const auto current = getColour();
    for (int i = 0; i < (int) presetColours.size(); ++i)
    {
        const auto cell   = presetCellBounds (i).toFloat();
        const auto colour = opaque (presetColours[(size_t) i]);

        g.setColour (colour);
        g.fillRoundedRectangle (cell, kPresetCornerRadius);

        g.setColour (colour == current ? push::colours::ledWhite : push::colours::outline);
        g.drawRoundedRectangle (cell.reduced (0.75f), kPresetCornerRadius, 1.5f);
    }
}

//==============================================================================
void ConduitColorPicker::updateFromSv (juce::Point<float> pos)
{
    const auto sv = svBounds.toFloat();
    if (sv.isEmpty())
        return;

    saturation = juce::jlimit (0.0f, 1.0f, (pos.x - sv.getX()) / sv.getWidth());
    value      = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - sv.getY()) / sv.getHeight());
    notifyChange();
}

void ConduitColorPicker::updateFromHue (float x)
{
    const auto track = hueBounds.toFloat();
    if (track.isEmpty())
        return;

    hue = juce::jlimit (0.0f, 1.0f, (x - track.getX()) / track.getWidth());
    notifyChange();
}

void ConduitColorPicker::applyPresetAt (juce::Point<int> pos)
{
    for (int i = 0; i < (int) presetColours.size(); ++i)
    {
        // Hit-Zone großzügig: Zelle + halber Gap ringsum (Touch).
        if (presetCellBounds (i).expanded (kPresetGap / 2 + 1).contains (pos))
        {
            toHsv (opaque (presetColours[(size_t) i]), hue, saturation, value);
            notifyChange();
            return;
        }
    }
}

void ConduitColorPicker::notifyChange()
{
    repaint();

    if (onColourChanged != nullptr)
        onColourChanged (getColour());
}

void ConduitColorPicker::mouseDown (const juce::MouseEvent& event)
{
    if (svBounds.contains (event.getPosition()))
    {
        activeZone = DragZone::svField;
        updateFromSv (event.position);
    }
    // Hue-Zone vertikal großzügiger als die 14-px-Pille (Touch).
    else if (hueBounds.expanded (kHueInsetX, kSectionGap / 2).contains (event.getPosition()))
    {
        activeZone = DragZone::hueSlider;
        updateFromHue (event.position.x);
    }
    else if (presetBounds.contains (event.getPosition()))
    {
        activeZone = DragZone::none;
        applyPresetAt (event.getPosition());
    }
}

void ConduitColorPicker::mouseDrag (const juce::MouseEvent& event)
{
    switch (activeZone)
    {
        case DragZone::svField:   updateFromSv (event.position); break;
        case DragZone::hueSlider: updateFromHue (event.position.x); break;
        case DragZone::none:
        default:                  break;
    }
}

void ConduitColorPicker::mouseUp (const juce::MouseEvent&)
{
    activeZone = DragZone::none;
}

} // namespace conduit

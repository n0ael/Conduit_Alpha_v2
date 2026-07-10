#include "CcPanel.h"

#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int kPaddingY    = 6;    // Padding vertikal (Design-Mock)
    constexpr int kSectionGap  = 8;    // Hinweis → Werkzeugraster
    constexpr int kTileGap     = 6;    // Abstand im 2×2-Raster
    constexpr int kTileHeight  = 64;
    constexpr int kHintHeight  = 44;   // ~3 Zeilen à scaledFont(11)
    constexpr float kIconSide  = 22.0f;

    // TODO(design): horizontales Padding ist im Mock nicht spezifiziert —
    // konservativ 8 px, damit der Hinweistext nicht an der Kante klebt.
    constexpr int kPaddingX = 8;
}

CcPanel::CcPanel()
{
    tiles = { { { grid::CcTool::fader,  "Fader",  {} },
               { grid::CcTool::push,   "Push",   {} },
               { grid::CcTool::toggle, "Toggle", {} },
               { grid::CcTool::xy,     "XY-Pad", {} } } };

    setWantsKeyboardFocus (false);
    setInterceptsMouseClicks (true, false);
}

void CcPanel::resized()
{
    auto bounds = getLocalBounds().reduced (kPaddingX, kPaddingY);

    bounds.removeFromTop (kHintHeight);   // Hinweistext (paint)
    bounds.removeFromTop (kSectionGap);

    auto gridArea   = bounds;
    const auto tileWidth = (gridArea.getWidth() - kTileGap) / 2;

    auto topRow = gridArea.removeFromTop (kTileHeight);
    tiles[0].bounds = topRow.removeFromLeft (tileWidth);
    topRow.removeFromLeft (kTileGap);
    tiles[1].bounds = topRow.removeFromLeft (tileWidth);

    gridArea.removeFromTop (kTileGap);

    auto bottomRow = gridArea.removeFromTop (kTileHeight);
    tiles[2].bounds = bottomRow.removeFromLeft (tileWidth);
    bottomRow.removeFromLeft (kTileGap);
    tiles[3].bounds = bottomRow.removeFromLeft (tileWidth);
}

void CcPanel::paint (juce::Graphics& g)
{
    const auto hintArea = getLocalBounds().reduced (kPaddingX, kPaddingY)
                              .removeFromTop (kHintHeight);

    g.setColour (push::colours::textDim);
    g.setFont (push::scaledFont (11.0f));
    // minimumHorizontalScale 1.0: Schrift wird NIE gestaucht (CLAUDE.md 10).
    // ×(U+00D7) statt ✕(U+2715): sicher im Jost-Font vorhanden, optisch
    // gleichwertig (Anti-Tofu; Nicht-ASCII immer via fromUTF8, Rule ui-design).
    g.drawFittedText (juce::String::fromUTF8 ("Control w\xc3\xa4hlen, dann im Raster "
                                              "aufziehen. Ziehen verschiebt, "
                                              "\xc3\x97 entfernt."),
                      hintArea, juce::Justification::topLeft, 3, 1.0f);

    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const auto& tile = tiles[i];
        const auto tileBounds = tile.bounds.toFloat();
        const auto isActive   = tile.tool == activeTool;
        const auto isHovered  = (int) i == hoveredTile;

        g.setColour (isHovered ? push::colours::tileActive : push::colours::tile);
        g.fillRoundedRectangle (tileBounds, 6.0f);

        g.setColour (isActive ? push::colours::ledWhite : push::colours::outline);
        g.drawRoundedRectangle (tileBounds.reduced (0.75f), 6.0f, 1.5f);

        // Icon (~22 px) mittig über dem Label.
        auto inner = tileBounds.reduced (4.0f);
        auto labelArea = inner.removeFromBottom (16.0f);
        const auto iconBounds = juce::Rectangle<float> (kIconSide, kIconSide)
                                    .withCentre (inner.getCentre());

        g.setColour (isActive ? push::colours::ledWhite : push::colours::text);
        drawToolIcon (g, tile.tool, iconBounds);

        g.setColour (push::colours::text);
        g.setFont (push::scaledFont (11.0f));
        g.drawText (tile.label, labelArea, juce::Justification::centred, false);
    }
}

void CcPanel::drawToolIcon (juce::Graphics& g, grid::CcTool tool,
                            juce::Rectangle<float> iconBounds)
{
    // Vektorgeometrie relativ zu iconBounds (PushIcons-Konvention: normiert
    // gedacht, hier direkt in Zielkoordinaten skaliert — keine Bitmaps).
    const auto b = iconBounds;
    const auto stroke = juce::jmax (1.5f, b.getWidth() * 0.07f);

    switch (tool)
    {
        case grid::CcTool::fader:
        {
            // vertikale Schiene + horizontaler Griff
            const auto cx = b.getCentreX();
            g.drawLine (cx, b.getY(), cx, b.getBottom(), stroke);
            const auto handle = juce::Rectangle<float> (b.getWidth() * 0.7f, b.getHeight() * 0.2f)
                                    .withCentre ({ cx, b.getY() + b.getHeight() * 0.4f });
            g.fillRoundedRectangle (handle, handle.getHeight() * 0.3f);
            break;
        }

        case grid::CcTool::push:
            // Kreis-Outline + gefüllter Innenkreis
            g.drawEllipse (b.reduced (stroke * 0.5f), stroke);
            g.fillEllipse (b.withSizeKeepingCentre (b.getWidth() * 0.45f,
                                                    b.getHeight() * 0.45f));
            break;

        case grid::CcTool::toggle:
        {
            // abgerundetes Rechteck-Outline + gefüllter Kreis rechts
            const auto pill = b.withSizeKeepingCentre (b.getWidth(), b.getHeight() * 0.5f)
                                  .reduced (stroke * 0.5f);
            g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, stroke);
            const auto knobDiameter = pill.getHeight() * 0.55f;
            g.fillEllipse (juce::Rectangle<float> (knobDiameter, knobDiameter)
                               .withCentre ({ pill.getRight() - pill.getHeight() * 0.5f,
                                              pill.getCentreY() }));
            break;
        }

        case grid::CcTool::xy:
        {
            // Rechteck-Outline + gefüllter Punkt versetzt
            g.drawRoundedRectangle (b.reduced (stroke * 0.5f), 2.0f, stroke);
            const auto dotDiameter = b.getWidth() * 0.22f;
            g.fillEllipse (juce::Rectangle<float> (dotDiameter, dotDiameter)
                               .withCentre ({ b.getX() + b.getWidth() * 0.62f,
                                              b.getY() + b.getHeight() * 0.38f }));
            break;
        }

        case grid::CcTool::none:
            break;
    }
}

int CcPanel::tileIndexAt (juce::Point<int> position) const noexcept
{
    for (size_t i = 0; i < tiles.size(); ++i)
        if (tiles[i].bounds.contains (position))
            return (int) i;

    return -1;
}

void CcPanel::mouseDown (const juce::MouseEvent& event)
{
    const auto index = tileIndexAt (event.getPosition());
    if (index < 0)
        return;

    const auto tapped = tiles[(size_t) index].tool;
    activeTool = (activeTool == tapped) ? grid::CcTool::none : tapped;
    repaint();

    if (onToolChanged != nullptr)
        onToolChanged (activeTool);
}

void CcPanel::mouseMove (const juce::MouseEvent& event)
{
    const auto index = tileIndexAt (event.getPosition());
    if (index == hoveredTile)
        return;

    hoveredTile = index;
    repaint();
}

void CcPanel::mouseExit (const juce::MouseEvent&)
{
    if (hoveredTile < 0)
        return;

    hoveredTile = -1;
    repaint();
}

} // namespace conduit

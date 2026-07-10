#include "PushIcons.h"

namespace conduit::push
{

namespace
{

//==============================================================================
// Geometrie im normierten 0..1-Quadrat. Strokes werden erst beim Zeichnen
// skaliert — die Pfade hier sind reine Mittellinien bzw. Umrisse.

juce::Path playTriangle()
{
    juce::Path p;
    p.addTriangle (0.20f, 0.10f, 0.20f, 0.90f, 0.88f, 0.50f);
    return p;
}

juce::Path tapeLoopOutline()
{
    // o͞o (PS-Referenz 04.07.): zwei sich berührende Spulen, die Bandkante
    // liegt DIREKT auf den Kreisen — kompakt und exakt zentriert
    juce::Path p;
    p.addEllipse (0.14f, 0.34f, 0.36f, 0.36f);
    p.addEllipse (0.50f, 0.34f, 0.36f, 0.36f);
    p.startNewSubPath (0.12f, 0.30f);
    p.lineTo (0.88f, 0.30f);
    return p;
}

juce::Path captureCorners()
{
    // ⛶ — vier Eckwinkel wie auf dem Push-Capture-Button
    constexpr auto inset = 0.10f;
    constexpr auto arm   = 0.26f;

    juce::Path p;
    // oben links
    p.startNewSubPath (inset, inset + arm);
    p.lineTo (inset, inset);
    p.lineTo (inset + arm, inset);
    // oben rechts
    p.startNewSubPath (1.0f - inset - arm, inset);
    p.lineTo (1.0f - inset, inset);
    p.lineTo (1.0f - inset, inset + arm);
    // unten rechts
    p.startNewSubPath (1.0f - inset, 1.0f - inset - arm);
    p.lineTo (1.0f - inset, 1.0f - inset);
    p.lineTo (1.0f - inset - arm, 1.0f - inset);
    // unten links
    p.startNewSubPath (inset + arm, 1.0f - inset);
    p.lineTo (inset, 1.0f - inset);
    p.lineTo (inset, 1.0f - inset - arm);
    return p;
}

juce::Path metronomeOutline()
{
    // ○● — Kreis-Outline links; der gefüllte Punkt rechts kommt in draw().
    // Größer + exakt mittig (PS-Referenz 04.07.)
    juce::Path p;
    p.addEllipse (0.03f, 0.28f, 0.44f, 0.44f);
    return p;
}

juce::Path metronomeDot()
{
    juce::Path p;
    p.addEllipse (0.53f, 0.28f, 0.44f, 0.44f);
    return p;
}

juce::Path plusSign()
{
    juce::Path p;
    p.startNewSubPath (0.50f, 0.12f);
    p.lineTo (0.50f, 0.88f);
    p.startNewSubPath (0.12f, 0.50f);
    p.lineTo (0.88f, 0.50f);
    return p;
}

juce::Path gearSun()
{
    // ☼ wie auf dem Push (Setup): Kreis mit acht kurzen Strahlen
    juce::Path p;
    p.addEllipse (0.30f, 0.30f, 0.40f, 0.40f);

    const juce::Point<float> centre (0.50f, 0.50f);
    for (int ray = 0; ray < 8; ++ray)
    {
        const auto angle = juce::MathConstants<float>::twoPi * (float) ray / 8.0f;
        const auto dir   = juce::Point<float> (std::sin (angle), -std::cos (angle));
        p.startNewSubPath (centre + dir * 0.30f);
        p.lineTo (centre + dir * 0.44f);
    }
    return p;
}

juce::Path nudgeBars (bool towardsRight)
{
    // Live-Phase-Nudge (PS-Referenz 04.07.): vier AUFRECHTE dicke Balken
    // (Füllung, kein Stroke), die Abstände verdichten sich in Nudge-
    // Richtung (Doppler) — beide Richtungen exakt gespiegelt/zentriert
    juce::Path p;
    constexpr float centres[] = { 0.14f, 0.42f, 0.64f, 0.84f };   // rechts verdichtet
    constexpr float barWidth = 0.10f;

    for (const auto centre : centres)
    {
        const auto x = towardsRight ? centre : 1.0f - centre;
        p.addRectangle (x - barWidth * 0.5f, 0.22f, barWidth, 0.56f);
    }
    return p;
}

juce::Path browserPanelOutline()
{
    // Browser-Toggle wie im Live-Header, GESPIEGELT (Panel rechts —
    // Conduits Browser klappt rechts auf, User 03.07.)
    juce::Path p;
    p.addRoundedRectangle (0.08f, 0.20f, 0.84f, 0.60f, 0.08f);
    return p;
}

juce::Path browserPanelFill()
{
    juce::Path p;
    p.addRoundedRectangle (0.60f, 0.26f, 0.26f, 0.48f, 0.04f);
    return p;
}

juce::Path chevronDownSmall()
{
    juce::Path p;
    p.addTriangle (0.22f, 0.36f, 0.78f, 0.36f, 0.50f, 0.72f);
    return p;
}

juce::Path mixerBars()
{
    // Live-Header-Optik (User 03.07.): Säulen unterschiedlicher Höhe von
    // der Grundlinie — wie ein stehendes Level-Meter „ılıl"
    juce::Path p;
    const float xs[]   = { 0.20f, 0.40f, 0.60f, 0.80f };
    const float tops[] = { 0.48f, 0.16f, 0.36f, 0.24f };

    for (int i = 0; i < 4; ++i)
    {
        p.startNewSubPath (xs[i], 0.88f);
        p.lineTo (xs[i], tops[i]);
    }
    return p;
}

juce::Path clipBoxOutline()
{
    juce::Path p;
    p.addRoundedRectangle (0.08f, 0.16f, 0.84f, 0.68f, 0.10f);
    return p;
}

juce::Path clipBoxTriangle()
{
    // zentriert und größer (Live-Header-Optik, User 03.07.)
    juce::Path p;
    p.addTriangle (0.40f, 0.32f, 0.40f, 0.68f, 0.70f, 0.50f);
    return p;
}

juce::Path deviceLines()
{
    juce::Path p;
    for (const auto x : { 0.26f, 0.50f, 0.74f })
    {
        p.startNewSubPath (x, 0.12f);
        p.lineTo (x, 0.88f);
    }
    return p;
}

juce::Path minusSign()
{
    juce::Path p;
    p.startNewSubPath (0.12f, 0.50f);
    p.lineTo (0.88f, 0.50f);
    return p;
}

juce::Path eyeOutline (bool crossedOut)
{
    // Mandelform (zwei Bögen) + Pupille; optional Diagonalstrich
    juce::Path p;
    p.startNewSubPath (0.06f, 0.50f);
    p.quadraticTo (0.50f, 0.10f, 0.94f, 0.50f);
    p.quadraticTo (0.50f, 0.90f, 0.06f, 0.50f);
    p.closeSubPath();
    p.addEllipse (0.38f, 0.38f, 0.24f, 0.24f);

    if (crossedOut)
    {
        p.startNewSubPath (0.14f, 0.88f);
        p.lineTo (0.86f, 0.12f);
    }

    return p;
}

juce::Path valueButtonsGrid()
{
    // 2×2 abgerundete Kacheln — die Wert-Button-Stapel in Miniatur
    juce::Path p;

    for (const auto y : { 0.12f, 0.54f })
        for (const auto x : { 0.12f, 0.54f })
            p.addRoundedRectangle (x, y, 0.34f, 0.34f, 0.06f);

    return p;
}

juce::Path faderTrack()
{
    // Vertikale Bahn — der Griffstein kommt als Füllung in draw()
    juce::Path p;
    p.startNewSubPath (0.50f, 0.08f);
    p.lineTo (0.50f, 0.92f);
    return p;
}

juce::Path faderThumb()
{
    juce::Path p;
    p.addRoundedRectangle (0.26f, 0.38f, 0.48f, 0.20f, 0.05f);
    return p;
}

juce::Path curveSweep()
{
    // Bezier-S-Kurve wie im CurveEditor
    juce::Path p;
    p.startNewSubPath (0.10f, 0.86f);
    p.cubicTo (0.60f, 0.86f, 0.40f, 0.14f, 0.90f, 0.14f);
    return p;
}

juce::Path sharpSign()
{
    // ♯ — zwei leicht geneigte Vertikalen, zwei steigende Querbalken
    juce::Path p;
    p.startNewSubPath (0.40f, 0.10f);
    p.lineTo (0.34f, 0.90f);
    p.startNewSubPath (0.66f, 0.10f);
    p.lineTo (0.60f, 0.90f);
    p.startNewSubPath (0.14f, 0.42f);
    p.lineTo (0.86f, 0.32f);
    p.startNewSubPath (0.14f, 0.70f);
    p.lineTo (0.86f, 0.60f);
    return p;
}

juce::Path browserProjectsFolder()
{
    // Ordner mit Tab oben links (Referenz: Assets/svg-browser-icons/projects.svg)
    juce::Path p;
    p.startNewSubPath (0.10f, 0.30f);
    p.lineTo (0.10f, 0.20f);
    p.lineTo (0.40f, 0.20f);
    p.lineTo (0.48f, 0.30f);
    p.lineTo (0.90f, 0.30f);
    p.lineTo (0.90f, 0.78f);
    p.lineTo (0.10f, 0.78f);
    p.closeSubPath();
    return p;
}

juce::Path browserAudioWave()
{
    // Waveform: fünf Balken symmetrisch um die Mittellinie (Fill wie Nudge)
    // (Referenz: Assets/svg-browser-icons/audio.svg)
    juce::Path p;
    constexpr float xs[]      = { 0.14f, 0.32f, 0.50f, 0.68f, 0.86f };
    constexpr float halves[]  = { 0.14f, 0.30f, 0.38f, 0.24f, 0.10f };
    constexpr float barWidth  = 0.09f;

    for (int i = 0; i < 5; ++i)
        p.addRectangle (xs[i] - barWidth * 0.5f, 0.50f - halves[i],
                        barWidth, halves[i] * 2.0f);
    return p;
}

juce::Path browserCvSine()
{
    // Eine Sinusperiode — CV/Control-Ast
    // (Referenz: Assets/svg-browser-icons/modules-cv-control.svg)
    juce::Path p;
    p.startNewSubPath (0.08f, 0.50f);
    p.cubicTo (0.20f, 0.10f, 0.34f, 0.10f, 0.50f, 0.50f);
    p.cubicTo (0.66f, 0.90f, 0.80f, 0.90f, 0.92f, 0.50f);
    return p;
}

juce::Path browserFxKnob()
{
    // Drehknopf: Kreis + Zeiger nach oben rechts — AudioFX-Ast
    // (Referenz: Assets/svg-browser-icons/modules-audiofx.svg)
    juce::Path p;
    p.addEllipse (0.16f, 0.16f, 0.68f, 0.68f);
    p.startNewSubPath (0.50f, 0.50f);
    p.lineTo (0.70f, 0.28f);
    return p;
}

juce::Path searchLens()
{
    // Lupe: Kreis oben links + Griff nach unten rechts
    // (Referenz: Assets/svg-browser-icons/search.svg)
    juce::Path p;
    p.addEllipse (0.14f, 0.14f, 0.48f, 0.48f);
    p.startNewSubPath (0.60f, 0.60f);
    p.lineTo (0.86f, 0.86f);
    return p;
}

juce::Path chevronLeftArrow()
{
    // ‹ — Zurück im Breadcrumb-Header
    juce::Path p;
    p.startNewSubPath (0.62f, 0.20f);
    p.lineTo (0.34f, 0.50f);
    p.lineTo (0.62f, 0.80f);
    return p;
}

juce::Path gridLoop()
{
    // Offener Ring wie im Live-Header (User 03.07.): Bogen mit Öffnung
    // unten, ohne Füßchen
    juce::Path p;
    p.addCentredArc (0.50f, 0.50f, 0.36f, 0.36f, 0.0f,
                     juce::MathConstants<float>::pi * 1.22f,
                     juce::MathConstants<float>::pi * 2.78f, true);
    return p;
}

juce::Path touchLiveStrips()
{
    // TouchLive-Page (User-SVG TouchLive.svg, 09.07.2026): drei Mini-
    // Kanalzüge — je drei Querstriche über einem Fader-Stem. Rects 1:1 aus
    // der 44er-ViewBox, Inhalt (x 14..30, y 13..31) zentriert hochskaliert.
    juce::Path p;

    const auto addSvgRect = [&p] (float x, float y, float w, float h)
    {
        constexpr float scale = 0.68f / 18.0f;             // Inhaltshöhe → 0.68
        constexpr float x0 = (1.0f - 16.0f * scale) / 2.0f;
        constexpr float y0 = (1.0f - 18.0f * scale) / 2.0f;
        p.addRectangle (x0 + (x - 14.0f) * scale, y0 + (y - 13.0f) * scale,
                        w * scale, h * scale);
    };

    for (const auto columnX : { 14.0f, 20.0f, 26.0f })
    {
        addSvgRect (columnX, 13.0f, 4.0f, 2.0f);
        addSvgRect (columnX, 17.0f, 4.0f, 2.0f);
        addSvgRect (columnX, 21.0f, 4.0f, 2.0f);
        addSvgRect (columnX + 1.0f, 25.0f, 2.0f, 6.0f);    // Stem
    }

    return p;
}

//==============================================================================
// Grid-Page-Symbole (User-SVGs 10.07.2026, viewBox 44×44 — das 44er-Rect ist
// die KACHEL, nur die Glyph-Formen zählen): Inhalt (x 14..32, y 13..31,
// 18×18) zentriert hochskaliert wie touchLiveStrips (Hausstil für die
// User-SVG-Familie — reine /44-Normierung bliebe nach dem IconTile-Inset
// unlesbar klein).

void addGridSvgRect (juce::Path& p, float x, float y, float w, float h)
{
    constexpr float scale = 0.68f / 18.0f;              // Inhaltshöhe → 0.68
    constexpr float x0 = (1.0f - 18.0f * scale) / 2.0f;
    constexpr float y0 = (1.0f - 18.0f * scale) / 2.0f;
    p.addRectangle (x0 + (x - 14.0f) * scale, y0 + (y - 13.0f) * scale,
                    w * scale, h * scale);
}

juce::Path gridMpeMatrix()
{
    // 5×5-Punktmatrix aus 2×2-Rects (x/y-Zentren im 4er-Raster ab 14/13) —
    // Grid-Page-Tab + 64-Pad-Modus (Push-Style).
    juce::Path p;

    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 5; ++col)
            addGridSvgRect (p, 14.0f + 4.0f * (float) col,
                            13.0f + 4.0f * (float) row, 2.0f, 2.0f);

    return p;
}

juce::Path gridMpeXyGlyph()
{
    // XY+Fader-Modus: untere 3 Punktreihen + oben links XY-Block (1×1-
    // Handle-Aussparung) + oben rechts Fader-Block (drei 2×4-Schlitze).
    // Aussparungen per Even-Odd-Füllregel — wie die fill-rule des User-SVGs
    // (PushIcons hatte bisher keine Fill-Cutouts, das ist der neue Hausweg).
    juce::Path p;
    p.setUsingNonZeroWinding (false);   // Even-Odd: innere Rects werden Löcher

    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 5; ++col)
            addGridSvgRect (p, 14.0f + 4.0f * (float) col,
                            21.0f + 4.0f * (float) row, 2.0f, 2.0f);

    // XY-Block (14,13)-(20,19) minus 1×1-Loch bei (16,17)
    addGridSvgRect (p, 14.0f, 13.0f, 6.0f, 6.0f);
    addGridSvgRect (p, 16.0f, 17.0f, 1.0f, 1.0f);

    // Fader-Block (22,13)-(32,19) minus drei 2×4-Schlitze bei x=23/26/29
    addGridSvgRect (p, 22.0f, 13.0f, 10.0f, 6.0f);
    addGridSvgRect (p, 23.0f, 14.0f, 2.0f, 4.0f);
    addGridSvgRect (p, 26.0f, 14.0f, 2.0f, 4.0f);
    addGridSvgRect (p, 29.0f, 14.0f, 2.0f, 4.0f);

    return p;
}

//==============================================================================
juce::AffineTransform fitToBounds (const juce::Rectangle<float>& bounds)
{
    // Größtes einbeschriebenes Quadrat, zentriert — Icons bleiben proportional
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto target = bounds.withSizeKeepingCentre (side, side);
    return juce::AffineTransform::scale (side, side)
               .translated (target.getX(), target.getY());
}

juce::Path strokeGeometry (Icon icon)
{
    switch (icon)
    {
        case Icon::play:         return playTriangle();
        case Icon::tapeLoop:     return tapeLoopOutline();
        case Icon::captureFrame: return captureCorners();
        case Icon::metronome:    return metronomeOutline();
        case Icon::plus:         return plusSign();
        case Icon::gear:         return gearSun();

        // reine Fill-Icons (dicke Balken/Dreieck) — kein Stroke obendrauf
        case Icon::nudgeLeft:
        case Icon::nudgeRight:
        case Icon::chevronDown:
        case Icon::pageTouchLive:
        case Icon::gridMpe:
        case Icon::gridMpeXy:
            return {};
        case Icon::pageMixer:    return mixerBars();
        case Icon::pageClip:     return clipBoxOutline();
        case Icon::pageDevice:   return deviceLines();
        case Icon::pageGrid:     return gridLoop();
        case Icon::minus:        return minusSign();
        case Icon::eye:          return eyeOutline (false);
        case Icon::eyeOff:       return eyeOutline (true);
        case Icon::valueButtons: return valueButtonsGrid();
        case Icon::fader:        return faderTrack();
        case Icon::curve:        return curveSweep();
        case Icon::sharp:        return sharpSign();
        case Icon::browserPanel: return browserPanelOutline();

        case Icon::browserProjects:  return browserProjectsFolder();
        case Icon::browserAudio:     return {};   // reines Fill-Icon (Balken)
        case Icon::browserCvControl: return browserCvSine();
        case Icon::browserAudioFx:   return browserFxKnob();
        case Icon::search:           return searchLens();
        case Icon::chevronLeft:      return chevronLeftArrow();
    }

    jassertfalse;
    return {};
}

/** Zusätzliche gefüllte Teilfläche (leer, wenn das Icon reiner Stroke ist). */
juce::Path fillGeometry (Icon icon)
{
    switch (icon)
    {
        case Icon::metronome:   return metronomeDot();
        case Icon::pageClip:    return clipBoxTriangle();
        case Icon::pageTouchLive: return touchLiveStrips();
        case Icon::gridMpe:     return gridMpeMatrix();
        case Icon::gridMpeXy:   return gridMpeXyGlyph();
        case Icon::chevronDown: return chevronDownSmall();
        case Icon::fader:       return faderThumb();
        case Icon::browserPanel: return browserPanelFill();
        case Icon::nudgeLeft:   return nudgeBars (false);
        case Icon::nudgeRight:  return nudgeBars (true);
        case Icon::browserAudio: return browserAudioWave();

        // reine Stroke-Icons — explizit statt default (Clang -Wswitch-enum)
        case Icon::play:
        case Icon::tapeLoop:
        case Icon::captureFrame:
        case Icon::plus:
        case Icon::gear:
        case Icon::pageMixer:
        case Icon::pageDevice:
        case Icon::pageGrid:
        case Icon::minus:
        case Icon::eye:
        case Icon::eyeOff:
        case Icon::valueButtons:
        case Icon::curve:
        case Icon::sharp:
        case Icon::browserProjects:
        case Icon::browserCvControl:
        case Icon::browserAudioFx:
        case Icon::search:
        case Icon::chevronLeft:
            return {};
    }

    jassertfalse;
    return {};
}

} // namespace

//==============================================================================
void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, juce::Colour colour)
{
    if (bounds.isEmpty())
        return;

    const auto transform = fitToBounds (bounds);
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto strokeWidth = juce::jmax (1.0f, side * 0.085f);

    g.setColour (colour);

    // Fill-only-Icons (chevronDown, Nudge-Balken) liefern leere Strokes
    auto stroke = strokeGeometry (icon);

    if (! stroke.isEmpty())
    {
        stroke.applyTransform (transform);
        g.strokePath (stroke, juce::PathStrokeType (strokeWidth,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    auto fill = fillGeometry (icon);

    if (! fill.isEmpty())
    {
        fill.applyTransform (transform);
        g.fillPath (fill);
    }
}

juce::Path outlinePath (Icon icon, juce::Rectangle<float> bounds)
{
    auto path = strokeGeometry (icon);
    const auto fill = fillGeometry (icon);

    if (! fill.isEmpty())
        path.addPath (fill);

    path.applyTransform (fitToBounds (bounds));
    return path;
}

} // namespace conduit::push

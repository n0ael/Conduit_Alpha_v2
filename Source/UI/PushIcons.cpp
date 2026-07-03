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
    // Doppel-oo mit Strich an der Oberkante (Tape-Symbol, User-Wunsch):
    // zwei Spulen unten, die Bandkante läuft als Linie oben drüber.
    juce::Path p;
    p.addEllipse (0.10f, 0.42f, 0.34f, 0.34f);
    p.addEllipse (0.56f, 0.42f, 0.34f, 0.34f);
    p.startNewSubPath (0.10f, 0.22f);
    p.lineTo (0.90f, 0.22f);
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
    // ○● — Kreis-Outline links; der gefüllte Punkt rechts kommt in draw()
    juce::Path p;
    p.addEllipse (0.06f, 0.30f, 0.40f, 0.40f);
    return p;
}

juce::Path metronomeDot()
{
    juce::Path p;
    p.addEllipse (0.56f, 0.30f, 0.40f, 0.40f);
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

juce::Path chevron (bool pointsRight)
{
    juce::Path p;
    const auto x0 = pointsRight ? 0.36f : 0.64f;
    const auto x1 = pointsRight ? 0.64f : 0.36f;
    p.startNewSubPath (x0, 0.18f);
    p.lineTo (x1, 0.50f);
    p.lineTo (x0, 0.82f);
    return p;
}

juce::Path nudgeBars (bool leanRight)
{
    // Live-Phase-Nudge-Optik (User 03.07.): drei geneigte Striche —
    // die Neigung zeigt die Richtung (vor/zurück)
    juce::Path p;
    const auto slant = leanRight ? 0.18f : -0.18f;

    for (const auto x : { 0.30f, 0.50f, 0.70f })
    {
        p.startNewSubPath (x - slant * 0.5f, 0.84f);
        p.lineTo (x + slant * 0.5f, 0.16f);
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
        case Icon::nudgeLeft:    return nudgeBars (false);
        case Icon::nudgeRight:   return nudgeBars (true);
        case Icon::chevronDown:  return chevronDownSmall();
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
        case Icon::chevronDown: return chevronDownSmall();
        case Icon::fader:       return faderThumb();
        case Icon::browserPanel: return browserPanelFill();

        // reine Stroke-Icons — explizit statt default (Clang -Wswitch-enum)
        case Icon::play:
        case Icon::tapeLoop:
        case Icon::captureFrame:
        case Icon::plus:
        case Icon::gear:
        case Icon::nudgeLeft:
        case Icon::nudgeRight:
        case Icon::pageMixer:
        case Icon::pageDevice:
        case Icon::pageGrid:
        case Icon::minus:
        case Icon::eye:
        case Icon::eyeOff:
        case Icon::valueButtons:
        case Icon::curve:
        case Icon::sharp:
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

    // chevronDown ist NUR Füllung (kleines Dreieck) — kein Stroke obendrauf
    if (icon != Icon::chevronDown)
    {
        auto stroke = strokeGeometry (icon);
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

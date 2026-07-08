#include "LockToggle.h"

namespace conduit
{

namespace
{
    // SVG-Vorlage-viewBox -- buildClosedLockPath()/buildOpenLockPath() und
    // der Korpus in paintButton() sind in diesen Koordinaten definiert.
    constexpr float kViewBoxSize = 34.0f;
}

LockToggle::LockToggle() : juce::Button ("lockToggle") {}

void LockToggle::setActive (bool shouldBeActive) noexcept
{
    if (active == shouldBeActive)
        return;

    active = shouldBeActive;
    repaint();
}

void LockToggle::setAccentColour (juce::Colour newColour) noexcept
{
    if (accentColour == newColour)
        return;

    accentColour = newColour;
    repaint();
}

juce::Path LockToggle::buildClosedLockPath() noexcept
{
    // Symmetrischer Bügel: Schenkel bei x=11/x=24 von der Korpus-Oberkante
    // (y=17) hoch bis y=14.6, dann äußerer Bogen über den Scheitel (y≈9)
    // und zurück -- beide Enden im Korpus verankert.
    juce::Path p;
    p.startNewSubPath (11.0f, 17.0f);
    p.lineTo (11.0f, 14.6f);
    p.addArc (11.0f, 9.0f, 13.0f, 11.2f,
             juce::MathConstants<float>::pi * -0.5f, juce::MathConstants<float>::pi * 0.5f, false);
    p.lineTo (24.0f, 17.0f);
    return p;
}

juce::Path LockToggle::buildOpenLockPath() noexcept
{
    // Dieselbe Bügel-Ellipse wie buildClosedLockPath() (Zentrum 17.5/14.6,
    // rx=6.5, ry=5.6) -- rechter Schenkel bleibt im Korpus verankert
    // (x=24, y=17..14.6), der Bogen schwingt aber nur über die Kuppe
    // (y≈9) und endet VOR dem linken Korpus-Rand frei schwebend
    // (≈12.9/10.6) -- die sichtbare Lücke zum Korpus macht den
    // "aufgeklappt"-Zustand erkennbar (kein Rückweg zu x=11).
    juce::Path p;
    p.startNewSubPath (24.0f, 17.0f);
    p.lineTo (24.0f, 14.6f);
    p.addArc (11.0f, 9.0f, 13.0f, 11.2f,
             juce::MathConstants<float>::pi * 0.5f, juce::MathConstants<float>::pi * -0.25f, false);
    return p;
}

void LockToggle::paintButton (juce::Graphics& g, bool /*isHighlighted*/, bool /*isDown*/)
{
    const auto bounds = getLocalBounds().toFloat();

    // Rahmen: separat von der Icon-Geometrie gezeichnet (nicht Teil eines
    // Pfads), Stärke skaliert mit der Größe, geklemmt auf Minimum 2 px --
    // dieses Muster gilt generell für künftige, größenskalierende
    // Rahmen-Elemente.
    const auto borderThickness = juce::jmax (2.0f, (float) getWidth() * kBorderFactor);
    g.setColour (push::colours::outline);
    g.drawRect (bounds, borderThickness);

    // Schloss-Symbol: kSymbolSize, zentriert in der Touch-Zone. viewBox
    // (34x34) wird per AffineTransform auf die Symbolfläche skaliert.
    const auto symbolBounds = bounds.withSizeKeepingCentre (kSymbolSize, kSymbolSize);
    const auto scale = symbolBounds.getWidth() / kViewBoxSize;
    const auto transform = juce::AffineTransform::scale (scale)
                               .translated (symbolBounds.getX(), symbolBounds.getY());

    const auto colour = active ? accentColour : push::colours::textDim;
    g.setColour (colour);

    juce::Path body;
    body.addRoundedRectangle (10.0f, 17.0f, 15.0f, 12.0f, 1.5f);
    g.fillPath (body, transform);

    const auto shackle = active ? buildOpenLockPath() : buildClosedLockPath();
    const auto shackleStroke = juce::jmax (1.6f, scale * 2.4f);
    g.strokePath (shackle, juce::PathStrokeType (shackleStroke, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded), transform);
}

} // namespace conduit

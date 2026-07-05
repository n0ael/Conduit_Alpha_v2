#include "PortComponent.h"

#include "UI/NodeCanvas.h"

namespace conduit
{

PortComponent::PortComponent (PortInfo portInfo)
    : info (std::move (portInfo))
{
    setSize (hitSize, hitSize);
}

void PortComponent::setSignalColour (juce::Colour newColour)
{
    if (signalColour == newColour)
        return;

    signalColour = newColour;
    repaint();
}

void PortComponent::paint (juce::Graphics& g)
{
    // Stecker = senkrechter Strich an der Kante (Signalfarbe, 90° zum Kabel).
    // Stereo-Paar = zwei Striche so eng (∓stripeOffset), dass sie zu EINER
    // durchgehenden Linie verschmelzen (User 05.07.).
    const auto centre = getLocalBounds().toFloat().getCentre();
    constexpr float stripeW = 4.0f;    // dünn
    constexpr float stripeH = 14.0f;   // hoch (senkrecht)
    constexpr float stripeOffset = 5.0f;

    const auto drawStripe = [&] (float cy)
    {
        g.setColour (signalColour);
        g.fillRoundedRectangle (
            juce::Rectangle<float> (centre.x - stripeW * 0.5f, cy - stripeH * 0.5f,
                                    stripeW, stripeH),
            stripeW * 0.5f);
    };

    if (info.span == 2)
    {
        // Höhe 14 + Versatz ∓5 → die beiden Striche überlappen zu einer Linie
        drawStripe (centre.y - stripeOffset);
        drawStripe (centre.y + stripeOffset);
    }
    else
    {
        drawStripe (centre.y);
    }
}

void PortComponent::mouseDown (const juce::MouseEvent& event)
{
    pointerDownMs = juce::Time::getMillisecondCounter();
    dragResolved = false;

    if (auto* canvas = findParentComponentOfClass<NodeCanvas>())
        canvas->beginCableDrag (info, canvas->getLocalPoint (this, event.getPosition()));
}

void PortComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (auto* canvas = findParentComponentOfClass<NodeCanvas>())
    {
        // Erste Bewegung entscheidet: lange gehalten → Mono trotz Stereo-Quelle
        if (! dragResolved)
        {
            dragResolved = true;
            if (juce::Time::getMillisecondCounter() - pointerDownMs >= (juce::int64) dwellMs)
                canvas->setCableDragMono();
        }

        canvas->updateCableDrag (canvas->getLocalPoint (this, event.getPosition()));
    }
}

void PortComponent::mouseUp (const juce::MouseEvent& event)
{
    if (auto* canvas = findParentComponentOfClass<NodeCanvas>())
        canvas->endCableDrag (canvas->getLocalPoint (this, event.getPosition()));
}

} // namespace conduit

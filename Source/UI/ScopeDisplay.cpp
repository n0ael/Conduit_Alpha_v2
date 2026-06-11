#include "ScopeDisplay.h"

#include "Modules/ScopeModule.h"

namespace conduit
{

ScopeDisplay::ScopeDisplay (GraphManager& graphManagerToUse, juce::String nodeUuidToShow)
    : graphManager (graphManagerToUse),
      nodeUuid (std::move (nodeUuidToShow)),
      historyMin (historySize, 0.0f),
      historyMax (historySize, 0.0f)
{
    setInterceptsMouseClicks (false, false);  // Drag geht an die Node-Kachel
    startTimerHz (30);                        // Scope-Refresh (CLAUDE.md 10)
}

void ScopeDisplay::stopUpdates()
{
    stopTimer();
}

//==============================================================================
void ScopeDisplay::timerCallback()
{
    pullPendingSamples();
}

void ScopeDisplay::pullPendingSamples()
{
    // Transienter Lookup statt gehaltenem Pointer (5.3) — nach Phase 2
    // liefert der GraphManager nullptr und das Scope friert ein
    auto* scope = dynamic_cast<ScopeModule*> (graphManager.getModuleFor (nodeUuid));

    if (scope == nullptr)
        return;

    auto& queue = scope->getScopeQueue();
    ScopeSample bin;
    bool changed = false;

    while (queue.pop (bin))
    {
        historyMin[static_cast<size_t> (writeIndex)] = bin.minValue;
        historyMax[static_cast<size_t> (writeIndex)] = bin.maxValue;

        if (++writeIndex >= historySize)
        {
            writeIndex = 0;
            historyFilled = true;
        }

        changed = true;
    }

    if (changed)
        repaint();
}

//==============================================================================
void ScopeDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colour (0xff15171a));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Nulllinie
    const auto midY = bounds.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine (juce::roundToInt (midY), bounds.getX(), bounds.getRight());

    const auto available = historyFilled ? historySize : writeIndex;

    if (available < 2)
        return;

    // Neuestes Bin rechts; ±1.0 füllt die Höhe
    const auto toY = [&] (float value)
    {
        return juce::jmap (juce::jlimit (-1.0f, 1.0f, value),
                           -1.0f, 1.0f, bounds.getBottom() - 2.0f, bounds.getY() + 2.0f);
    };

    g.setColour (juce::Colour (0xff7bc8f6));
    const auto width = bounds.getWidth();

    for (int x = 0; x < static_cast<int> (width); ++x)
    {
        const auto age = static_cast<int> ((width - 1.0f - x) / width * static_cast<float> (available));
        auto index = writeIndex - 1 - age;

        while (index < 0)
            index += historySize;

        const auto top    = toY (historyMax[static_cast<size_t> (index)]);
        const auto bottom = toY (historyMin[static_cast<size_t> (index)]);

        g.drawVerticalLine (juce::roundToInt (bounds.getX()) + x,
                            top, std::max (bottom, top + 1.0f));
    }
}

} // namespace conduit

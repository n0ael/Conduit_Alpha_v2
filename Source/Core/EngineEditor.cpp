#include "EngineEditor.h"

#include "EngineProcessor.h"

namespace conduit
{

EngineEditor::EngineEditor (EngineProcessor& engineProcessor)
    : juce::AudioProcessorEditor (engineProcessor),
      rootState (engineProcessor.getRootState())
{
    setResizable (true, true);
    setSize (960, 640);
}

void EngineEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto bounds = getLocalBounds();

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (24.0f)));
    g.drawText ("Conduit Alpha v2", bounds, juce::Justification::centred);

    // Defensive Audio-Setup-Warnung anzeigen (CLAUDE.md 9.1)
    const auto warning = rootState.getProperty (id::audioSetupWarning).toString();

    if (warning.isNotEmpty())
    {
        g.setColour (juce::Colours::orange);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText ("Audio-Setup: " + warning,
                    bounds.removeFromBottom (32),
                    juce::Justification::centred);
    }
}

void EngineEditor::resized()
{
    // Noch keine Child-Components — Layout folgt mit dem Node-Canvas.
}

} // namespace conduit

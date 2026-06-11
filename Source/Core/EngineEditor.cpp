#include "EngineEditor.h"

#include "EngineProcessor.h"
#include "Modules/AttenuatorModule.h"
#include "Modules/LfoModule.h"

namespace conduit
{

EngineEditor::EngineEditor (EngineProcessor& engineProcessor)
    : juce::AudioProcessorEditor (engineProcessor),
      rootState (engineProcessor.getRootState()),
      undoManager (engineProcessor.getUndoManager()),
      graphManager (engineProcessor.getGraphManager()),
      linkClock (engineProcessor.getLinkClock()),
      canvas (rootState, engineProcessor.getGraphManager(), engineProcessor.getNodeUiRegistry())
{
    const auto addModule = [this] (const char* moduleId)
    {
        // Versetzt platzieren, damit gestapelte Nodes greifbar bleiben
        const auto offset = 24 * (canvas.getNumNodeComponents() % 8);
        const auto created = graphManager.addModuleNode (moduleId, { 40 + offset, 40 + offset });
        jassertquiet (created.isValid());
    };

    addButton.onClick    = [addModule] { addModule (AttenuatorModule::staticModuleId); };
    addLfoButton.onClick = [addModule] { addModule (LfoModule::staticModuleId); };

    undoButton.onClick = [this] { undoManager.undo(); };
    redoButton.onClick = [this] { undoManager.redo(); };

    // Link-Transport: Slider schreibt in die Session, der Timer pollt zurück
    tempoSlider.setRange (20.0, 300.0, 0.1);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setValue (linkClock.getTempo(), juce::dontSendNotification);
    tempoSlider.onValueChange = [this] { linkClock.setTempo (tempoSlider.getValue()); };

    peersLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    peersLabel.setJustificationType (juce::Justification::centredLeft);

    warningLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    warningLabel.setJustificationType (juce::Justification::centredRight);

    const auto warning = rootState.getProperty (id::audioSetupWarning).toString();

    if (warning.isNotEmpty())
        warningLabel.setText ("Audio-Setup: " + warning, juce::dontSendNotification);

    addAndMakeVisible (addButton);
    addAndMakeVisible (addLfoButton);
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    addAndMakeVisible (tempoSlider);
    addAndMakeVisible (peersLabel);
    addAndMakeVisible (warningLabel);
    addAndMakeVisible (canvas);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setSize (960, 640);

    timerCallback();    // Peer-Label sofort befüllen, nicht erst nach 250ms
    startTimerHz (4);   // Session-Polling — Tempo/Peers können sich im Netz ändern
}

//==============================================================================
void EngineEditor::timerCallback()
{
    // Kein Kampf mit dem User: während des Drags gewinnt der Slider
    if (! tempoSlider.isMouseButtonDown())
        tempoSlider.setValue (linkClock.getTempo(), juce::dontSendNotification);

    const auto numPeers = linkClock.getNumPeers();
    const auto text = numPeers == 0
                          ? juce::String ("Link: keine Peers")
                          : "Link: " + juce::String (numPeers)
                                + (numPeers == 1 ? " Peer" : " Peers");

    if (peersLabel.getText() != text)
        peersLabel.setText (text, juce::dontSendNotification);
}

//==============================================================================
void EngineEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff24272c));  // Toolbar-Hintergrund
}

void EngineEditor::resized()
{
    auto bounds = getLocalBounds();
    auto toolbar = bounds.removeFromTop (toolbarHeight).reduced (8, 6);  // Buttons ≥ 44px hoch

    addButton.setBounds (toolbar.removeFromLeft (150));
    toolbar.removeFromLeft (8);
    addLfoButton.setBounds (toolbar.removeFromLeft (100));
    toolbar.removeFromLeft (8);
    undoButton.setBounds (toolbar.removeFromLeft (80));
    toolbar.removeFromLeft (8);
    redoButton.setBounds (toolbar.removeFromLeft (80));
    toolbar.removeFromLeft (16);
    tempoSlider.setBounds (toolbar.removeFromLeft (150));
    toolbar.removeFromLeft (8);
    peersLabel.setBounds (toolbar.removeFromLeft (130));
    warningLabel.setBounds (toolbar);

    canvas.setBounds (bounds);
}

bool EngineEditor::keyPressed (const juce::KeyPress& key)
{
    const auto modifier = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress ('z', modifier, 0))
        return undoManager.undo();

    if (key == juce::KeyPress ('y', modifier, 0)
        || key == juce::KeyPress ('z', modifier | juce::ModifierKeys::shiftModifier, 0))
        return undoManager.redo();

    return false;
}

} // namespace conduit

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "UI/NodeCanvas.h"

namespace conduit
{

class EngineProcessor;

//==============================================================================
/**
    Haupt-Editor der App — read/listen-only gegenüber dem Datenmodell
    (CLAUDE.md 6); Patch-Aktionen laufen über GraphManager/UndoManager.

    Layout: Toolbar (Add/Undo/Redo, Touch-Targets ≥ 44px) über dem NodeCanvas;
    die audioSetupWarning (9.1) erscheint rechts in der Toolbar.
*/
class EngineEditor final : public juce::AudioProcessorEditor
{
public:
    explicit EngineEditor (EngineProcessor& engineProcessor);
    ~EngineEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    static constexpr int toolbarHeight = 56;

    juce::ValueTree rootState;  // ref-counted Handle, read/listen-only
    juce::UndoManager& undoManager;
    GraphManager& graphManager;

    juce::TextButton addButton  { juce::String::fromUTF8 ("\xef\xbc\x8b Attenuator") };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::Label warningLabel;

    NodeCanvas canvas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineEditor)
};

} // namespace conduit

#include "FxModulePanel.h"

#include "Modules/ChassisSchema.h"
#include "Modules/ProcessorModule.h"
#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int sendRowHeight = 24;   // LED + LINK-Button unter dem Out-Zug
    constexpr int sendLedWidth  = 16;
}

FxModulePanel::FxModulePanel (juce::ValueTree nodeTreeToBind, GraphManager& graphManagerToUse)
    : nodeTree (std::move (nodeTreeToBind)),
      graphManager (graphManagerToUse)
{
    nodeTree.addListener (this);

    inputFader = std::make_unique<GainFaderMeter> (nodeTree, ChassisSchema::inputGainId,
                                                   graphManager, true);
    addAndMakeVisible (*inputFader);

    outputFader = std::make_unique<GainFaderMeter> (nodeTree, ChassisSchema::outputGainId,
                                                    graphManager, false);
    addAndMakeVisible (*outputFader);

    // Link-Send-Toggle: patchbare Aktion → undo-fähig über den GraphManager;
    // der Property-Listener des Managers leitet live ans Modul weiter (4.6)
    linkSendButton.setTooltip ("Post-Output-Gain als Link-Audio-Kanal zu Ableton streamen");
    linkSendButton.setClickingTogglesState (false);
    linkSendButton.onClick = [this]
    {
        const auto enabled = (bool) nodeTree.getProperty (id::linkSendEnabled, false);
        graphManager.setLinkSendEnabled (nodeTree.getProperty (id::nodeId).toString(),
                                         ! enabled);
    };
    addAndMakeVisible (linkSendButton);
    refreshSendButtonState();

    buildColumns();
    startTimerHz (10);  // LED-Statuswechsel sind selten (Muster StatusBadge)
}

FxModulePanel::~FxModulePanel()
{
    nodeTree.removeListener (this);
}

void FxModulePanel::stopUpdates()
{
    stopTimer();
    nodeTree.removeListener (this);
    setInterceptsMouseClicks (false, false);

    inputFader->stopUpdates();
    outputFader->stopUpdates();
    linkSendButton.setEnabled (false);

    for (auto& column : columns)
    {
        column->slider.setEnabled (false);
        column->cvKnob.setEnabled (false);
        column->minEdit.setEnabled (false);
        column->maxEdit.setEnabled (false);
        column->hideButton.setEnabled (false);
    }
}

//==============================================================================
juce::ValueTree FxModulePanel::parametersTree() const
{
    return nodeTree.getChildWithName (id::parameters);
}

juce::ValueTree FxModulePanel::paramTreeFor (const juce::String& paramId) const
{
    return parametersTree().getChildWithProperty (id::paramId, paramId);
}

void FxModulePanel::applyUserRangeToColumn (ParameterColumn& column, const juce::ValueTree& param)
{
    // Fader nutzt den User-Bereich (Dev-Modus 4.6); DSP clamped weiter hart
    const auto userMin = (double) param.getProperty (id::paramUserMin,
                                                     param.getProperty (id::paramMin, 0.0));
    const auto userMax = (double) param.getProperty (id::paramUserMax,
                                                     param.getProperty (id::paramMax, 1.0));

    column.slider.setRange (userMin, userMax, 0.0);
    column.slider.setValue ((double) param.getProperty (id::paramValue, 0.0),
                            juce::dontSendNotification);
    column.minEdit.setText (juce::String (userMin, 3), juce::dontSendNotification);
    column.maxEdit.setText (juce::String (userMax, 3), juce::dontSendNotification);
}

void FxModulePanel::buildColumns()
{
    const auto parameters = parametersTree();

    for (int i = 0; i < parameters.getNumChildren(); ++i)
    {
        const auto param = parameters.getChild (i);

        // Nur DSP-Parameter werden Spalten — Gains sind die Kanalzüge,
        // Attenuverter sind die Knobs unter den Fadern (4.6)
        if (ChassisSchema::roleOf (param) != juce::String (ChassisSchema::roleDsp))
            continue;

        const auto hidden = (bool) param.getProperty (id::paramUiHidden, false);

        // Normalmodus: uiHidden-Spalten verschwinden komplett (Bus-Layout
        // und CV-Kanal-Zuordnung bleiben davon unberührt, 4.6)
        if (hidden && ! devMode)
            continue;

        auto column = std::make_unique<ParameterColumn>();
        column->paramId    = param.getProperty (id::paramId).toString();
        column->cvAmountId = ChassisSchema::cvAmountIdFor (column->paramId);
        column->cvChannel  = ChassisSchema::cvChannelForParam (nodeTree, column->paramId);
        column->hidden     = hidden;

        column->titleLabel.setText (column->paramId, juce::dontSendNotification);
        column->titleLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
        column->titleLabel.setColour (juce::Label::textColourId,
                                      juce::Colours::white.withAlpha (0.6f));
        column->titleLabel.setJustificationType (juce::Justification::centred);
        column->titleLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (column->titleLabel);

        column->slider.setDoubleClickReturnValue (true, (double) param.getProperty (id::paramDefault, 0.0));

        // Schreibt NUR in den Tree — der GraphManager spiegelt aufs Atomic
        // (6.1); bewusst ohne UndoManager (Muster ParameterPanel).
        column->slider.onValueChange = [this, paramId = column->paramId, slider = &column->slider]
        {
            if (auto p = paramTreeFor (paramId); p.isValid())
                p.setProperty (id::paramValue, slider->getValue(), nullptr);
        };
        addAndMakeVisible (column->slider);

        // Attenuverter (MI-Stil): bipolar −1..+1, Mitte = keine Modulation,
        // Doppelklick zurück auf 0 — bindet {param}_cv_amt
        if (const auto cvParam = paramTreeFor (column->cvAmountId); cvParam.isValid())
        {
            column->cvKnob.setRange ((double) cvParam.getProperty (id::paramMin, -1.0),
                                     (double) cvParam.getProperty (id::paramMax, 1.0), 0.0);
            column->cvKnob.setValue ((double) cvParam.getProperty (id::paramValue, 0.0),
                                     juce::dontSendNotification);
            column->cvKnob.setDoubleClickReturnValue (true,
                (double) cvParam.getProperty (id::paramDefault, 0.0));
            column->cvKnob.onValueChange = [this, cvId = column->cvAmountId, knob = &column->cvKnob]
            {
                if (auto p = paramTreeFor (cvId); p.isValid())
                    p.setProperty (id::paramValue, knob->getValue(), nullptr);
            };
            addAndMakeVisible (column->cvKnob);
        }

        // CV-Eingangs-Port des Parameters (fester Kanal aus dem Schema) —
        // normale PortComponent, Kabel-Gesten laufen über den NodeCanvas.
        // Ausgeblendete Parameter tragen KEINEN Port (Kabel wurden beim
        // Ausblenden getrennt; im Dev-Modus keine neuen anschließbar).
        if (! hidden)
        {
            column->cvPort = std::make_unique<PortComponent> (
                PortInfo { nodeTree.getProperty (id::nodeId).toString(), true,
                           column->cvChannel, 1 });
            column->cvPort->setTooltip ("CV: " + column->paramId);
            addAndMakeVisible (*column->cvPort);
        }

        // Dev-Modus-Controls: Min/Max-Editierfelder (User-Regelbereich,
        // undo-fähig über den GraphManager) + Ausblenden-Toggle
        if (devMode)
        {
            const auto uuid = nodeTree.getProperty (id::nodeId).toString();

            for (auto* edit : { &column->minEdit, &column->maxEdit })
            {
                edit->setFont (juce::Font (juce::FontOptions (10.0f)));
                edit->setColour (juce::Label::textColourId,
                                 juce::Colours::white.withAlpha (0.7f));
                edit->setJustificationType (juce::Justification::centred);
                edit->setEditable (true, true, false);
                addAndMakeVisible (*edit);
            }

            const auto commitRange = [this, uuid, paramId = column->paramId,
                                      minEdit = &column->minEdit, maxEdit = &column->maxEdit]
            {
                const auto ok = graphManager.setParameterUserRange (
                    uuid, paramId,
                    minEdit->getText().getDoubleValue(),
                    maxEdit->getText().getDoubleValue());

                // Abgelehnt (außerhalb Hard-Range / min >= max) → Texte
                // aus dem Tree restaurieren
                if (! ok)
                    if (auto p = paramTreeFor (paramId); p.isValid())
                        for (auto& c : columns)
                            if (c->paramId == paramId)
                                applyUserRangeToColumn (*c, p);
            };
            column->minEdit.onTextChange = commitRange;
            column->maxEdit.onTextChange = commitRange;

            column->hideButton.setButtonText (hidden ? "ein" : "aus");
            column->hideButton.setTooltip (hidden
                ? juce::String::fromUTF8 ("Parameter wieder einblenden")
                : juce::String::fromUTF8 ("Parameter ausblenden — trennt bestehende CV-Kabel"));
            column->hideButton.onClick = [this, uuid, paramId = column->paramId, hidden]
            {
                graphManager.setParameterHidden (uuid, paramId, ! hidden);
            };
            addAndMakeVisible (column->hideButton);

            // Ausgeblendete Spalten gedimmt zeigen (Dev-Sichtbarkeit)
            if (hidden)
            {
                column->titleLabel.setAlpha (0.4f);
                column->slider.setAlpha (0.4f);
                column->cvKnob.setAlpha (0.4f);
            }
        }

        applyUserRangeToColumn (*column, param);
        columns.push_back (std::move (column));
    }
}

void FxModulePanel::rebuildColumns()
{
    retiredColumns.clear();   // Friedhof des VORIGEN Rebuilds ist jetzt sicher

    // Alte Spalten aus der Hierarchie lösen, aber am Leben lassen — der
    // Auslöser kann der hideButton einer dieser Spalten sein (s. Friedhof)
    for (auto& column : columns)
    {
        for (auto* child : { static_cast<juce::Component*> (&column->titleLabel),
                             static_cast<juce::Component*> (&column->slider),
                             static_cast<juce::Component*> (&column->cvKnob),
                             static_cast<juce::Component*> (&column->minEdit),
                             static_cast<juce::Component*> (&column->maxEdit),
                             static_cast<juce::Component*> (&column->hideButton),
                             static_cast<juce::Component*> (column->cvPort.get()) })
            if (child != nullptr)
                removeChildComponent (child);

        retiredColumns.push_back (std::move (column));
    }

    columns.clear();

    // Friedhof asynchron leeren (nach dem auslösenden Callback-Stack)
    juce::Component::SafePointer<FxModulePanel> self (this);
    juce::MessageManager::callAsync ([self]
    {
        if (self != nullptr)
            self->retiredColumns.clear();
    });

    buildColumns();
    resized();
    repaint();

    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

int FxModulePanel::getNumHiddenParameters() const
{
    const auto parameters = parametersTree();
    int hidden = 0;

    for (int i = 0; i < parameters.getNumChildren(); ++i)
    {
        const auto param = parameters.getChild (i);

        if (ChassisSchema::roleOf (param) == juce::String (ChassisSchema::roleDsp)
            && (bool) param.getProperty (id::paramUiHidden, false))
            ++hidden;
    }

    return hidden;
}

void FxModulePanel::setDevMode (bool shouldBeInDevMode)
{
    if (devMode == shouldBeInDevMode)
        return;

    devMode = shouldBeInDevMode;
    rebuildColumns();
}

//==============================================================================
juce::Point<int> FxModulePanel::cvPortCentre (int cvChannel) const
{
    // Über den GESPEICHERTEN Kanal suchen — bei uiHidden-Spalten verschiebt
    // sich der sichtbare Index, die Kanal-Zuordnung nie (4.6)
    for (const auto& column : columns)
        if (column->cvChannel == cvChannel && column->cvPort != nullptr)
            return column->cvPort->getBounds().getCentre();

    return {};
}

const PortComponent* FxModulePanel::getCvPort (int columnIndex) const noexcept
{
    if (columnIndex < 0 || columnIndex >= getNumColumns())
        return nullptr;

    return columns[static_cast<size_t> (columnIndex)]->cvPort.get();
}

//==============================================================================
void FxModulePanel::timerCallback()
{
    refreshSendStatusNow();
}

void FxModulePanel::refreshSendStatusNow()
{
    // Transiente Auflösung pro Tick (5.3) — nullptr während Deleting/Swap
    auto* module = dynamic_cast<ProcessorModule*> (graphManager.getModuleFor (
        nodeTree.getProperty (id::nodeId).toString()));

    const auto status = module != nullptr ? module->getLinkSendStatus()
                                          : LinkSendTaps::Status::offline;

    if (status != shownSendStatus)
    {
        shownSendStatus = status;
        repaint (sendLedBounds());
    }
}

void FxModulePanel::refreshSendButtonState()
{
    const auto enabled = (bool) nodeTree.getProperty (id::linkSendEnabled, false);
    linkSendButton.setToggleState (enabled, juce::dontSendNotification);
    linkSendButton.setColour (juce::TextButton::textColourOffId,
                              enabled ? push::colours::ledCyan : push::colours::textDim);
}

juce::Rectangle<int> FxModulePanel::sendLedBounds() const
{
    return { getWidth() - GainFaderMeter::preferredWidth,
             getHeight() - sendRowHeight, sendLedWidth, sendRowHeight };
}

void FxModulePanel::paint (juce::Graphics& g)
{
    // Send-Status-LED links neben dem LINK-Button (Farben wie StatusBadge)
    const auto colour = shownSendStatus == LinkSendTaps::Status::streaming ? juce::Colour (0xff58d68d)
                      : shownSendStatus == LinkSendTaps::Status::announced ? juce::Colour (0xffe8b339)
                                                                           : juce::Colour (0xff5a6170);

    g.setColour (colour);
    g.fillEllipse (sendLedBounds().toFloat().withSizeKeepingCentre (8.0f, 8.0f));
}

//==============================================================================
void FxModulePanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // Link-Send-Toggle folgt dem Patch-Zustand (auch Undo/Preset-Load)
    if (property == id::linkSendEnabled && tree == nodeTree)
    {
        refreshSendButtonState();
        return;
    }

    // Dev-Modus-Properties (auch via Undo/Preset-Load):
    // uiHidden strukturiert die Spalten um, User-Range aktualisiert nur
    if (tree.hasType (id::parameter))
    {
        if (property == id::paramUiHidden)
        {
            rebuildColumns();
            return;
        }

        if (property == id::paramUserMin || property == id::paramUserMax)
        {
            const auto changedId = tree.getProperty (id::paramId).toString();

            for (auto& column : columns)
                if (column->paramId == changedId)
                {
                    applyUserRangeToColumn (*column, tree);
                    return;
                }

            return;
        }
    }

    // Spalten-Fader folgen externen Quellen (OSC-Nachzug 6.1, Undo,
    // Preset-Load); die Gain-Züge hören selbst auf denselben Tree.
    if (property != id::paramValue || ! tree.hasType (id::parameter))
        return;

    const auto paramId = tree.getProperty (id::paramId).toString();

    for (auto& column : columns)
    {
        if (column->paramId == paramId)
        {
            column->slider.setValue ((double) tree.getProperty (id::paramValue),
                                     juce::dontSendNotification);
            return;
        }

        if (column->cvAmountId == paramId)
        {
            column->cvKnob.setValue ((double) tree.getProperty (id::paramValue),
                                     juce::dontSendNotification);
            return;
        }
    }
}

//==============================================================================
void FxModulePanel::resized()
{
    auto bounds = getLocalBounds();

    inputFader->setBounds (bounds.removeFromLeft (GainFaderMeter::preferredWidth));

    // Rechte Spalte: Out-Zug oben, darunter LED + LINK-Button (4.6)
    auto right = bounds.removeFromRight (GainFaderMeter::preferredWidth);
    auto sendRow = right.removeFromBottom (sendRowHeight);
    sendRow.removeFromLeft (sendLedWidth);   // LED zeichnet paint()
    linkSendButton.setBounds (sendRow.reduced (0, 2));
    outputFader->setBounds (right);

    // DSP-Spalten mittig zwischen den Gain-Zügen
    bounds.reduce (8, 0);

    for (auto& column : columns)
    {
        auto columnBounds = bounds.removeFromLeft (columnWidth);
        column->titleLabel.setBounds (columnBounds.removeFromTop (titleHeight));

        // Dev-Modus: Min/Max-Editierfelder unter dem Titel, Ausblenden-
        // Toggle ganz unten — der Fader schrumpft entsprechend
        if (devMode)
        {
            auto devRow = columnBounds.removeFromTop (devRowHeight);
            column->minEdit.setBounds (devRow.removeFromLeft (columnWidth / 2));
            column->maxEdit.setBounds (devRow);

            auto hideRow = columnBounds.removeFromBottom (hideRowHeight);
            column->hideButton.setBounds (hideRow.reduced (6, 1));
        }

        // Unten: CV-Port, darüber der Attenuverter — Rest ist der lange Fader
        auto portRow = columnBounds.removeFromBottom (portRowHeight);
        if (column->cvPort != nullptr)
            column->cvPort->setCentrePosition (portRow.getCentreX(), portRow.getCentreY());

        auto knobRow = columnBounds.removeFromBottom (knobHeight);
        column->cvKnob.setBounds (knobRow.withSizeKeepingCentre (knobHeight - 4, knobHeight - 4));

        column->slider.setBounds (columnBounds.reduced (4, 2));
    }
}

} // namespace conduit

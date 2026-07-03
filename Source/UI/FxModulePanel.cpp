#include "FxModulePanel.h"

#include "Modules/ChassisSchema.h"
#include "Modules/ProcessorModule.h"
#include "UI/CurveEditor.h"
#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int sendRowHeight = 24;   // LED + LINK-Button unter dem Out-Zug
    constexpr int sendLedWidth  = 16;
}

//==============================================================================
void FxModulePanel::ValueButton::mouseUp (const juce::MouseEvent& e)
{
    // Nur der erste Klick löst aus — der zweite eines Doppelklicks gehört
    // dem Rename-Editor (setEditable-Muster)
    if (! isBeingEdited() && e.mouseWasClicked()
        && e.getNumberOfClicks() == 1 && onClick != nullptr)
        onClick();

    juce::Label::mouseUp (e);
}

void FxModulePanel::ValueButton::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.5f);

    if (active)
    {
        // LED-Stil (Push 10.0): der Button, dessen Wert gerade anliegt
        g.setColour (push::colours::ledCyan.withAlpha (0.2f));
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (push::colours::ledCyan);
    }
    else
    {
        g.setColour (push::colours::textDim.withAlpha (0.4f));
    }

    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    juce::Label::paint (g);   // Text + Inline-Editor zeichnet das Label
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

    // Dev-Modus: Ist-Zustand (Range/Hidden/Kurve) als Modul-Typ-Default
    // sichern — greift bei künftigen Neu-Anlagen (4.6)
    saveDefaultsButton.setTooltip (juce::String::fromUTF8 (
        "Regelbereiche, Sichtbarkeit, Kurven und Wert-Buttons als Standard für diesen Modul-Typ speichern"));
    saveDefaultsButton.onClick = [this]
    {
        graphManager.captureModuleUiDefaults (nodeTree.getProperty (id::nodeId).toString());
    };
    addChildComponent (saveDefaultsButton);   // sichtbar nur im Dev-Modus

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
    saveDefaultsButton.setEnabled (false);

    for (auto& column : columns)
    {
        column->slider.setEnabled (false);
        column->cvKnob.setEnabled (false);
        column->hideButton.setEnabled (false);
        column->curveButton.setEnabled (false);
        column->modeButton.setEnabled (false);
        column->addButton.setEnabled (false);
        column->removeButton.setEnabled (false);

        for (auto& valueButton : column->valueButtons)
            valueButton->setEnabled (false);
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

    // Bezier-Response-Kurve: reines UI-Mapping — der Wert bleibt echt (4.6)
    column.slider.setResponseCurve (ChassisSchema::parseCurve (
        param.getProperty (id::paramCurve).toString()));

    column.slider.setValue ((double) param.getProperty (id::paramValue, 0.0),
                            juce::dontSendNotification);
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

        // Fader↔Button-Modus (4.6): Snapshots bestimmen Spaltenbreite/Layout.
        // Kaputte uiButtons-Strings (handeditierte Patches) → leere Liste,
        // nie Crash; Buttons werden NUR bei uiMode == "buttons" gebaut.
        column->buttonMode = ChassisSchema::isButtonMode (param);
        const auto buttonList = ChassisSchema::parseButtons (
            param.getProperty (id::paramUiButtons).toString())
                .value_or (std::vector<ChassisSchema::ButtonPreset>{});
        column->numButtons = static_cast<int> (buttonList.size());

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

        // Button-Modus ohne Dev: Buttons ERSETZEN den Fader — der Slider
        // bleibt Member (Range-/Value-Sync unverändert), nur unsichtbar
        if (column->buttonMode && ! devMode)
            column->slider.setVisible (false);

        // Wert-Buttons (4.6): Nicht-Dev ruft den gespeicherten Wert ab
        // (derselbe Tree-Pfad wie der Fader, kein UndoManager); Dev speichert
        // den aktuellen Fader-Wert in den Button (undo-fähig) und benennt
        // per Doppelklick um (Label-Muster NodeComponent-Rename)
        if (column->buttonMode)
        {
            const auto ownerUuid = nodeTree.getProperty (id::nodeId).toString();
            const auto currentValue = (double) param.getProperty (id::paramValue, 0.0);

            for (size_t buttonIndex = 0; buttonIndex < buttonList.size(); ++buttonIndex)
            {
                auto button = std::make_unique<ValueButton>();
                const auto& preset = buttonList[buttonIndex];

                button->setText (preset.name, juce::dontSendNotification);
                button->setFont (juce::Font (juce::FontOptions (11.0f)));
                button->setJustificationType (juce::Justification::centred);
                button->setMinimumHorizontalScale (0.7f);
                button->storedValue = preset.value;
                button->active = juce::exactlyEqual (static_cast<float> (preset.value),
                                                     static_cast<float> (currentValue));
                button->setEditable (false, devMode, false);

                if (devMode)
                {
                    button->onClick = [this, ownerUuid, paramId = column->paramId,
                                       storeIndex = static_cast<int> (buttonIndex)]
                    {
                        graphManager.storeParameterButtonValue (ownerUuid, paramId, storeIndex);
                    };

                    // Rename-Commit; bei Ablehnung Text restaurieren — dann
                    // gibt es KEINEN Property-Change, der Button lebt weiter
                    button->onTextChange = [this, ownerUuid, paramId = column->paramId,
                                            renameIndex = static_cast<int> (buttonIndex),
                                            oldName = preset.name, buttonPtr = button.get()]
                    {
                        if (! graphManager.renameParameterButton (ownerUuid, paramId, renameIndex,
                                                                  buttonPtr->getText()))
                            buttonPtr->setText (oldName, juce::dontSendNotification);
                    };
                }
                else
                {
                    button->onClick = [this, paramId = column->paramId,
                                       recallValue = preset.value]
                    {
                        if (auto p = paramTreeFor (paramId); p.isValid())
                            p.setProperty (id::paramValue, recallValue, nullptr);
                    };
                }

                addAndMakeVisible (*button);
                column->valueButtons.push_back (std::move (button));
            }
        }

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

        // Dev-Modus-Controls: Ausblenden-Toggle + Kurven-/Range-Editor —
        // Min/Max leben IM Kurven-Popup (User-Wunsch 03.07.)
        if (devMode)
        {
            const auto uuid = nodeTree.getProperty (id::nodeId).toString();

            column->hideButton.setButtonText (hidden ? "ein" : "aus");
            column->hideButton.setTooltip (hidden
                ? juce::String::fromUTF8 ("Parameter wieder einblenden")
                : juce::String::fromUTF8 ("Parameter ausblenden — trennt bestehende CV-Kabel"));
            column->hideButton.onClick = [this, uuid, paramId = column->paramId, hidden]
            {
                graphManager.setParameterHidden (uuid, paramId, ! hidden);
            };
            addAndMakeVisible (column->hideButton);

            // Parameter-Setup als CallOutBox über der Spalte: Bezier-Kurve
            // PLUS Min/Max des User-Regelbereichs — Änderungen committen
            // live und undo-fähig via GraphManager
            column->curveButton.setButtonText ("~");
            column->curveButton.setTooltip (
                juce::String::fromUTF8 ("Fader-Kurve + Regelbereich editieren"));
            column->curveButton.onClick = [this, uuid, paramId = column->paramId,
                                           button = &column->curveButton]
            {
                // Bewusst andere Namen als die äußeren Locals der Rebuild-
                // Schleife (param/parameters/i) — Clang -Wshadow-uncaptured-local
                const auto editedParam = paramTreeFor (paramId);

                // Link-Quellen: alle ANDEREN dsp-Parameter dieses Moduls
                juce::StringArray linkSources;
                const auto allParameters = parametersTree();

                for (int candidateIndex = 0; candidateIndex < allParameters.getNumChildren(); ++candidateIndex)
                {
                    const auto candidate = allParameters.getChild (candidateIndex);
                    const auto candidateId = candidate.getProperty (id::paramId).toString();

                    if (candidateId != paramId
                        && ChassisSchema::roleOf (candidate) == juce::String (ChassisSchema::roleDsp))
                        linkSources.add (candidateId);
                }

                auto editor = std::make_unique<CurveEditor> (
                    editedParam.getProperty (id::paramCurve).toString(),
                    (double) editedParam.getProperty (id::paramUserMin,
                                                      editedParam.getProperty (id::paramMin, 0.0)),
                    (double) editedParam.getProperty (id::paramUserMax,
                                                      editedParam.getProperty (id::paramMax, 1.0)),
                    (double) editedParam.getProperty (id::paramMin, 0.0),
                    (double) editedParam.getProperty (id::paramMax, 1.0),
                    linkSources,
                    editedParam.getProperty (id::paramLinkSource).toString(),
                    (double) editedParam.getProperty (id::paramLinkAmount, 0.0),
                    editedParam.getProperty (id::paramLinkCurve).toString());

                editor->onCurveChanged = [this, uuid, paramId] (const juce::String& curveText)
                {
                    graphManager.setParameterCurve (uuid, paramId, curveText);
                };
                editor->onRangeChanged = [this, uuid, paramId] (double newMin, double newMax)
                {
                    return graphManager.setParameterUserRange (uuid, paramId, newMin, newMax);
                };
                editor->onLinkChanged = [this, uuid, paramId] (const juce::String& source, double amount)
                {
                    graphManager.setParameterLink (uuid, paramId, source, amount);
                };
                editor->onLinkCurveChanged = [this, uuid, paramId] (const juce::String& curveText)
                {
                    graphManager.setParameterLinkCurve (uuid, paramId, curveText);
                };

                juce::CallOutBox::launchAsynchronously (std::move (editor),
                                                        button->getScreenBounds(), nullptr);
            };
            addAndMakeVisible (column->curveButton);

            // Fader↔Button-Umschalter (4.6): dritter Toggle der Dev-Zeile
            column->modeButton.setButtonText (column->buttonMode ? "fdr" : "btn");
            column->modeButton.setTooltip (juce::String::fromUTF8 (column->buttonMode
                ? "Zurück zum Fader — die Buttons bleiben gespeichert"
                : "Wert-Buttons statt Fader (in der Nicht-Dev-Ansicht)"));
            column->modeButton.onClick = [this, uuid, paramId = column->paramId,
                                          toButtons = ! column->buttonMode]
            {
                graphManager.setParameterUiMode (uuid, paramId, toButtons);
            };
            addAndMakeVisible (column->modeButton);

            // +/−-Stepper: Button-Anzahl (0..10) wird NUR im Dev-Modus bestimmt
            if (column->buttonMode)
            {
                column->addButton.setButtonText ("+");
                column->addButton.setTooltip (juce::String::fromUTF8 (
                    "Button anhängen — startet mit dem aktuellen Fader-Wert"));
                column->addButton.onClick = [this, uuid, paramId = column->paramId,
                                             grownCount = column->numButtons + 1]
                {
                    graphManager.setParameterButtonCount (uuid, paramId, grownCount);
                };
                column->addButton.setEnabled (column->numButtons < ChassisSchema::maxUiButtons);
                addAndMakeVisible (column->addButton);

                column->removeButton.setButtonText (juce::String::fromUTF8 ("−"));
                column->removeButton.setTooltip (juce::String::fromUTF8 (
                    "Letzten Button entfernen"));
                column->removeButton.onClick = [this, uuid, paramId = column->paramId,
                                                shrunkCount = column->numButtons - 1]
                {
                    graphManager.setParameterButtonCount (uuid, paramId, shrunkCount);
                };
                column->removeButton.setEnabled (column->numButtons > 0);
                addAndMakeVisible (column->removeButton);
            }

            // Ausgeblendete Spalten gedimmt zeigen (Dev-Sichtbarkeit)
            if (hidden)
            {
                column->titleLabel.setAlpha (0.4f);
                column->slider.setAlpha (0.4f);
                column->cvKnob.setAlpha (0.4f);

                for (auto& valueButton : column->valueButtons)
                    valueButton->setAlpha (0.4f);
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
                             static_cast<juce::Component*> (&column->hideButton),
                             static_cast<juce::Component*> (&column->curveButton),
                             static_cast<juce::Component*> (&column->modeButton),
                             static_cast<juce::Component*> (&column->addButton),
                             static_cast<juce::Component*> (&column->removeButton),
                             static_cast<juce::Component*> (column->cvPort.get()) })
            if (child != nullptr)
                removeChildComponent (child);

        // ValueButtons ebenfalls lösen — der Auslöser kann ihr eigener
        // onClick/onTextChange sein (Friedhof, s.u.)
        for (auto& valueButton : column->valueButtons)
            removeChildComponent (valueButton.get());

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

int FxModulePanel::getPreferredWidth() const noexcept
{
    int width = 2 * GainFaderMeter::preferredWidth + 16;

    for (const auto& column : columns)
        width += columnWidthFor (column->buttonMode, devMode, column->numButtons);

    return width;
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
    saveDefaultsButton.setVisible (devMode);
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
        // uiMode/uiButtons ändern Struktur UND Spaltenbreite → Rebuild
        // (auch beim Rename: der Friedhof macht das crashsicher)
        if (property == id::paramUiHidden
            || property == id::paramUiMode || property == id::paramUiButtons)
        {
            rebuildColumns();
            return;
        }

        if (property == id::paramUserMin || property == id::paramUserMax
            || property == id::paramCurve)
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
            const auto newValue = (double) tree.getProperty (id::paramValue);
            column->slider.setValue (newValue, juce::dontSendNotification);

            // Aktiv-Markierung in-place nachziehen (kein Rebuild) —
            // Vergleich bewusst über float (6.1: var speichert double)
            for (auto& valueButton : column->valueButtons)
            {
                const auto isActive = juce::exactlyEqual (
                    static_cast<float> (valueButton->storedValue),
                    static_cast<float> (newValue));

                if (valueButton->active != isActive)
                {
                    valueButton->active = isActive;
                    valueButton->repaint();
                }
            }

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

    // Linke Spalte: In-Zug; im Dev-Modus darunter der Defaults-Button
    auto left = bounds.removeFromLeft (GainFaderMeter::preferredWidth);

    if (devMode)
        saveDefaultsButton.setBounds (left.removeFromBottom (sendRowHeight).reduced (0, 2));

    inputFader->setBounds (left);

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
        auto columnBounds = bounds.removeFromLeft (
            columnWidthFor (column->buttonMode, devMode, column->numButtons));
        column->titleLabel.setBounds (columnBounds.removeFromTop (titleHeight));

        // Dev + Buttons: Fader-Teil links (56px, Layout wie gehabt), die
        // Button-Stapel daneben — Fader zum Wert-Finden, Klick speichert (4.6)
        auto stackArea = juce::Rectangle<int>();

        if (column->buttonMode && devMode)
            stackArea = columnBounds.removeFromRight (columnBounds.getWidth() - columnWidth);

        // Dev-Modus: Ausblenden / Fader↔Buttons / Kurven-Editor als Drittel
        // der Dev-Zeile ganz unten — der Fader schrumpft entsprechend
        if (devMode)
        {
            auto hideRow = columnBounds.removeFromBottom (hideRowHeight);
            const auto thirdWidth = hideRow.getWidth() / 3;
            column->curveButton.setBounds (hideRow.removeFromRight (thirdWidth).reduced (1));
            column->modeButton.setBounds (hideRow.removeFromRight (thirdWidth).reduced (1));
            column->hideButton.setBounds (hideRow.reduced (2, 1));
        }

        // Unten: CV-Port, darüber der Attenuverter — Rest ist der lange Fader
        auto portRow = columnBounds.removeFromBottom (portRowHeight);
        if (column->cvPort != nullptr)
            column->cvPort->setCentrePosition (portRow.getCentreX(), portRow.getCentreY());

        auto knobRow = columnBounds.removeFromBottom (knobHeight);
        column->cvKnob.setBounds (knobRow.withSizeKeepingCentre (knobHeight - 4, knobHeight - 4));

        // Nicht-Dev + Buttons: die Stapel ERSETZEN den Fader-Bereich
        // (Knob-/Port-Zeile bleibt — CV-Modulation ist weiter bedienbar)
        if (column->buttonMode && ! devMode)
            stackArea = columnBounds;
        else
            column->slider.setBounds (columnBounds.reduced (4, 2));

        layoutValueButtons (*column, stackArea);
    }
}

void FxModulePanel::layoutValueButtons (ParameterColumn& column, juce::Rectangle<int> stackArea)
{
    if (! column.buttonMode || stackArea.isEmpty())
        return;

    // Dev: +/−-Stepper unter den Stapeln (Anzahl nur hier veränderbar)
    if (devMode)
    {
        auto stepperRow = stackArea.removeFromBottom (stepperRowHeight);
        column.removeButton.setBounds (
            stepperRow.removeFromLeft (stepperRow.getWidth() / 2).reduced (1));
        column.addButton.setBounds (stepperRow.reduced (1));
    }

    const auto numButtons = static_cast<int> (column.valueButtons.size());

    if (numButtons == 0)
        return;

    const auto stacks = juce::jmax (1,
        (numButtons + ChassisSchema::maxUiButtonsPerStack - 1)
            / ChassisSchema::maxUiButtonsPerStack);
    const auto stackWidth = stackArea.getWidth() / stacks;

    // Wenige Buttons = hohe Touch-Ziele (≥53px bei ≤3); volle Stapel bleiben
    // bei ~32px — bewusste Ausnahme von der 44px-Regel (10.0) analog zur
    // 16px-Dev-Zeile, gekappt bei 34px
    const auto rowsUsed = juce::jmin (numButtons, ChassisSchema::maxUiButtonsPerStack);
    const auto buttonHeight = juce::jmin (34, stackArea.getHeight() / juce::jmax (1, rowsUsed));

    for (int buttonIndex = 0; buttonIndex < numButtons; ++buttonIndex)
    {
        const auto stackIndex = buttonIndex / ChassisSchema::maxUiButtonsPerStack;
        const auto rowIndex   = buttonIndex % ChassisSchema::maxUiButtonsPerStack;

        column.valueButtons[static_cast<size_t> (buttonIndex)]->setBounds (
            stackArea.getX() + stackIndex * stackWidth,
            stackArea.getY() + rowIndex * buttonHeight,
            stackWidth, buttonHeight);
    }
}

} // namespace conduit

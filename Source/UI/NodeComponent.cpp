#include "NodeComponent.h"

#include <cstdlib>

#include "Modules/ConduitModule.h"
#include "Modules/LinkAudioSendModule.h"
#include "Modules/ScopeModule.h"
#include "Modules/StepSequencerModule.h"
#include "PushLookAndFeel.h"

namespace conduit
{

namespace
{
    // Layout der I/O-Endpunkte MIT Pegelanzeigen
    constexpr int endpointMeterWidth  = 120;
    constexpr int endpointMeterHeight = 14;
    constexpr int endpointPortInset   = 24;   // Port ↔ Meter-Kante
    constexpr int endpointWidth       = 300;  // verbreiterte Kachel (Label + Meter + Port)
    constexpr int endpointPairColumn  = 20;   // Koppel-Toggle-Spalte (nur audio_in)
    constexpr int endpointSendColumn  = 24;   // Link-Send-Toggle-Spalte (nur audio_in)

    // Vertikaler Versatz der beiden Kabel-Anker eines Stereo-Paar-Ports —
    // die zwei Connections starten dicht beieinander (Doppel-Linien-Optik)
    constexpr int pairCableOffset = 3;
}

NodeComponent::NodeComponent (juce::ValueTree nodeTreeToBind,
                              GraphManager& graphManagerToUse,
                              NodeUiRegistry& uiRegistryToUse,
                              ChannelNames* channelNamesToUse,
                              LevelMeter* inputLevelsToUse,
                              LevelMeter* outputLevelsToUse,
                              InputLinkSend* inputSendToUse)
    : nodeTree (std::move (nodeTreeToBind)),
      graphManager (graphManagerToUse),
      uiRegistry (uiRegistryToUse),
      channelNames (channelNamesToUse),
      inputLevels (inputLevelsToUse),
      outputLevels (outputLevelsToUse),
      inputSend (inputSendToUse),
      nodeUuid (nodeTree.getProperty (id::nodeId).toString())
{
    uiRegistry.acquire (nodeUuid);
    nodeTree.addListener (this);

    const auto factoryKey = GraphManager::factoryKeyOf (nodeTree);
    isExternalEndpoint = graphManager.isExternalEndpoint (factoryKey);
    endpointIsInput    = (factoryKey == audioInputModuleId);
    const auto isExternal = isExternalEndpoint;

    deleteButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));  // ×
    deleteButton.onClick = [this]
    {
        const auto requested = graphManager.requestNodeDelete (nodeUuid);
        jassertquiet (requested);  // Node muss existieren, solange die UI lebt
    };
    addAndMakeVisible (deleteButton);

    // Externe I/O-Endpunkte sind Grundausstattung — nicht löschbar
    deleteButton.setVisible (! isExternal);

    // named_id im Header — Doppelklick benennt um (OSC-Pfad folgt, 7)
    titleLabel.setText (nodeTree.getProperty (id::moduleId).toString(),
                        juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (15.0f)));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    titleLabel.setEditable (false, ! isExternal, false);
    titleLabel.onTextChange = [this]
    {
        if (! graphManager.renameNode (nodeUuid, titleLabel.getText()))
            titleLabel.setText (nodeTree.getProperty (id::moduleId).toString(),
                                juce::dontSendNotification);  // abgelehnt → zurück
        // Bei Erfolg zieht der Property-Listener den sanitierten Namen nach
    };
    addAndMakeVisible (titleLabel);

    // Kopfzeile ist Grifffläche: das Label schluckt sonst alle Drags —
    // weitergeleitete Events landen in mouseDown/mouseDrag (getEventRelativeTo
    // rechnet um), Doppelklick-Rename bleibt unberührt
    titleLabel.addMouseListener (this, false);

    // Ports aus den persistierten Kanalzahlen (Schema 6.2); Kanal-Labels der
    // I/O-Endpunkte zieht rebuildPorts() gleich mit nach
    rebuildPorts();
    rebuildMeters();  // Pegelanzeigen der I/O-Endpunkte (falls Provider da)

    // Änderungen der ChannelNames (Rename, Gerätewechsel) ziehen Labels nach
    if (channelNames != nullptr && portLabelDirection().has_value())
        channelNames->addChangeListener (this);

    // Sequencer- und Send-Kacheln haben eine eigene Bedienleiste (Grid bzw.
    // Attenuator-Zeilen) — kein generischer Slider
    if (const auto parameter = firstParameter();
        parameter.isValid()
        && factoryKey != StepSequencerModule::staticModuleId
        && factoryKey != LinkAudioSendModule::staticModuleId)
    {
        parameterSlider.setRange ((double) parameter.getProperty (id::paramMin, 0.0),
                                  (double) parameter.getProperty (id::paramMax, 1.0), 0.0);
        parameterSlider.setValue ((double) parameter.getProperty (id::paramValue, 0.0),
                                  juce::dontSendNotification);

        // Schreibt NUR in den Tree — der GraphManager spiegelt auf das
        // Echtzeit-Atomic. Bewusst ohne UndoManager: Parameter-Sweeps sind
        // keine patchbaren Aktionen (gleiches Verhalten wie der OSC-Pfad 6.1).
        parameterSlider.onValueChange = [this]
        {
            // eigener Name — verschattet sonst das 'parameter' des umgebenden
            // if-Init (Clang -Wshadow-uncaptured-local unter -Werror)
            if (auto liveParameter = firstParameter(); liveParameter.isValid())
                liveParameter.setProperty (id::paramValue, parameterSlider.getValue(), nullptr);
        };
        addAndMakeVisible (parameterSlider);
    }

    // Modulspezifische Anzeigen direkt in der Kachel
    if (factoryKey == ScopeModule::staticModuleId)
    {
        scopeDisplay = std::make_unique<ScopeDisplay> (graphManager, nodeUuid);
        addAndMakeVisible (*scopeDisplay);
        setSize (252, 168);
    }
    else if (factoryKey == StepSequencerModule::staticModuleId)
    {
        stepGrid = std::make_unique<StepGridDisplay> (nodeTree, graphManager);
        addAndMakeVisible (*stepGrid);

        sequencerControls = std::make_unique<SequencerControlPanel> (nodeTree);
        addAndMakeVisible (*sequencerControls);
        setSize (492, 380);
    }
    else if (factoryKey == LinkAudioSendModule::staticModuleId)
    {
        sendPanel = std::make_unique<LinkAudioSendPanel> (nodeTree, graphManager);
        addAndMakeVisible (*sendPanel);

        // Höhe folgt der Eingangszahl (fixe Zahl, kein Live-Umbau)
        const auto numInputs = juce::jmax (1, nodeTree.getChildWithName (id::inputs).getNumChildren());
        setSize (280, touchTarget + LinkAudioSendPanel::heightForInputs (numInputs));
    }
    else if (isExternalEndpoint)
    {
        updateEndpointSize();  // Höhe folgt der Hardware-Kanalzahl (Schritt B)
    }
    else
    {
        setSize (defaultWidth, defaultHeight);
    }

    applyTreePosition();
}

NodeComponent::~NodeComponent()
{
    if (channelNames != nullptr)
        channelNames->removeChangeListener (this);

    nodeTree.removeListener (this);
    uiRegistry.release (nodeUuid);  // gibt eine wartende Phase 2 frei (5.3)
}

//==============================================================================
void NodeComponent::beginTeardown()
{
    if (tearingDown)
        return;

    // Phase 1 (5.3): Rendering-Updates stoppen, Listener deregistrieren,
    // Interaktion abschalten — die Registry-Freigabe folgt erst nach dem
    // letzten Render-Zyklus.
    tearingDown = true;
    nodeTree.removeListener (this);

    if (channelNames != nullptr)
        channelNames->removeChangeListener (this);
    setInterceptsMouseClicks (false, false);
    deleteButton.setEnabled (false);
    parameterSlider.setEnabled (false);
    titleLabel.setEnabled (false);

    if (scopeDisplay != nullptr)
        scopeDisplay->stopUpdates();  // keine Rendering-Updates mehr (5.3 Phase 1)

    if (stepGrid != nullptr)
        stepGrid->stopUpdates();

    if (sequencerControls != nullptr)
        sequencerControls->setEnabled (false);

    if (sendPanel != nullptr)
        sendPanel->stopUpdates();

    for (auto& toggle : pairToggles)
        toggle->setEnabled (false);

    for (auto& sendButton : sendButtons)
        sendButton->stopUpdates();

    for (auto& bar : meterBars)
        bar->stopUpdates();

    repaint();

    teardownVBlank = std::make_unique<juce::VBlankAttachment> (this, [this] (double)
    {
        // Nicht im VBlank-Callback zerstören — einen Message-Loop-Schritt
        // entkoppeln. Der VBlank feuert pro Frame: nur einmal dispatchen.
        if (teardownNotified)
            return;

        teardownNotified = true;

        juce::Component::SafePointer<NodeComponent> self (this);
        juce::MessageManager::callAsync ([self]
        {
            if (self != nullptr)
                self->completeTeardownNow();
        });
    });
}

void NodeComponent::completeTeardownNow()
{
    if (! tearingDown)
        return;

    teardownVBlank.reset();

    if (onTeardownFinished != nullptr)
        onTeardownFinished (*this);  // zerstört uns — danach nichts mehr anfassen
}

//==============================================================================
void NodeComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == nodeTree)
    {
        if (property == id::positionX || property == id::positionY)
            applyTreePosition();
        else if (property == id::nodeState
                 && tree.getProperty (id::nodeState).toString() == toString (NodeState::deleting))
            beginTeardown();
        else if (property == id::nodeError || property == id::tintColour)
            repaint();
        else if (property == id::moduleId)  // Rename (auch Undo/OSC-extern)
            titleLabel.setText (tree.getProperty (id::moduleId).toString(),
                                juce::dontSendNotification);
        else if (property == id::numInputChannels || property == id::numOutputChannels)
        {
            // I/O-Endpunkt hat die Hardware-Kanalzahl geändert (Schritt B):
            // Ports + Meter neu bauen, Kachel anpassen, Kabel folgen
            rebuildPorts();
            rebuildMeters();

            if (isExternalEndpoint)
                updateEndpointSize();

            resized();   // neue Ports positionieren (auch bei gleicher Größe)
            repaint();

            if (auto* parent = getParentComponent())
                parent->repaint();  // Kabel-Pfade des Canvas neu zeichnen
        }

        return;
    }

    // Slider folgt externen Quellen (OSC-Nachzug 6.1, Undo, Preset-Load) —
    // dontSendNotification verhindert die Rückkopplungsschleife.
    if (tree.hasType (id::parameter) && property == id::paramValue
        && tree == firstParameter())
        parameterSlider.setValue ((double) tree.getProperty (id::paramValue),
                                  juce::dontSendNotification);
}

void NodeComponent::applyTreePosition()
{
    setTopLeftPosition ((int) nodeTree.getProperty (id::positionX, 0),
                        (int) nodeTree.getProperty (id::positionY, 0));
}

juce::Point<int> NodeComponent::snapToSiblings (juce::Point<int> position) const
{
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return position;

    // Je Achse zur nächstgelegenen Geschwister-Kante innerhalb des
    // Fangbereichs — X und Y rasten unabhängig (Kachel kann gleichzeitig
    // auf Höhe von A und bündig unter B liegen)
    auto bestDx = siblingSnapRange + 1;
    auto bestDy = siblingSnapRange + 1;

    for (auto* child : parent->getChildren())
    {
        const auto* other = dynamic_cast<NodeComponent*> (child);
        if (other == nullptr || other == this)
            continue;

        const auto dx = other->getX() - position.x;
        const auto dy = other->getY() - position.y;

        if (std::abs (dx) < std::abs (bestDx))
            bestDx = dx;
        if (std::abs (dy) < std::abs (bestDy))
            bestDy = dy;
    }

    return { std::abs (bestDx) <= siblingSnapRange ? position.x + bestDx : position.x,
             std::abs (bestDy) <= siblingSnapRange ? position.y + bestDy : position.y };
}

juce::ValueTree NodeComponent::firstParameter() const
{
    return nodeTree.getChildWithName (id::parameters).getChild (0);
}

std::vector<NodeComponent::PortRow> NodeComponent::buildPortRows (
    int numChannels, const std::function<bool (int)>& isPairStart)
{
    std::vector<PortRow> rows;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const bool paired = channel + 1 < numChannels
                            && isPairStart != nullptr && isPairStart (channel);
        rows.push_back ({ channel, paired ? 2 : 1 });

        if (paired)
            ++channel;  // Partner-Kanal gehört zur selben Zeile
    }

    return rows;
}

bool NodeComponent::hasPairingUi() const noexcept
{
    // Pairing-Scope: der Audio-EINGANG (audio_in trägt Output-Ports = Hardware-
    // Inputs); die Paar-Definition lebt in ChannelNames (App-Zustand)
    return isExternalEndpoint && endpointIsInput && channelNames != nullptr;
}

void NodeComponent::rebuildPorts()
{
    inputPorts.clear();
    outputPorts.clear();
    pairToggles.clear();

    for (auto& sendButton : sendButtons)
        sendButton->stopUpdates();
    sendButtons.clear();

    inputChannelCount  = (int) nodeTree.getProperty (id::numInputChannels,  0);
    outputChannelCount = (int) nodeTree.getProperty (id::numOutputChannels, 0);

    // Stereo-Paare verschmelzen nur am audio_in-Endpunkt zu span-2-Zeilen
    const auto pairStart = hasPairingUi()
        ? std::function<bool (int)> ([this] (int channel)
              { return channelNames->isPortPairStart (ChannelNames::Direction::input, channel); })
        : std::function<bool (int)>();

    inputRows  = buildPortRows (inputChannelCount, nullptr);
    outputRows = buildPortRows (outputChannelCount, pairStart);

    const auto makePorts = [this] (bool isInput, const std::vector<PortRow>& rows,
                                   std::vector<std::unique_ptr<PortComponent>>& ports)
    {
        for (const auto& row : rows)
        {
            auto port = std::make_unique<PortComponent> (
                PortInfo { nodeUuid, isInput, row.channel, row.span });
            addAndMakeVisible (*port);
            ports.push_back (std::move (port));
        }
    };

    makePorts (true,  inputRows,  inputPorts);
    makePorts (false, outputRows, outputPorts);

    // Link-Send-Toggles: einer pro Port-ZEILE (Paar = ein Send am Anker) —
    // Klick schreibt nur das ChannelNames-Flag, Engine und UI folgen dem
    // Broadcast (7.2)
    if (hasPairingUi())
        for (const auto& row : outputRows)
        {
            auto sendButton = std::make_unique<InputSendButton> (*channelNames, inputSend,
                                                                 row.channel);
            addAndMakeVisible (*sendButton);
            sendButtons.push_back (std::move (sendButton));
        }

    // Koppel-Toggles zwischen benachbarten Kanal-Zeilen (audio_in): Klick
    // koppelt/löst (ChannelNames räumt Konflikte), der ChangeBroadcast baut
    // die Ports neu
    if (hasPairingUi())
        for (int channel = 0; channel + 1 < outputChannelCount; ++channel)
        {
            auto toggle = std::make_unique<juce::TextButton> (
                juce::String::fromUTF8 ("\xe2\x88\xa5"));  // ∥ — zwei Linien
            toggle->setTooltip (juce::String::fromUTF8 ("Stereo-Paar koppeln/l\xc3\xb6sen"));
            toggle->setClickingTogglesState (false);
            toggle->setToggleState (channelNames->isPortPairStart (ChannelNames::Direction::input,
                                                                   channel),
                                    juce::dontSendNotification);
            toggle->onClick = [this, channel]
            {
                const auto paired = channelNames->isPortPairStart (
                    ChannelNames::Direction::input, channel);
                channelNames->setPortPairedWithNext (ChannelNames::Direction::input,
                                                     channel, ! paired);
            };
            addAndMakeVisible (*toggle);
            pairToggles.push_back (std::move (toggle));
        }

    refreshPortTooltips();  // Kanal-Labels der I/O-Endpunkte nachziehen
}

void NodeComponent::updateEndpointSize()
{
    // Ein KANAL braucht rund eine Touch-Reihe (Meter/Labels bleiben eine
    // Zeile pro Kanal, auch wenn Paare zu einem Port verschmelzen). Mit
    // Metern breitere Kachel; audio_in zusätzlich die Koppel-Spalte.
    const auto maxChannels = juce::jmax (inputChannelCount, outputChannelCount, 1);
    const auto width = hasMeters()
                     ? endpointWidth + (hasPairingUi() ? endpointPairColumn + endpointSendColumn : 0)
                     : defaultWidth;
    setSize (width, touchTarget + maxChannels * 30);
}

void NodeComponent::rebuildMeters()
{
    for (auto& bar : meterBars)
        bar->stopUpdates();
    meterBars.clear();

    if (! isExternalEndpoint)
        return;

    // audio_in liest das Input-Metering (Ausgangs-Ports = Hardware-Inputs),
    // audio_out das Output-Metering (Eingangs-Ports)
    auto* provider = endpointIsInput ? inputLevels : outputLevels;
    if (provider == nullptr)
        return;

    // Eine Bar pro KANAL — auch wenn ein Stereo-Paar die Ports verschmilzt
    const int count = endpointIsInput ? outputChannelCount : inputChannelCount;
    for (int channel = 0; channel < count; ++channel)
    {
        auto bar = std::make_unique<LevelMeterBar> (provider, channel);
        addAndMakeVisible (*bar);
        meterBars.push_back (std::move (bar));
    }
}

bool NodeComponent::hasMeters() const noexcept
{
    return ! meterBars.empty();
}

juce::Rectangle<int> NodeComponent::meterBoundsFor (bool isInputEndpoint, int channel) const
{
    // Kanal-Bank des Endpunkts: audio_in trägt Ausgangs-Ports, audio_out
    // Eingangs. Feste Zeile pro KANAL (Pairing verschiebt nur die Ports).
    const auto rowY = channelRowY (! isInputEndpoint, channel);
    const int  y    = rowY - endpointMeterHeight / 2;
    const int  rightColumns = hasPairingUi() ? endpointPairColumn + endpointSendColumn : 0;
    const int  x    = isInputEndpoint
                    ? getWidth() - endpointPortInset - rightColumns - endpointMeterWidth
                    : endpointPortInset;
    return { x, y, endpointMeterWidth, endpointMeterHeight };
}

//==============================================================================
std::optional<ChannelNames::Direction> NodeComponent::portLabelDirection() const
{
    if (channelNames == nullptr)
        return std::nullopt;

    // audio_input speist den Graph: seine OUTPUT-Ports sind Hardware-Inputs
    const auto factoryKey = GraphManager::factoryKeyOf (nodeTree);

    if (factoryKey == audioInputModuleId)
        return ChannelNames::Direction::input;

    if (factoryKey == audioOutputModuleId)
        return ChannelNames::Direction::output;

    return std::nullopt;
}

void NodeComponent::refreshPortTooltips()
{
    const auto direction = portLabelDirection();
    if (! direction.has_value())
        return;

    auto& ports = *direction == ChannelNames::Direction::input ? outputPorts : inputPorts;

    for (auto& port : ports)
    {
        const auto& info = port->getInfo();
        auto tooltip = channelNames->getLabel (*direction, info.channel);

        if (info.span == 2)  // Stereo-Paar: beide Kanal-Labels
            tooltip << " / " << channelNames->getLabel (*direction, info.channel + 1);

        port->setTooltip (tooltip);
    }
}

void NodeComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // ChannelNames-Änderung: Labels ODER Stereo-Pairing — Ports neu bauen
    // (Paare verschmelzen/lösen sich), Kabel-Anker verschieben sich
    rebuildPorts();
    resized();
    repaint();

    if (auto* parent = getParentComponent())
        parent->repaint();  // Kabel-Pfade folgen den Port-Ankern
}

//==============================================================================
int NodeComponent::getNumInputPorts() const noexcept  { return static_cast<int> (inputPorts.size()); }
int NodeComponent::getNumOutputPorts() const noexcept { return static_cast<int> (outputPorts.size()); }
int NodeComponent::getNumMeterBars() const noexcept   { return static_cast<int> (meterBars.size()); }
int NodeComponent::getNumSendButtons() const noexcept { return static_cast<int> (sendButtons.size()); }

int NodeComponent::channelRowY (bool isInputBank, int channel) const
{
    const auto count = juce::jmax (1, isInputBank ? inputChannelCount : outputChannelCount);
    const auto availableHeight = getHeight() - touchTarget;  // unterhalb des Headers

    return touchTarget + availableHeight * (channel + 1) / (count + 1);
}

juce::Point<int> NodeComponent::getPortCentre (bool isInput, int channel) const
{
    const int x = isInput ? 12 : getWidth() - 12;

    // Stereo-Paar: beide Kanäle ankern am selben Port (Mitte zwischen den
    // Kanal-Zeilen), ∓3px versetzt — zwei Connections liegen als Doppel-Linie
    const auto& rows = isInput ? inputRows : outputRows;
    for (const auto& row : rows)
        if (row.span == 2 && (channel == row.channel || channel == row.channel + 1))
        {
            const auto mid = (channelRowY (isInput, row.channel)
                              + channelRowY (isInput, row.channel + 1)) / 2;
            return { x, mid + (channel == row.channel ? -pairCableOffset : pairCableOffset) };
        }

    return { x, channelRowY (isInput, channel) };
}

std::optional<int> NodeComponent::pairAnchorForPort (bool isInput, int channel) const
{
    const auto& rows = isInput ? inputRows : outputRows;
    for (const auto& row : rows)
        if (row.span == 2 && (channel == row.channel || channel == row.channel + 1))
            return row.channel;

    return std::nullopt;
}

const PortComponent* NodeComponent::findPortNear (juce::Point<int> localPoint,
                                                  int maxDistance) const
{
    const PortComponent* nearest = nullptr;
    auto nearestDistance = maxDistance;

    const auto consider = [&] (const std::vector<std::unique_ptr<PortComponent>>& ports)
    {
        for (const auto& port : ports)
        {
            const auto centre = getPortCentre (port->getInfo().isInput, port->getInfo().channel);
            const auto distance = juce::roundToInt (centre.getDistanceFrom (localPoint));

            if (distance <= nearestDistance)
            {
                nearest = port.get();
                nearestDistance = distance;
            }
        }
    };

    consider (inputPorts);
    consider (outputPorts);
    return nearest;
}

//==============================================================================
void NodeComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto error  = nodeTree.getProperty (id::nodeError).toString();

    auto fill = push::colours::tile;

    if (tearingDown)
        fill = fill.withAlpha (0.4f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 8.0f);

    // Kachel-Tint (7.4): Track-Farbe des announce-gebundenen Live-Devices
    // als Streifen an der Unterkante der Kopfzeile — Live liefert
    // 0x00RRGGBB, deshalb opak machen
    if (const auto tintVar = nodeTree.getProperty (id::tintColour); ! tintVar.isVoid())
    {
        const auto tint = juce::Colour (0xff000000u | static_cast<juce::uint32> ((int) tintVar));
        g.setColour (tearingDown ? tint.withAlpha (0.4f) : tint);
        g.fillRect (bounds.withY ((float) touchTarget - 3.0f)
                          .withHeight (3.0f)
                          .reduced (6.0f, 0.0f));
    }

    // Kontur nur im Fehlerfall — Push-Minimalismus verzichtet sonst auf
    // sichtbare Umrandungen (Kachel-Fläche selbst grenzt gegen den Canvas ab)
    if (error.isNotEmpty())
    {
        g.setColour (juce::Colours::orangered);
        g.drawRoundedRectangle (bounds, 8.0f, 2.0f);
    }

    if (error.isNotEmpty())
    {
        g.setColour (juce::Colours::orangered);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (error, getLocalBounds().reduced (8).removeFromBottom (16),
                    juce::Justification::centredLeft);
    }

    // Kanal-Labels neben den Ports der I/O-Endpunkte — Touch hat keinen
    // Hover, deshalb gemalt statt nur als Tooltip (ChannelNames-Quelle)
    if (const auto direction = portLabelDirection(); direction.has_value())
    {
        const auto isInputEndpoint = *direction == ChannelNames::Direction::input;

        // Eine Label-Zeile pro KANAL (nicht pro Port — Paare teilen den Port,
        // behalten aber beide Kanal-Zeilen samt Meter)
        const auto numPorts = isInputEndpoint ? outputChannelCount : inputChannelCount;

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));

        for (int channel = 0; channel < numPorts; ++channel)
        {
            // Feste Zeile pro Kanal — auch gepaarte Kanäle behalten ihr Label
            const auto rowY  = channelRowY (! isInputEndpoint, channel);
            const auto portX = isInputEndpoint ? getWidth() - 12 : 12;

            // Mit Metern liegt das Label jenseits des Balkens; sonst direkt am Port
            juce::Rectangle<int> area;
            if (hasMeters())
            {
                const auto meter = meterBoundsFor (isInputEndpoint, channel);
                area = isInputEndpoint
                     ? juce::Rectangle<int> (8, rowY - 8, meter.getX() - 12, 16)              // links vom Meter
                     : juce::Rectangle<int> (meter.getRight() + 8, rowY - 8,
                                             getWidth() - meter.getRight() - 14, 16);          // rechts vom Meter
            }
            else
            {
                area = isInputEndpoint
                     ? juce::Rectangle<int> (portX - 110, rowY - 8, 90, 16)
                     : juce::Rectangle<int> (portX + 20,  rowY - 8, 90, 16);
            }

            g.drawText (channelNames->getLabel (*direction, channel), area,
                        isInputEndpoint ? juce::Justification::centredRight
                                        : juce::Justification::centredLeft);
        }
    }
}

void NodeComponent::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (touchTarget);
    deleteButton.setBounds (header.removeFromRight (touchTarget));
    titleLabel.setBounds (header.withTrimmedLeft (8));

    // Eingerückt, damit der Slider nicht unter den Port-Hit-Zonen liegt
    parameterSlider.setBounds (bounds.removeFromBottom (touchTarget).reduced (28, 0));

    if (scopeDisplay != nullptr)
        scopeDisplay->setBounds (getLocalBounds().withTrimmedTop (touchTarget)
                                     .reduced (24, 8));  // Platz für die Port-Hit-Zonen

    if (stepGrid != nullptr)
    {
        auto sequencerArea = getLocalBounds().withTrimmedTop (touchTarget).reduced (24, 4);
        sequencerControls->setBounds (sequencerArea.removeFromBottom (SequencerControlPanel::preferredHeight));
        stepGrid->setBounds (sequencerArea.withTrimmedBottom (6));
    }

    if (sendPanel != nullptr)
        sendPanel->setBounds (getLocalBounds().withTrimmedTop (touchTarget).reduced (22, 4));

    const auto placePorts = [this] (std::vector<std::unique_ptr<PortComponent>>& ports)
    {
        for (auto& port : ports)
        {
            const auto& info = port->getInfo();

            // Paar-Port: mittig zwischen seinen beiden Kanal-Zeilen — die
            // ∓3px-Kabel-Anker (getPortCentre) liegen in der Hit-Zone
            const auto y = info.span == 2
                ? (channelRowY (info.isInput, info.channel)
                   + channelRowY (info.isInput, info.channel + 1)) / 2
                : channelRowY (info.isInput, info.channel);

            port->setCentrePosition (info.isInput ? 12 : getWidth() - 12, y);
        }
    };

    placePorts (inputPorts);
    placePorts (outputPorts);

    // Koppel-Toggles: in der Spalte zwischen Send und Port, mittig zwischen
    // den beiden Kanal-Zeilen, die sie koppeln (Toggle i = Kanäle i, i+1)
    for (int i = 0; i < (int) pairToggles.size(); ++i)
    {
        const auto y = (channelRowY (false, i) + channelRowY (false, i + 1)) / 2;
        pairToggles[(std::size_t) i]->setBounds (getWidth() - endpointPortInset - endpointPairColumn,
                                                 y - 10, endpointPairColumn, 20);
    }

    // Link-Send-Toggles: eigene Spalte links der Koppel-Toggles, ein Button
    // pro Port-Zeile (Paar-Zeilen mittig zwischen ihren Kanal-Zeilen)
    for (auto& sendButton : sendButtons)
    {
        const auto anchor = sendButton->getAnchorPort();
        const auto row = pairAnchorForPort (false, anchor).has_value()
                       ? (channelRowY (false, anchor) + channelRowY (false, anchor + 1)) / 2
                       : channelRowY (false, anchor);

        sendButton->setBounds (getWidth() - endpointPortInset - endpointPairColumn
                                   - endpointSendColumn,
                               row - 10, endpointSendColumn - 2, 20);
    }

    for (int channel = 0; channel < (int) meterBars.size(); ++channel)
        meterBars[(std::size_t) channel]->setBounds (meterBoundsFor (endpointIsInput, channel));
}

void NodeComponent::mouseDown (const juce::MouseEvent& event)
{
    toFront (false);  // gegriffene Kachel über überlappende Nachbarn heben
    dragger.startDraggingComponent (this, event);
}

void NodeComponent::mouseDrag (const juce::MouseEvent& event)
{
    // ≤ 1-Frame-Feedback: Component bewegt sich sofort, der Tree zieht nach.
    // Ohne UndoManager — ein Drag erzeugt sonst hunderte Undo-Schritte.
    dragger.dragComponent (this, event, nullptr);

    // Beide Properties aus dem lokalen Wert schreiben: der X-Write ruft
    // synchron applyTreePosition auf und setzt die Component auf das noch
    // alte Tree-Y zurück — getY() danach wäre der alte Wert (Drag war
    // dadurch auf horizontal beschränkt)
    const auto snapped = snapToSiblings (getPosition());
    setTopLeftPosition (snapped);
    nodeTree.setProperty (id::positionX, snapped.x, nullptr);
    nodeTree.setProperty (id::positionY, snapped.y, nullptr);
}

} // namespace conduit

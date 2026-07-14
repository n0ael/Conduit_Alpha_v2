#include "MacroPanel.h"

#include "TouchLive/AbletonParamTarget.h"
#include "Util/CcNames.h"

namespace conduit
{

namespace
{
    constexpr int kPaddingX     = 8;
    constexpr int kHeaderHeight = 24;
    constexpr int kRowGap       = 4;
    constexpr int kAddTileHeight = 32;
}

//==============================================================================
MacroPanel::TargetRow::TargetRow (MacroPanel& ownerToUse, int indexToUse)
    : owner (ownerToUse), index (indexToUse)
{
    addChildComponent (midiTile);
    addChildComponent (liveTile);
    addChildComponent (hardwareTile);
    addChildComponent (removeTile);
    addChildComponent (channelField);
    addChildComponent (ccField);
    addChildComponent (trackCombo);
    addChildComponent (deviceCombo);
    addChildComponent (parameterCombo);
    addChildComponent (hwSummaryTile);

    midiTile.onClick     = [this] { applyTargetType (TargetType::midi); };
    liveTile.onClick     = [this] { applyTargetType (TargetType::live); };
    hardwareTile.onClick = [this] { applyTargetType (TargetType::hardware); };
    removeTile.onClick = [this] { owner.removeTargetSlot (index); };

    // Block L: Tooltip zeigt den Funktionsnamen der Nummer live beim Swipe
    // ("Mod Wheel" statt nur "1") -- kein Layout-Eingriff am NumberFieldBracket.
    channelField.onValueCommitted = [this] (double)
    {
        if (targetType == TargetType::hardware)
            rebuildHardwareTargetForChannelChange();   // Kanalwechsel: Ziel mit neuem Kanal neu bauen
        else
            rebuildMidiTarget();
    };
    ccField.onValueChanged   = [this] (double v) { ccField.setTooltip (CcNames::displayName ((int) v)); };
    ccField.onValueCommitted = [this] (double)   { rebuildMidiTarget(); };

    trackCombo.onChange     = [this] { populateDeviceCombo(); };
    deviceCombo.onChange    = [this] { populateParameterCombo(); };
    parameterCombo.onChange = [this] { createAbletonTarget(); };

    // MIDI-Rig M3: Zusammenfassungs-Kachel oeffnet den semantischen Picker
    // (CallOutBox) statt zweier ComboBoxen.
    hwSummaryTile.onClick = [this] { openHardwareTargetPicker(); };

    rebuildFromBinding();
}

grid::MacroBinding* MacroPanel::TargetRow::binding() const noexcept
{
    return owner.macroBindings.get (owner.currentKey(), index);
}

void MacroPanel::TargetRow::rebuildFromBinding()
{
    auto* b = binding();

    // Kurveneditor bindet an die im Binding lebende Kurve -- bei jedem
    // Rebuild neu erzeugen (Referenz koennte auf ein anderes Binding zeigen).
    curveTile.reset();
    if (b != nullptr)
    {
        curveTile = std::make_unique<CurveEditorTile> (b->curve);
        addChildComponent (*curveTile);
    }

    // Typ aus dem vorhandenen Ziel ableiten (Rebuild verliert sonst die Wahl).
    if (b != nullptr && b->target != nullptr)
    {
        if (auto* live = dynamic_cast<grid::AbletonParamTarget*> (b->target.get()))
        {
            // Block K2 (User-Feldtest 12.07.2026): die geladene Zuweisung in
            // den Combos ANZEIGEN — leere Dropdowns sahen aus wie "verloren".
            targetType = TargetType::live;
            applySpecToCombos (live->spec());
        }
        else if (auto* nrpn = dynamic_cast<grid::MidiNrpnTarget*> (b->target.get()))
        {
            // MIDI-Rig M2/M3-Fund: NRPN-/PC-Ziele fehlten hier bisher komplett
            // (nur MidiCcTarget/AbletonParamTarget wurden erkannt) -- beim
            // Neuladen einer Session mit einem solchen Ziel landete die Zeile
            // faelschlich im Live-Zustand mit leeren Combos. Auswahl selbst
            // kommt erst beim naechsten Picker-Tap (hasHwSelection bleibt
            // false, describe() reicht bis dahin fuer die Anzeige).
            targetType = TargetType::hardware;
            channelField.setValue (nrpn->channel(), juce::dontSendNotification);
            hasHwSelection = false;
        }
        else if (auto* pc = dynamic_cast<grid::MidiProgramChangeTarget*> (b->target.get()))
        {
            targetType = TargetType::hardware;
            channelField.setValue (pc->channel(), juce::dontSendNotification);
            hasHwSelection = false;
        }
        else if (auto* midi = dynamic_cast<grid::MidiCcTarget*> (b->target.get()))
        {
            targetType = TargetType::midi;
            channelField.setValue (midi->channel(), juce::dontSendNotification);
            ccField.setValue (midi->ccNumber(), juce::dontSendNotification);
            ccField.setTooltip (CcNames::displayName (midi->ccNumber()));   // Block L
        }
        else
        {
            targetType = TargetType::live;
        }
    }

    midiTile.setActive (targetType == TargetType::midi);
    liveTile.setActive (targetType == TargetType::live);
    hardwareTile.setActive (targetType == TargetType::hardware);

    if (targetType == TargetType::live && trackCombo.getNumItems() == 0)
        populateTrackCombo();

    updateHwSummaryText();

    resized();
    repaint();
}

void MacroPanel::TargetRow::applyTargetType (TargetType newType)
{
    if (newType == targetType)
        return;

    targetType = newType;
    midiTile.setActive (targetType == TargetType::midi);
    liveTile.setActive (targetType == TargetType::live);
    hardwareTile.setActive (targetType == TargetType::hardware);

    if (auto* b = binding())
    {
        if (targetType == TargetType::midi)
            rebuildMidiTarget();
        else
            b->target.reset();   // Live/Hardware: erst nach Auswahl konfiguriert
    }

    if (targetType == TargetType::live)
        populateTrackCombo();
    else if (targetType == TargetType::hardware)
    {
        hasHwSelection = false;
        updateHwSummaryText();
    }

    resized();
    repaint();
}

void MacroPanel::TargetRow::rebuildMidiTarget()
{
    auto* b = binding();
    if (b == nullptr || targetType != TargetType::midi)
        return;

    b->target = std::make_unique<grid::MidiCcTarget> (
        owner.midiTarget, (int) channelField.getValue(), (int) ccField.getValue());
    repaint();
}

void MacroPanel::TargetRow::populateTrackCombo()
{
    trackCombo.clear (juce::dontSendNotification);
    trackKeys.clear();

    auto tracks = owner.liveSetModel.getDomain ("tracks");

    for (int i = 0; i < tracks.getNumChildren(); ++i)
    {
        const auto item = tracks.getChild (i);
        const auto key = item.getProperty (touchlive::id::itemKey).toString();
        const auto name = item.getProperty ("name").toString();

        if (key.isEmpty())
            continue;

        trackKeys.add (key);
        trackCombo.addItem (name.isNotEmpty() ? name : key, trackKeys.size());
    }

    deviceCombo.clear (juce::dontSendNotification);
    parameterCombo.clear (juce::dontSendNotification);
    deviceIds.clear();
}

void MacroPanel::TargetRow::populateDeviceCombo()
{
    deviceCombo.clear (juce::dontSendNotification);
    parameterCombo.clear (juce::dontSendNotification);
    deviceIds.clear();

    const auto trackIndex = trackCombo.getSelectedId() - 1;
    if (trackIndex < 0 || trackIndex >= trackKeys.size())
        return;

    auto devices = owner.liveSetModel.getDomain ("devices");
    const auto chain = devices.getProperty ("chain:" + trackKeys[trackIndex]);

    if (const auto* chainArray = chain.getArray())
    {
        for (const auto& entry : *chainArray)
        {
            const auto dvid = entry.toString();
            if (dvid.isEmpty())
                continue;

            const auto deviceItem = owner.liveSetModel.findItem ("devices", "dev:" + dvid);
            const auto name = deviceItem.isValid() ? deviceItem.getProperty ("name").toString()
                                                   : dvid;

            deviceIds.add (dvid);
            deviceCombo.addItem (name.isNotEmpty() ? name : dvid, deviceIds.size());
        }
    }
}

void MacroPanel::TargetRow::populateParameterCombo()
{
    parameterCombo.clear (juce::dontSendNotification);

    const auto deviceIndex = deviceCombo.getSelectedId() - 1;
    if (deviceIndex < 0 || deviceIndex >= deviceIds.size())
        return;

    auto devices = owner.liveSetModel.getDomain ("devices");
    const auto meta = devices.getProperty ("parmeta:" + deviceIds[deviceIndex]);

    if (const auto* metaArray = meta.getArray())
    {
        // Index 0 = "Device On" (Schalter) -- fuer Macros ab 1 anbieten;
        // Combo-Id = parmeta-Index + 1 (Combo-Ids muessen > 0 sein).
        for (int i = 1; i < metaArray->size(); ++i)
        {
            const auto& entry = metaArray->getReference (i);
            const auto name = entry.getProperty ("name", {}).toString();
            parameterCombo.addItem (name.isNotEmpty() ? name : ("Param " + juce::String (i)), i + 1);
        }
    }
}

void MacroPanel::TargetRow::applySpecToCombos (const grid::LiveParamSpec& spec)
{
    populateTrackCombo();

    for (int i = 0; i < trackCombo.getNumItems(); ++i)
    {
        if (trackCombo.getItemText (i) == spec.trackName)
        {
            trackCombo.setSelectedId (i + 1, juce::dontSendNotification);
            break;
        }
    }

    populateDeviceCombo();

    auto sameNameSeen = 0;
    for (int i = 0; i < deviceCombo.getNumItems(); ++i)
    {
        if (deviceCombo.getItemText (i) != spec.deviceName)
            continue;

        if (sameNameSeen == spec.deviceOrdinal)
        {
            deviceCombo.setSelectedId (i + 1, juce::dontSendNotification);
            break;
        }

        ++sameNameSeen;
    }

    populateParameterCombo();

    for (int i = 0; i < parameterCombo.getNumItems(); ++i)
    {
        if (parameterCombo.getItemText (i) == spec.paramName)
        {
            parameterCombo.setSelectedId (parameterCombo.getItemId (i), juce::dontSendNotification);
            break;
        }
    }
}

void MacroPanel::TargetRow::createAbletonTarget()
{
    auto* b = binding();
    if (b == nullptr || targetType != TargetType::live)
        return;

    const auto deviceIndex = deviceCombo.getSelectedId() - 1;
    const auto parameterIndex = parameterCombo.getSelectedId() - 1;
    if (deviceIndex < 0 || deviceIndex >= deviceIds.size() || parameterIndex < 1)
        return;

    auto devices = owner.liveSetModel.getDomain ("devices");
    const auto meta = devices.getProperty ("parmeta:" + deviceIds[deviceIndex]);
    const auto* metaArray = meta.getArray();
    if (metaArray == nullptr || parameterIndex >= metaArray->size())
        return;

    const auto& entry = metaArray->getReference (parameterIndex);
    const auto minValue = (float) (double) entry.getProperty ("min", 0.0);
    const auto maxValue = (float) (double) entry.getProperty ("max", 1.0);
    const auto quantised = (bool) entry.getProperty ("quant", false);

    // Block K: Re-Resolve-Merkmale (Rule touchlive — dvid ist Laufzeit-ID;
    // persistiert werden Track-/Device-/Parameter-NAME + Device-Ordinal =
    // Anzahl GLEICHNAMIGER Devices davor in der Chain).
    grid::LiveParamSpec spec;
    spec.trackName   = trackCombo.getText();
    spec.deviceName  = deviceCombo.getText();
    spec.paramName   = parameterCombo.getText();
    spec.displayName = spec.deviceName + ": " + spec.paramName;

    for (int i = 0; i < deviceIndex; ++i)
        if (deviceCombo.getItemText (i) == spec.deviceName)
            ++spec.deviceOrdinal;

    auto target = std::make_unique<grid::AbletonParamTarget> (owner.touchLiveClient,
                                                              std::move (spec));
    target->resolve (deviceIds[deviceIndex], parameterIndex, minValue, maxValue, quantised);
    b->target = std::move (target);
    repaint();
}

//==============================================================================
// MIDI-Rig M3 (semantischer Picker, ADR 006)

void MacroPanel::TargetRow::openHardwareTargetPicker()
{
    auto picker = std::make_unique<HardwareTargetPicker> (
        MidiTargetBrowserModel (owner.hardwareDb, owner.profileLibrary));

    picker->onTargetChosen = [this] (const MidiTargetBrowserModel::Row& selection)
    { createHardwareTarget (selection); };

    juce::CallOutBox::launchAsynchronously (std::move (picker),
                                            hwSummaryTile.getScreenBounds(), nullptr);
}

void MacroPanel::TargetRow::createHardwareTarget (const MidiTargetBrowserModel::Row& selection)
{
    auto* b = binding();
    if (b == nullptr || targetType != TargetType::hardware)
        return;

    hwSelection = selection;
    hasHwSelection = true;

    // Klartext-DB-Geraet (E1b) UND CSV-CC-Parameter bauen weiter einen
    // ganz normalen MidiCcTarget; nur CSV-NRPN-Parameter einen
    // MidiNrpnTarget mit min/max aus dem Profil (M2-Verhalten unveraendert,
    // nur die Auswahl kommt jetzt aus dem Picker statt aus zwei Combos).
    if (selection.isCc)
        b->target = std::make_unique<grid::MidiCcTarget> (
            owner.midiTarget, (int) channelField.getValue(), selection.number);
    else
        b->target = std::make_unique<grid::MidiNrpnTarget> (
            owner.midiTarget, (int) channelField.getValue(), selection.number,
            selection.minValue, selection.maxValue, selection.displayName);

    updateHwSummaryText();
    repaint();
}

void MacroPanel::TargetRow::rebuildHardwareTargetForChannelChange()
{
    if (hasHwSelection)
    {
        createHardwareTarget (hwSelection);
        return;
    }

    // Session-Reload-Fall: in DIESER Session noch nicht ueber den Picker
    // gewaehlt -- aus dem bestehenden (geladenen) Ziel rekonstruieren, nur
    // der Kanal aendert sich.
    auto* b = binding();
    if (b == nullptr)
        return;

    if (auto* nrpn = dynamic_cast<grid::MidiNrpnTarget*> (b->target.get()))
        b->target = std::make_unique<grid::MidiNrpnTarget> (
            owner.midiTarget, (int) channelField.getValue(), nrpn->nrpnNumber(),
            nrpn->rangeMin(), nrpn->rangeMax(), nrpn->name());
    else if (auto* cc = dynamic_cast<grid::MidiCcTarget*> (b->target.get()))
        b->target = std::make_unique<grid::MidiCcTarget> (
            owner.midiTarget, (int) channelField.getValue(), cc->ccNumber());

    updateHwSummaryText();
    repaint();
}

void MacroPanel::TargetRow::updateHwSummaryText()
{
    const auto* b = binding();
    hwSummaryTile.setText (b != nullptr && b->target != nullptr
                               ? b->target->describe()
                               : juce::String::fromUTF8 ("Ger\xc3\xa4t w\xc3\xa4hlen\xe2\x80\xa6"));
}

void MacroPanel::TargetRow::setExpanded (bool shouldBeExpanded)
{
    if (expanded == shouldBeExpanded)
        return;

    expanded = shouldBeExpanded;
    resized();
    repaint();
}

int MacroPanel::TargetRow::preferredHeight() const noexcept
{
    return expanded ? kExpandedHeight : kCollapsedHeight;
}

void MacroPanel::TargetRow::refreshLiveValues()
{
    if (const auto* b = binding())
    {
        if (expanded && curveTile != nullptr)
            curveTile->setLiveValues (b->lastInput01, b->lastOutput01);
        else if (! expanded)
            repaint();   // eingeklappt: Punkt auf der Linie wandert
    }
}

void MacroPanel::TargetRow::resized()
{
    const auto showDetail = expanded;

    midiTile.setVisible (showDetail);
    liveTile.setVisible (showDetail);
    hardwareTile.setVisible (showDetail);
    removeTile.setVisible (showDetail);
    channelField.setVisible (showDetail
        && (targetType == TargetType::midi || targetType == TargetType::hardware));
    ccField.setVisible (showDetail && targetType == TargetType::midi);
    trackCombo.setVisible (showDetail && targetType == TargetType::live);
    deviceCombo.setVisible (showDetail && targetType == TargetType::live);
    parameterCombo.setVisible (showDetail && targetType == TargetType::live);
    hwSummaryTile.setVisible (showDetail && targetType == TargetType::hardware);
    if (curveTile != nullptr)
        curveTile->setVisible (showDetail);

    if (! showDetail)
        return;

    auto area = getLocalBounds().reduced (4);
    area.removeFromTop (22);   // Kopfzeile (describe, paint)

    auto typeRow = area.removeFromTop (26);
    removeTile.setBounds (typeRow.removeFromRight (26));
    typeRow.removeFromRight (4);
    const auto typeTileWidth = (typeRow.getWidth() - 8) / 3;
    midiTile.setBounds (typeRow.removeFromLeft (typeTileWidth));
    typeRow.removeFromLeft (4);
    liveTile.setBounds (typeRow.removeFromLeft (typeTileWidth));
    typeRow.removeFromLeft (4);
    hardwareTile.setBounds (typeRow);

    area.removeFromTop (4);

    if (targetType == TargetType::midi)
    {
        auto configRow = area.removeFromTop (30);
        const auto fieldWidth = (configRow.getWidth() - 4) / 2;
        channelField.setBounds (configRow.removeFromLeft (fieldWidth));
        configRow.removeFromLeft (4);
        ccField.setBounds (configRow);
        area.removeFromTop (4);
        area.removeFromTop (64);   // Platz, den der Live-Browser braeuchte (stabile Kurvenhoehe)
    }
    else if (targetType == TargetType::live)
    {
        trackCombo.setBounds (area.removeFromTop (28));
        area.removeFromTop (3);
        deviceCombo.setBounds (area.removeFromTop (28));
        area.removeFromTop (3);
        parameterCombo.setBounds (area.removeFromTop (28));
        area.removeFromTop (4);
        area.removeFromTop (8);
    }
    else if (targetType == TargetType::hardware)
    {
        // MIDI-Rig M3: Kanal + Zusammenfassungs-Kachel nebeneinander -- Tap
        // auf die Kachel oeffnet den semantischen Picker (CallOutBox).
        auto configRow = area.removeFromTop (30);
        const auto fieldWidth = (configRow.getWidth() - 4) / 2;
        channelField.setBounds (configRow.removeFromLeft (fieldWidth));
        configRow.removeFromLeft (4);
        hwSummaryTile.setBounds (configRow);
        area.removeFromTop (4);
        area.removeFromTop (28 + 4 + 34);   // Platz, den frueher hwParamCombo + Puffer brauchten
    }
    else
    {
        area.removeFromTop (98);   // kein Typ gewaehlt: Platzhalter
    }

    if (curveTile != nullptr)
        curveTile->setBounds (area);
}

void MacroPanel::TargetRow::paint (juce::Graphics& g)
{
    const auto tile = getLocalBounds().toFloat();
    g.setColour (expanded ? push::colours::tile : push::colours::panel);
    g.fillRoundedRectangle (tile, 4.0f);
    g.setColour (expanded ? push::colours::ledWhite.withAlpha (0.4f) : push::colours::outline);
    g.drawRoundedRectangle (tile.reduced (0.5f), 4.0f, 1.0f);

    const auto* b = binding();
    const auto label = juce::String (index + 1) + "  "
                       + (b != nullptr && b->target != nullptr ? b->target->describe()
                                                               : juce::String ("Kein Ziel"));

    g.setColour (b != nullptr && b->target != nullptr ? push::colours::text : push::colours::textDim);
    g.setFont (push::scaledFont (11.0f));

    if (expanded)
    {
        g.drawFittedText (label, getLocalBounds().reduced (8, 4).removeFromTop (18),
                          juce::Justification::centredLeft, 1, 1.0f);
        return;
    }

    // Compact-View (Masterplan Block E): Name links, rechts eine Linie mit
    // Punkt = aktuell gesendeter Wert und Min/Max als Striche.
    auto area = getLocalBounds().reduced (8, 4);
    const auto lineArea = area.removeFromRight (juce::jmax (60, area.getWidth() / 3)).toFloat();
    g.drawFittedText (label, area, juce::Justification::centredLeft, 1, 1.0f);

    const auto midY = lineArea.getCentreY();
    g.setColour (push::colours::outline);
    g.drawLine (lineArea.getX(), midY, lineArea.getRight(), midY, 1.5f);

    if (b != nullptr)
    {
        const auto outMin = juce::jlimit (0.0f, 1.0f, juce::jmin (b->curve.getOutputMin(),
                                                                  b->curve.getOutputMax()));
        const auto outMax = juce::jlimit (0.0f, 1.0f, juce::jmax (b->curve.getOutputMin(),
                                                                  b->curve.getOutputMax()));

        g.setColour (push::colours::textDim);
        for (const auto bound : { outMin, outMax })
        {
            const auto x = lineArea.getX() + bound * lineArea.getWidth();
            g.drawLine (x, midY - 4.0f, x, midY + 4.0f, 1.5f);
        }

        g.setColour (push::colours::ledWhite);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f)
                           .withCentre ({ lineArea.getX() + b->lastOutput01 * lineArea.getWidth(), midY }));
    }
}

void MacroPanel::TargetRow::mouseUp (const juce::MouseEvent& event)
{
    if (! expanded && getLocalBounds().contains (event.getPosition()))
        owner.selectRow (index);
}

//==============================================================================
MacroPanel::MacroPanel (grid::MacroBindings& bindingsToUse, grid::IMidiOutputTarget& midiTargetToUse,
                        LiveSetModel& liveSetModelToUse, TouchLiveClient& touchLiveClientToUse,
                        grid::MidiInBindings& midiInBindingsToUse,
                        grid::HardwareCcDatabase& hardwareDbToUse,
                        MidiProfileLibrary& profileLibraryToUse)
    : macroBindings (bindingsToUse), midiTarget (midiTargetToUse),
      liveSetModel (liveSetModelToUse), touchLiveClient (touchLiveClientToUse),
      midiInBindings (midiInBindingsToUse), hardwareDb (hardwareDbToUse),
      profileLibrary (profileLibraryToUse)
{
    addAndMakeVisible (titleLabel);
    addChildComponent (axisXTile);
    addChildComponent (axisYTile);
    addChildComponent (midiInTile);
    addChildComponent (learnTile);
    addChildComponent (midiInChannelField);
    addChildComponent (midiInCcField);
    addAndMakeVisible (viewport);
    addAndMakeVisible (addTile);

    // MIDI-In (Block G): Toggle bindet/loest den externen CC dieses
    // Control-Werts, die Felder committen die Bindung neu.
    midiInTile.onClick = [this]
    {
        if (currentControlId < 0)
            return;

        if (midiInTile.isActive())
        {
            midiInBindings.unbind (currentKey());
            midiInTile.setActive (false);
        }
        else
        {
            commitMidiInBinding();
        }
        refreshMidiInRow();
    };
    midiInChannelField.onValueCommitted = [this] (double) { if (midiInTile.isActive()) commitMidiInBinding(); };
    // Block L: Tooltip zeigt den Funktionsnamen live beim Swipe.
    midiInCcField.onValueChanged   = [this] (double v) { midiInCcField.setTooltip (CcNames::displayName ((int) v)); };
    midiInCcField.onValueCommitted = [this] (double)   { if (midiInTile.isActive()) commitMidiInBinding(); };

    // MIDI-Learn (User-Wunsch 11.07.): scharfschalten, der naechste
    // eingehende CC bindet -- onLearnCompleted aktualisiert die Felder.
    learnTile.onClick = [this]
    {
        if (currentControlId < 0)
            return;

        if (midiInBindings.isLearnArmed())
        {
            midiInBindings.cancelLearn();
            learnTile.setActive (false);
        }
        else
        {
            midiInBindings.armLearn (currentKey());
            learnTile.setActive (true);
        }
    };
    midiInBindings.onLearnCompleted = [this] (const grid::MacroControlKey&, int channel, int cc,
                                              bool /*isNote*/, const grid::ModifierSet&)
    {
        learnTile.setActive (false);
        midiInChannelField.setValue (channel, juce::dontSendNotification);
        midiInCcField.setValue (cc, juce::dontSendNotification);
        midiInTile.setActive (true);
        refreshMidiInRow();   // Tooltip inkl. M5-Shift-Ebene ("+ C1, D#1")
    };

    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, push::colours::text);
    titleLabel.setText (juce::String::fromUTF8 ("Control lang halten, um Ziele zuzuweisen"),
                        juce::dontSendNotification);

    axisXTile.onClick = [this]
    {
        currentAxis = 0;
        axisXTile.setActive (true);
        axisYTile.setActive (false);
        refreshMidiInRow();
        rebuildRows();
    };
    axisYTile.onClick = [this]
    {
        currentAxis = 1;
        axisXTile.setActive (false);
        axisYTile.setActive (true);
        refreshMidiInRow();
        rebuildRows();
    };

    addTile.onClick = [this] { addTargetSlot(); };

    viewport.setViewedComponent (&rowHost, false);
    viewport.setScrollBarsShown (true, false);
}

MacroPanel::~MacroPanel() = default;

grid::MacroControlKey MacroPanel::currentKey() const noexcept
{
    return { currentLayer, currentControlId, currentAxis };
}

void MacroPanel::showControl (int layer, int controlId, const juce::String& title, bool hasYAxis)
{
    currentLayer = layer;
    currentControlId = controlId;
    currentAxis = 0;
    currentHasYAxis = hasYAxis;
    currentTitle = title;

    titleLabel.setText (title, juce::dontSendNotification);
    axisXTile.setVisible (hasYAxis);
    axisYTile.setVisible (hasYAxis);
    axisXTile.setActive (true);
    axisYTile.setActive (false);

    // Standard: 1 Ziel-Slot sichtbar (Masterplan) -- leeren Slot anlegen.
    if (macroBindings.count (currentKey()) == 0)
        macroBindings.add (currentKey());

    selectedRow = 0;
    refreshMidiInRow();
    rebuildRows();
    resized();
}

void MacroPanel::refreshMidiInRow()
{
    const auto showRow = currentControlId >= 0;
    midiInTile.setVisible (showRow);
    learnTile.setVisible (showRow);
    midiInChannelField.setVisible (showRow);
    midiInCcField.setVisible (showRow);

    if (! showRow)
        return;

    if (const auto* binding = midiInBindings.bindingFor (currentKey()))
    {
        midiInTile.setActive (true);
        midiInChannelField.setValue (binding->channel, juce::dontSendNotification);
        midiInCcField.setValue (binding->cc, juce::dontSendNotification);

        // Block L / M4: Funktions- bzw. Notenname; M5: Shift-Ebene anhaengen.
        auto tooltip = binding->isNote
                           ? juce::MidiMessage::getMidiNoteName (binding->cc, true, true, 4)
                           : CcNames::displayName (binding->cc);
        if (! binding->modifiers.empty())
        {
            juce::StringArray names;
            for (const auto& m : binding->modifiers)
                names.add (juce::MidiMessage::getMidiNoteName (m.note, true, true, 4));
            tooltip << " + " << names.joinIntoString (", ");
        }
        midiInCcField.setTooltip (tooltip);
    }
    else
    {
        midiInTile.setActive (false);
    }
}

void MacroPanel::commitMidiInBinding()
{
    if (currentControlId < 0)
        return;

    // M4: eine bestehende Note-Bindung (per Learn erzeugt) behaelt ihren
    // Adressraum, wenn nur Kanal/Nummer per Feld editiert werden;
    // M5: Shift-Ebene und Suppress-Flag bleiben ebenfalls erhalten.
    const auto* existing = midiInBindings.bindingFor (currentKey());
    const auto isNote    = existing != nullptr && existing->isNote;
    auto modifiers       = existing != nullptr ? existing->modifiers : grid::ModifierSet {};
    const auto suppress  = existing != nullptr && existing->suppressWhileShift;

    midiInBindings.bind (currentKey(), (int) midiInChannelField.getValue(),
                         (int) midiInCcField.getValue(), isNote, std::move (modifiers), suppress);
    midiInTile.setActive (true);
}

void MacroPanel::rebuildRows()
{
    rows.clear();

    if (currentControlId < 0)
    {
        layoutRows();
        repaint();
        return;
    }

    const auto count = macroBindings.count (currentKey());
    selectedRow = juce::jlimit (0, juce::jmax (0, count - 1), selectedRow);

    for (int i = 0; i < count; ++i)
    {
        auto row = std::make_unique<TargetRow> (*this, i);
        row->setExpanded (i == selectedRow);
        rowHost.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    layoutRows();
    repaint();
}

void MacroPanel::layoutRows()
{
    const auto width = juce::jmax (0, viewport.getWidth() - viewport.getScrollBarThickness());

    int y = 0;
    for (auto& row : rows)
    {
        row->setBounds (0, y, width, row->preferredHeight());
        y += row->preferredHeight() + kRowGap;
    }

    rowHost.setSize (juce::jmax (1, width), juce::jmax (1, y));
}

void MacroPanel::selectRow (int index)
{
    if (index == selectedRow)
        return;

    selectedRow = index;

    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setExpanded ((int) i == selectedRow);

    layoutRows();
}

void MacroPanel::addTargetSlot()
{
    if (currentControlId < 0)
        return;

    if (macroBindings.add (currentKey()) == nullptr)
        return;   // 16 erreicht

    selectedRow = macroBindings.count (currentKey()) - 1;
    rebuildRows();
}

void MacroPanel::removeTargetSlot (int index)
{
    if (currentControlId < 0)
        return;

    macroBindings.remove (currentKey(), index);

    if (selectedRow >= index && selectedRow > 0)
        --selectedRow;

    rebuildRows();
}

void MacroPanel::tick()
{
    for (auto& row : rows)
        row->refreshLiveValues();
}

void MacroPanel::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::panel);
}

void MacroPanel::resized()
{
    auto area = getLocalBounds().reduced (kPaddingX, 6);

    auto header = area.removeFromTop (kHeaderHeight);
    if (currentHasYAxis)
    {
        axisYTile.setBounds (header.removeFromRight (32));
        header.removeFromRight (4);
        axisXTile.setBounds (header.removeFromRight (32));
        header.removeFromRight (4);
    }
    titleLabel.setBounds (header);
    area.removeFromTop (4);

    // MIDI-In-Zeile (Block G): Toggle + Learn + Ch/CC-Felder unter dem Titel.
    if (currentControlId >= 0)
    {
        auto midiInRow = area.removeFromTop (28);
        midiInTile.setBounds (midiInRow.removeFromLeft (64));
        midiInRow.removeFromLeft (4);
        learnTile.setBounds (midiInRow.removeFromLeft (52));
        midiInRow.removeFromLeft (6);
        const auto fieldWidth = (midiInRow.getWidth() - 4) / 2;
        midiInChannelField.setBounds (midiInRow.removeFromLeft (fieldWidth));
        midiInRow.removeFromLeft (4);
        midiInCcField.setBounds (midiInRow);
        area.removeFromTop (4);
    }

    addTile.setBounds (area.removeFromBottom (kAddTileHeight).reduced (0, 2));
    area.removeFromBottom (4);

    viewport.setBounds (area);
    layoutRows();
}

} // namespace conduit

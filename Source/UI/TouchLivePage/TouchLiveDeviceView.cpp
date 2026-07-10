#include "TouchLiveDeviceView.h"

#include <algorithm>
#include <cmath>

#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr int chipWidth   = 108;
    constexpr int chipGap     = 4;
    constexpr int nameHeight  = 18;
    constexpr int valueHeight = 16;

    [[nodiscard]] juce::String stringField (const juce::var& object, const char* field)
    {
        if (auto* dyn = object.getDynamicObject())
            return dyn->getProperty (field).toString();

        return {};
    }
}

//==============================================================================
TouchLiveDeviceView::TouchLiveDeviceView (TouchLiveClient& clientToUse,
                                          LiveSetModel& modelToUse,
                                          TouchLiveMeterBus& meterBusToUse,
                                          LiveSpectrumTap* spectrumTapToUse)
    : client (clientToUse),
      model (modelToUse),
      meterBus (meterBusToUse),
      spectrumTap (spectrumTapToUse),
      modelState (model.getState())
{
    modelState.addListener (this);

    trackChipViewport.setViewedComponent (&trackChipRow, false);
    trackChipViewport.setScrollBarsShown (false, false);
    addAndMakeVisible (trackChipViewport);

    deviceChipViewport.setViewedComponent (&deviceChipRow, false);
    deviceChipViewport.setScrollBarsShown (false, false);
    addAndMakeVisible (deviceChipViewport);

    for (int i = 0; i < parametersPerBank; ++i)
    {
        auto& strip = strips[(size_t) i];
        strip.slider.setRange (0.0, 1.0, 0.0);

        strip.slider.onValueChange = [this, i]
        {
            auto& s = strips[(size_t) i];

            if (s.parameterIndex < 0)
                return;

            s.valueText = valueTextFor (s, s.slider.getValue());
            sendParameter (s.parameterIndex, (float) s.slider.getValue());
            repaint (s.bounds);
        };

        addChildComponent (strip.slider);
    }

    onTile.setTooltip (juce::String::fromUTF8 ("Device an/aus"));
    onTile.onClick = [this] { toggleDeviceActive(); };
    addAndMakeVisible (onTile);

    bankPrevTile.onClick = [this] { setBank (bank - 1); };
    bankNextTile.onClick = [this] { setBank (bank + 1); };
    addAndMakeVisible (bankPrevTile);
    addAndMakeVisible (bankNextTile);

    viewTile.setTooltip (juce::String::fromUTF8 (
        "Bespoke-UI und Parameter-Bank umschalten"));
    viewTile.onClick = [this] { setBespokePreferred (! bespokePreferred); };
    addChildComponent (viewTile);

    // Spektrum hinter der EQ-Kurve (§10k): SPEC schaltet aus → Link → Input
    spectrumTile.setTooltip (juce::String::fromUTF8 (
        "Spektrum: aus → Link-Audio-Kanal → Hardware-Input"));
    spectrumTile.onClick = [this] { cycleSpectrumMode(); };
    addChildComponent (spectrumTile);

    averagingTile.setCaption ("AVG");
    averagingTile.setTooltip (juce::String::fromUTF8 (
        "Spektrum-Glättung (lokal — unabhängig von Lives Analyzer)"));
    averagingTile.onDragStart = [this]
    {
        if (spectrumTap != nullptr)
            averagingAtDragStart = spectrumTap->getAveraging();
    };
    averagingTile.onDrag = [this] (float totalDeltaY)
    {
        if (spectrumTap == nullptr)
            return;

        spectrumTap->setAveraging (averagingAtDragStart
                                   - (double) totalDeltaY / 200.0);
        refreshSpectrumTiles();
    };
    addChildComponent (averagingTile);

    rebuild();
    startTimerHz (30);   // GR-Meter-Refresh (roh, §5.1); ohne Sicht kostenlos
}

TouchLiveDeviceView::~TouchLiveDeviceView()
{
    stopTimer();
    modelState.removeListener (this);
    cancelPendingUpdate();
}

//==============================================================================
void TouchLiveDeviceView::timerCallback()
{
    if (! isShowing())
        return;

    refreshGainReductionNow();
}

void TouchLiveDeviceView::refreshGainReductionNow()
{
    const auto frame = meterBus.getFrameCounter();

    if (frame == lastMeterFrame)
        return;

    lastMeterFrame = frame;

    const auto level = selectedDevice.isEmpty()
                           ? 0.0f
                           : meterBus.getLevel (selectedDevice).left;

    if (level > 0.001f)
        grSeen = true;

    if (std::abs (level - grLevel) > 0.004f)
    {
        grLevel = level;
        repaint (grBounds.expanded (2));
    }
    else
    {
        grLevel = level;
    }
}

//==============================================================================
juce::ValueTree TouchLiveDeviceView::devicesDomain() const
{
    return model.getDomain ("devices");
}

juce::var TouchLiveDeviceView::chainOf (const juce::String& trackKey) const
{
    return devicesDomain().getProperty ("chain:" + trackKey);
}

//==============================================================================
void TouchLiveDeviceView::valueTreePropertyChanged (juce::ValueTree& tree,
                                                    const juce::Identifier& property)
{
    // Domain des Trees bestimmen (Item → Parent)
    auto domainName = tree.hasType (touchlive::id::domain)
                          ? tree.getProperty (touchlive::id::domainName).toString()
                          : tree.getParent().getProperty (touchlive::id::domainName).toString();

    if (domainName == "tracks")
    {
        triggerAsyncUpdate();   // Namen/Reihenfolge der Chips
        return;
    }

    if (domainName != "devices")
        return;

    const auto propName = property.toString();

    // Heiße Zeile: nur Werte des gewählten Devices → Slider direkt
    if (propName == "parvals:" + selectedDevice)
    {
        refreshValues();
        return;
    }

    if (propName.startsWith ("parvals:"))
        return;   // fremdes Device — kein Rebuild nötig

    // dev:-Item (is_active/name) des gewählten Devices → LED/Chip
    if (tree.hasType (touchlive::id::item))
    {
        const auto key = tree.getProperty (touchlive::id::itemKey).toString();

        if (key == "dev:" + selectedDevice)
        {
            deviceActive = (bool) tree.getProperty ("is_active", true);
            onTile.setActive (deviceActive);
        }
    }

    triggerAsyncUpdate();   // chain:/parmeta:/dev: → Struktur (coalesced)
}

void TouchLiveDeviceView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    juce::ignoreUnused (parent);
    triggerAsyncUpdate();
}

void TouchLiveDeviceView::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    triggerAsyncUpdate();
}

void TouchLiveDeviceView::handleAsyncUpdate()
{
    rebuild();
}

void TouchLiveDeviceView::flushPendingRebuild()
{
    handleUpdateNowIfNeeded();
}

//==============================================================================
void TouchLiveDeviceView::selectTrack (const juce::String& trackKey)
{
    if (selectedTrack == trackKey)
        return;

    selectedTrack = trackKey;
    selectedDevice.clear();   // Kette wechselt → erstes Device
    bank = 0;
    rebuild();
}

void TouchLiveDeviceView::selectDevice (const juce::String& deviceKey)
{
    if (selectedDevice == deviceKey)
        return;

    selectedDevice = deviceKey;
    bank = 0;
    grSeen = false;   // GR-Spalte gehört zum Device
    grLevel = 0.0f;
    bespokePreferred = true;   // frisches Device → bespoke zuerst (falls da)
    rebuild();
}

void TouchLiveDeviceView::setBank (int newBank)
{
    const auto clamped = juce::jlimit (0, juce::jmax (0, getBankCount() - 1), newBank);

    if (clamped == bank)
        return;

    bank = clamped;
    rebuildParameterBank();
    resized();
    repaint();
}

int TouchLiveDeviceView::getBankCount() const
{
    if (selectedDevice.isEmpty())
        return 0;

    const auto vals = devicesDomain().getProperty ("parvals:" + selectedDevice);
    const auto* array = vals.getArray();

    if (array == nullptr)
        return 0;

    const auto banked = array->size() - 1;   // Index 0 = Device On (ON-Tile)
    return banked <= 0 ? 0 : (banked + parametersPerBank - 1) / parametersPerBank;
}

//==============================================================================
void TouchLiveDeviceView::rebuild()
{
    // Track-Chips (Sortierung wie MixerView: Tracks → Returns → Master)
    auto tracksDomain = model.getDomain ("tracks");

    struct Entry
    {
        juce::String key, name, kind;
        int index = 0;
    };

    std::vector<Entry> regulars, returns, masters;

    for (int i = 0; i < tracksDomain.getNumChildren(); ++i)
    {
        const auto item = tracksDomain.getChild (i);
        Entry entry { item.getProperty (touchlive::id::itemKey).toString(),
                      item.getProperty ("name").toString(),
                      item.getProperty ("kind").toString(),
                      (int) item.getProperty ("index", 0) };

        if (entry.key.isEmpty())
            continue;

        if (entry.kind == "master")
            masters.push_back (std::move (entry));
        else if (entry.kind == "return")
            returns.push_back (std::move (entry));
        else
            regulars.push_back (std::move (entry));
    }

    const auto byIndex = [] (const Entry& a, const Entry& b) { return a.index < b.index; };
    std::sort (regulars.begin(), regulars.end(), byIndex);
    std::sort (returns.begin(), returns.end(), byIndex);

    std::vector<Entry> ordered = std::move (regulars);
    ordered.insert (ordered.end(), std::make_move_iterator (returns.begin()),
                    std::make_move_iterator (returns.end()));
    ordered.insert (ordered.end(), std::make_move_iterator (masters.begin()),
                    std::make_move_iterator (masters.end()));

    // Auswahl validieren: verschwundener Track → erster mit Geräten, sonst erster
    const auto trackKnown = std::any_of (ordered.begin(), ordered.end(),
                                         [this] (const Entry& e)
                                         { return e.key == selectedTrack; });

    if (! trackKnown)
    {
        selectedTrack.clear();

        for (const auto& entry : ordered)
        {
            const auto* chain = chainOf (entry.key).getArray();

            if (chain != nullptr && ! chain->isEmpty())
            {
                selectedTrack = entry.key;
                break;
            }
        }

        if (selectedTrack.isEmpty() && ! ordered.empty())
            selectedTrack = ordered.front().key;

        selectedDevice.clear();
        bank = 0;
    }

    trackChips.clear();

    for (const auto& entry : ordered)
    {
        auto chip = std::make_unique<push::TextTile> (entry.name);
        chip->setActive (entry.key == selectedTrack);

        const auto key = entry.key;
        chip->onClick = [this, key] { selectTrack (key); };

        trackChipRow.addAndMakeVisible (*chip);
        trackChips.push_back (std::move (chip));
    }

    // Device-Chips der Kette des gewählten Tracks
    juce::StringArray chainKeys;

    if (const auto* chain = chainOf (selectedTrack).getArray())
        for (const auto& dvid : *chain)
            chainKeys.add (dvid.toString());

    if (selectedDevice.isEmpty() || ! chainKeys.contains (selectedDevice))
    {
        selectedDevice = chainKeys.isEmpty() ? juce::String() : chainKeys[0];
        bank = 0;
    }

    deviceChips.clear();

    for (const auto& dvid : chainKeys)
    {
        auto item = model.findItem ("devices", "dev:" + dvid);
        auto label = item.isValid() ? item.getProperty ("name").toString() : dvid;

        auto chip = std::make_unique<push::TextTile> (label, push::colours::ledOrange);
        chip->setActive (dvid == selectedDevice);
        chip->onClick = [this, dvid] { selectDevice (dvid); };

        deviceChipRow.addAndMakeVisible (*chip);
        deviceChips.push_back (std::move (chip));
    }

    auto selectedItem = model.findItem ("devices", "dev:" + selectedDevice);
    deviceActive = ! selectedItem.isValid()
                 || (bool) selectedItem.getProperty ("is_active", true);
    onTile.setActive (deviceActive && selectedDevice.isNotEmpty());
    onTile.setEnabled (selectedDevice.isNotEmpty());

    // Bespoke-Registry (M5, §6b): Panel folgt der class_name des Devices
    const auto className = selectedItem.isValid()
                               ? selectedItem.getProperty ("class_name").toString()
                               : juce::String();

    if (className != bespokeClassName)
    {
        bespokeClassName = className;
        bespokePanel = createBespokePanel (className, client, spectrumTap);

        if (bespokePanel != nullptr)
            addChildComponent (*bespokePanel);
    }

    if (bespokePanel != nullptr && selectedDevice.isNotEmpty())
    {
        const auto domain = devicesDomain();
        bespokePanel->setDevice (selectedDevice,
                                 domain.getProperty ("parmeta:" + selectedDevice));
        bespokePanel->setValues (domain.getProperty ("parvals:" + selectedDevice));
    }

    updateBespokeVisibility();

    bank = juce::jlimit (0, juce::jmax (0, getBankCount() - 1), bank);
    rebuildParameterBank();
    resized();   // layoutet Chips, Footer-Kacheln und Bespoke-Panel
    repaint();
}

//==============================================================================
bool TouchLiveDeviceView::isBespokeActive() const noexcept
{
    return bespokePreferred && bespokePanel != nullptr && bespokePanel->isUsable();
}

void TouchLiveDeviceView::cycleSpectrumMode()
{
    if (spectrumTap == nullptr)
        return;

    using Mode = LiveSpectrumTap::SourceMode;

    switch (spectrumTap->getMode())
    {
        case Mode::off:        spectrumTap->setMode (Mode::linkAudio);  break;
        case Mode::linkAudio:  spectrumTap->setMode (Mode::audioInput); break;
        case Mode::audioInput:
        default:               spectrumTap->setMode (Mode::off);        break;
    }

    refreshSpectrumTiles();
}

void TouchLiveDeviceView::refreshSpectrumTiles()
{
    const auto active = spectrumTap != nullptr
                        && spectrumTap->getMode() != LiveSpectrumTap::SourceMode::off;

    spectrumTile.setActive (active);
    spectrumTile.setTooltip (spectrumTap != nullptr && active
                                 ? "Spektrum: " + spectrumTap->getSourceLabel()
                                 : juce::String::fromUTF8 (
                                       "Spektrum: aus → Link-Audio-Kanal → Hardware-Input"));

    if (spectrumTap != nullptr)
        averagingTile.setText (juce::String (
            juce::roundToInt (spectrumTap->getAveraging() * 100.0)) + " %");

    averagingTile.setVisible (active && isBespokeActive());
}

void TouchLiveDeviceView::setBespokePreferred (bool shouldPreferBespoke)
{
    if (bespokePreferred == shouldPreferBespoke)
        return;

    bespokePreferred = shouldPreferBespoke;
    updateBespokeVisibility();
    rebuildParameterBank();
    resized();
    repaint();
}

void TouchLiveDeviceView::updateBespokeVisibility()
{
    const auto bespoke = isBespokeActive();
    const auto available = bespokePanel != nullptr && bespokePanel->isUsable();

    if (bespokePanel != nullptr)
        bespokePanel->setVisible (bespoke);

    // Umschalter zeigt das ZIEL: BANK (aus bespoke raus) bzw. Geräte-Kürzel
    viewTile.setVisible (available);
    viewTile.setText (bespoke ? "BANK" : bespokeClassName.toUpperCase());
    viewTile.setActive (bespoke);

    bankPrevTile.setVisible (! bespoke);
    bankNextTile.setVisible (! bespoke);

    spectrumTile.setVisible (bespoke && spectrumTap != nullptr);
    refreshSpectrumTiles();
}

void TouchLiveDeviceView::rebuildParameterBank()
{
    const auto domain = devicesDomain();
    const auto metaVar = domain.getProperty ("parmeta:" + selectedDevice);
    const auto* meta = metaVar.getArray();

    for (int column = 0; column < parametersPerBank; ++column)
    {
        auto& strip = strips[(size_t) column];
        const auto parameterIndex = 1 + bank * parametersPerBank + column;

        // Bespoke aktiv → Bank komplett stumm (Umschalter rebuildet sie)
        if (meta == nullptr || parameterIndex >= meta->size() || isBespokeActive())
        {
            strip.parameterIndex = -1;
            strip.slider.setVisible (false);
            strip.name.clear();
            strip.valueText.clear();
            continue;
        }

        const auto& entry = meta->getReference (parameterIndex);
        strip.parameterIndex = parameterIndex;
        strip.name = stringField (entry, "name");
        strip.quantized = false;
        strip.items.clear();

        auto minValue = 0.0, maxValue = 1.0;

        if (auto* object = entry.getDynamicObject())
        {
            minValue = (double) object->getProperty ("min");
            maxValue = (double) object->getProperty ("max");
            strip.quantized = (bool) object->getProperty ("quant");

            if (const auto* items = object->getProperty ("items").getArray())
                for (const auto& item : *items)
                    strip.items.add (item.toString());
        }

        if (maxValue <= minValue)
            maxValue = minValue + 1.0;

        strip.slider.setRange (minValue, maxValue, strip.quantized ? 1.0 : 0.0);
        strip.slider.setVisible (true);
    }

    refreshValues();
}

void TouchLiveDeviceView::refreshValues()
{
    const auto vals = devicesDomain().getProperty ("parvals:" + selectedDevice);
    const auto* array = vals.getArray();

    if (bespokePanel != nullptr && selectedDevice.isNotEmpty())
        bespokePanel->setValues (vals);

    for (auto& strip : strips)
    {
        if (strip.parameterIndex < 0 || array == nullptr
            || strip.parameterIndex >= array->size())
            continue;

        const auto value = (double) array->getReference (strip.parameterIndex);
        strip.slider.setValue (value, juce::dontSendNotification);
        strip.valueText = valueTextFor (strip, value);
        repaint (strip.bounds);
    }
}

//==============================================================================
void TouchLiveDeviceView::sendParameter (int parameterIndex, float value)
{
    if (selectedDevice.isEmpty())
        return;

    client.noteTouchedParameter (TouchLiveClient::makeParameterKey (
        "devices", "parvals:" + selectedDevice));

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/device/set/parameter") };
    message.addString (selectedDevice);
    message.addInt32 (parameterIndex);
    message.addFloat32 (value);
    client.sendTouchValue (message);
}

void TouchLiveDeviceView::toggleDeviceActive()
{
    if (selectedDevice.isEmpty())
        return;

    // Optimistisch (Feel-Regel 5.1) + Suppression fürs Feld
    deviceActive = ! deviceActive;
    onTile.setActive (deviceActive);
    client.noteTouchedParameter (TouchLiveClient::makeParameterKey (
        "devices", "dev:" + selectedDevice, "is_active"));

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/device/set/is_active") };
    message.addString (selectedDevice);
    message.addInt32 (deviceActive ? 1 : 0);
    client.sendCommand (message);
}

//==============================================================================
juce::String TouchLiveDeviceView::valueTextFor (const ParameterStrip& strip, double value)
{
    if (strip.quantized && ! strip.items.isEmpty())
    {
        const auto index = juce::jlimit (0, strip.items.size() - 1,
                                         (int) std::lround (value));
        return strip.items[index];
    }

    return juce::String (value, 2);
}

juce::Slider* TouchLiveDeviceView::getParameterSlider (int column)
{
    if (column < 0 || column >= parametersPerBank)
        return nullptr;

    return &strips[(size_t) column].slider;
}

juce::String TouchLiveDeviceView::getParameterName (int column) const
{
    return column >= 0 && column < parametersPerBank
               ? strips[(size_t) column].name : juce::String();
}

juce::String TouchLiveDeviceView::getParameterValueText (int column) const
{
    return column >= 0 && column < parametersPerBank
               ? strips[(size_t) column].valueText : juce::String();
}

//==============================================================================
void TouchLiveDeviceView::resized()
{
    auto area = getLocalBounds().reduced (4);

    trackChipViewport.setBounds (area.removeFromTop (chipRowHeight));
    area.removeFromTop (2);
    deviceChipViewport.setBounds (area.removeFromTop (chipRowHeight));

    auto footer = area.removeFromBottom (footerHeight).reduced (0, 3);
    bankPrevTile.setBounds (footer.removeFromLeft (44));
    footer.removeFromLeft (4);
    bankNextTile.setBounds (footer.removeFromLeft (44));
    onTile.setBounds (footer.removeFromRight (56));
    footer.removeFromRight (4);
    viewTile.setBounds (footer.removeFromRight (64));
    footer.removeFromRight (4);
    spectrumTile.setBounds (footer.removeFromRight (56));
    footer.removeFromRight (4);
    averagingTile.setBounds (footer.removeFromRight (64));

    // Bank-Spalten; rechts die (immer reservierte) GR-Spalte
    auto bankArea = area.reduced (0, 4);
    grBounds = bankArea.removeFromRight (18).reduced (3, nameHeight);

    if (bespokePanel != nullptr)
        bespokePanel->setBounds (bankArea);

    const auto columnWidth = bankArea.getWidth() / parametersPerBank;

    for (int column = 0; column < parametersPerBank; ++column)
    {
        auto& strip = strips[(size_t) column];
        auto cell = bankArea.removeFromLeft (columnWidth);
        strip.bounds = cell;

        auto sliderArea = cell.reduced (6, 0);
        sliderArea.removeFromTop (nameHeight);
        sliderArea.removeFromBottom (valueHeight);
        strip.slider.setBounds (sliderArea);
    }

    layoutChips();
}

void TouchLiveDeviceView::layoutChips()
{
    const auto layoutRow = [] (juce::Component& row,
                               std::vector<std::unique_ptr<push::TextTile>>& chips,
                               int viewportHeight)
    {
        const auto width = juce::jmax (1, (int) chips.size() * (chipWidth + chipGap));
        row.setSize (width, viewportHeight);

        int x = 0;

        for (auto& chip : chips)
        {
            chip->setBounds (x, 0, chipWidth, viewportHeight);
            x += chipWidth + chipGap;
        }
    };

    layoutRow (trackChipRow, trackChips, trackChipViewport.getHeight());
    layoutRow (deviceChipRow, deviceChips, deviceChipViewport.getHeight());
}

void TouchLiveDeviceView::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    if (selectedDevice.isEmpty())
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (15.0f));
        g.drawText (juce::String::fromUTF8 ("Kein Device — Track mit Geräten wählen"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setFont (push::scaledFont (11.0f));

    for (const auto& strip : strips)
    {
        if (strip.parameterIndex < 0)
            continue;

        g.setColour (push::colours::text);
        g.drawFittedText (strip.name,
                          strip.bounds.withHeight (nameHeight).reduced (2, 0),
                          juce::Justification::centred, 1, 1.0f);

        g.setColour (push::colours::textDim);
        g.drawFittedText (strip.valueText,
                          strip.bounds.withTop (strip.bounds.getBottom() - valueHeight)
                                      .reduced (2, 0),
                          juce::Justification::centred, 1, 1.0f);
    }

    // Gain-Reduction-Spalte (Push-Vorbild): Reduktion füllt VON OBEN
    if (grSeen && ! grBounds.isEmpty())
    {
        const auto column = grBounds.toFloat();

        g.setColour (juce::Colour (0xff1b1e22));
        g.fillRoundedRectangle (column, 2.0f);

        if (grLevel > 0.001f)
        {
            g.setColour (push::colours::ledOrange.withAlpha (0.9f));
            g.fillRoundedRectangle (column.withHeight (column.getHeight()
                                                           * juce::jmin (1.0f, grLevel)),
                                    2.0f);
        }

        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (9.0f));
        g.drawText ("GR", grBounds.withY (grBounds.getBottom() + 2).withHeight (12),
                    juce::Justification::centred);
    }

    // Bank-Anzeige mittig im Footer (im Bespoke-Modus keine Bänke)
    const auto bankCount = isBespokeActive() ? 0 : getBankCount();

    if (bankCount > 1)
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (12.0f));
        g.drawText ("Bank " + juce::String (bank + 1) + "/" + juce::String (bankCount),
                    getLocalBounds().removeFromBottom (footerHeight),
                    juce::Justification::centred);
    }
}

} // namespace conduit

#include "TouchLiveEq8Panel.h"

#include <cmath>
#include <complex>

#include "UI/PushLookAndFeel.h"

namespace conduit
{

namespace
{
    constexpr double minHz = 10.0, maxHz = 22000.0;

    // Lives Anzeige-Farben (aus den Kalibrier-Screenshots gemessen)
    const juce::Colour curveColour  { 0xff03cfde };   // Lives Kurven-Cyan
    const juce::Colour handleColour { 0xfff0a53c };   // Lives Handle-Orange
    const juce::Colour handleOff    { 0xff6a6f74 };   // Band aus

    // Butterworth-8-Stufen-Qs (48-dB-Cuts, §10j)
    constexpr double butter8[4] = { 0.5098, 0.6013, 0.8999, 2.5629 };

    // Wire-Norm ↔ Hz / Q — gegen Lives Anzeige verifiziert (§10i)
    [[nodiscard]] double normToHz (double norm)
    {
        return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0, 1.0, norm));
    }

    [[nodiscard]] double normToQ (double norm)
    {
        return 0.1 * std::pow (180.0, juce::jlimit (0.0, 1.0, norm));
    }

    [[nodiscard]] juce::String hzText (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + " kHz"
                            : juce::String (hz, 0) + " Hz";
    }

    [[nodiscard]] juce::String stringField (const juce::var& object, const char* field)
    {
        if (auto* dyn = object.getDynamicObject())
            return dyn->getProperty (field).toString();

        return {};
    }
}

//==============================================================================
TouchLiveEq8Panel::TouchLiveEq8Panel (TouchLiveClient& clientToUse,
                                      LiveSpectrumTap* spectrumTapToUse)
    : client (clientToUse),
      spectrumTap (spectrumTapToUse)
{
    // Alles läuft über Gesten (§10j) — keine Footer-Bedienelemente.
    // Der Spektrum-Timer repainted nur bei neuen Analyse-Frames.
    if (spectrumTap != nullptr)
        startTimer (spectrumTimerId, 33);
}

TouchLiveEq8Panel::~TouchLiveEq8Panel()
{
    stopTimer (longPressTimerId);
    stopTimer (spectrumTimerId);
}

//==============================================================================
void TouchLiveEq8Panel::setDevice (const juce::String& deviceKeyToUse,
                                   const juce::var& parmeta)
{
    deviceKey = deviceKeyToUse;
    bands = {};
    mappedBandCount = 0;
    adaptiveQIndex = -1;
    outputIndex = -1;
    scaleIndex = -1;

    if (const auto* meta = parmeta.getArray())
    {
        for (int index = 0; index < meta->size(); ++index)
        {
            const auto& entry = meta->getReference (index);
            const auto name = stringField (entry, "name");

            if (name == "Adaptive Q")
            {
                adaptiveQIndex = index;
                continue;
            }

            if (name == "Output" || name == "Output Gain")
            {
                outputIndex = index;

                if (auto* object = entry.getDynamicObject())
                {
                    outputMin = (double) object->getProperty ("min");
                    outputMax = (double) object->getProperty ("max");
                }
                continue;
            }

            if (name == "Scale")
            {
                scaleIndex = index;

                if (auto* object = entry.getDynamicObject())
                {
                    scaleMin = (double) object->getProperty ("min");
                    scaleMax = (double) object->getProperty ("max");
                }
                continue;
            }

            for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex)
            {
                const auto prefix = juce::String (bandIndex + 1) + " ";
                auto& band = bands[(size_t) bandIndex];

                if (name == prefix + "Filter On A")
                    band.onIndex = index;
                else if (name == prefix + "Frequency A")
                    band.frequencyIndex = index;
                // Live 12 nennt den Q-Parameter "{n} Q A" (Feldtest
                // 10.07.2026) — "Resonance A" als Alias für ältere Versionen
                else if (name == prefix + "Q A" || name == prefix + "Resonance A")
                    band.resonanceIndex = index;
                else if (name == prefix + "Gain A")
                {
                    band.gainIndex = index;

                    if (auto* object = entry.getDynamicObject())
                    {
                        band.gainMin = (double) object->getProperty ("min");
                        band.gainMax = (double) object->getProperty ("max");

                        if (band.gainMax <= band.gainMin)
                        {
                            band.gainMin = -15.0;
                            band.gainMax = 15.0;
                        }
                    }
                }
                else if (name == prefix + "Filter Type A")
                {
                    band.typeIndex = index;
                    band.typeItems.clear();

                    if (auto* object = entry.getDynamicObject())
                        if (const auto* items = object->getProperty ("items").getArray())
                            for (const auto& item : *items)
                                band.typeItems.add (item.toString());
                }
            }
        }
    }

    for (const auto& band : bands)
        if (band.isMapped())
            ++mappedBandCount;

    if (! bands[(size_t) selectedBand].isMapped())
        for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex)
            if (bands[(size_t) bandIndex].isMapped())
            {
                selectedBand = bandIndex;
                break;
            }

    touches.clear();
    gesture = Gesture::none;
    primaryTouchIndex = pinchTouchIndex = -1;
    bandSelected = {};

    if (bands[(size_t) selectedBand].isMapped())
        bandSelected[(size_t) selectedBand] = true;

    curveDirty = true;
    repaint();
}

void TouchLiveEq8Panel::setValues (const juce::var& parvals)
{
    const auto* values = parvals.getArray();

    if (values == nullptr)
        return;

    const auto valueAt = [values] (int index, double fallback)
    {
        return index >= 0 && index < values->size()
                   ? (double) values->getReference (index) : fallback;
    };

    adaptiveQ = valueAt (adaptiveQIndex, adaptiveQ ? 1.0 : 0.0) > 0.5;

    if (gesture != Gesture::trimOutput)
        outputValue = juce::jlimit (outputMin, outputMax,
                                    valueAt (outputIndex, outputValue));

    if (gesture != Gesture::trimScale)
        scaleValue = juce::jlimit (scaleMin, scaleMax,
                                   valueAt (scaleIndex, scaleValue));

    for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex)
    {
        auto& band = bands[(size_t) bandIndex];

        if (! band.isMapped())
            continue;

        // Lokal-optimistisch (§5.1): berührte Bänder folgen NUR dem Finger
        const auto touched = ((gesture == Gesture::bandDrag
                                   || gesture == Gesture::pinchQ)
                              && bandIndex == selectedBand)
                          || (gesture == Gesture::moveSelection
                              && bandSelected[(size_t) bandIndex]);

        band.on = valueAt (band.onIndex, band.on ? 1.0 : 0.0) > 0.5;
        band.typeValue = juce::jlimit (0, juce::jmax (0, band.typeItems.size() - 1),
                                       (int) std::lround (valueAt (band.typeIndex,
                                                                   band.typeValue)));

        if (! touched)
        {
            band.frequencyNorm = juce::jlimit (0.0, 1.0,
                                               valueAt (band.frequencyIndex,
                                                        band.frequencyNorm));
            band.gainDb = juce::jlimit (band.gainMin, band.gainMax,
                                        valueAt (band.gainIndex, band.gainDb));
            band.resonanceNorm = juce::jlimit (0.0, 1.0,
                                               valueAt (band.resonanceIndex,
                                                        band.resonanceNorm));
        }
    }

    curveDirty = true;
    repaint();
}

//==============================================================================
void TouchLiveEq8Panel::sendParameter (int parameterIndex, float value, bool continuous)
{
    if (deviceKey.isEmpty() || parameterIndex < 0)
        return;

    client.noteTouchedParameter (TouchLiveClient::makeParameterKey (
        "devices", "parvals:" + deviceKey));

    juce::OSCMessage message { juce::OSCAddressPattern ("/live/device/set/parameter") };
    message.addString (deviceKey);
    message.addInt32 (parameterIndex);
    message.addFloat32 (value);

    if (continuous)
        client.sendTouchValue (message);
    else
        client.sendCommand (message);
}

void TouchLiveEq8Panel::setResonanceNorm (Band& band, double newNorm)
{
    band.resonanceNorm = juce::jlimit (0.0, 1.0, newNorm);
    sendParameter (band.resonanceIndex, (float) band.resonanceNorm, true);
    curveDirty = true;
    repaint();
}

//==============================================================================
int TouchLiveEq8Panel::bandAt (juce::Point<float> position) const
{
    int best = -1;
    auto bestDistance = touchRadius;

    for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex)
    {
        if (! bands[(size_t) bandIndex].isMapped())
            continue;

        const auto distance = bandPosition (bandIndex).getDistanceFrom (position);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = bandIndex;
        }
    }

    return best;
}

void TouchLiveEq8Panel::selectBand (int band)
{
    if (band < 0 || band >= bandCount || ! bands[(size_t) band].isMapped())
        return;

    selectedBand = band;
    bandSelected[(size_t) band] = true;
    repaint();
}

//==============================================================================
juce::Point<float> TouchLiveEq8Panel::touchCentroid() const
{
    juce::Point<float> sum;

    for (const auto& [index, touch] : touches)
    {
        juce::ignoreUnused (index);
        sum += touch.current;
    }

    return touches.empty() ? sum : sum / (float) touches.size();
}

int TouchLiveEq8Panel::heldBandTouchIndex() const
{
    return primaryTouchIndex;
}

void TouchLiveEq8Panel::beginFreeGesture()
{
    const auto count = (int) touches.size();
    gestureStartCentroid = touchCentroid();

    if (count == 2)
    {
        gesture = Gesture::moveSelection;

        for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex)
            selectionStart[(size_t) bandIndex] = { bands[(size_t) bandIndex].frequencyNorm,
                                                   bands[(size_t) bandIndex].gainDb };
    }
    else if (count == 3 && outputIndex >= 0)
    {
        gesture = Gesture::trimOutput;
        gestureStartValue = outputValue;
    }
    else if (count == 4 && scaleIndex >= 0)
    {
        gesture = Gesture::trimScale;
        gestureStartValue = scaleValue;
    }
    else
    {
        gesture = Gesture::none;   // 1 Finger frei / > 4: keine Geste
    }

    repaint();
}

void TouchLiveEq8Panel::touchDown (int touchIndex, juce::Point<float> position)
{
    touches[touchIndex] = { position, position, bandAt (position) };
    const auto hit = touches[touchIndex].bandHit;

    if (gesture == Gesture::idle)
        return;   // Restfinger nach Primär-Release: wirkungslos bis alle oben

    // Band-Familie: ein Finger hält bereits einen Punkt
    if (primaryTouchIndex >= 0)
    {
        if (hit >= 0)
        {
            // Punkt HALTEN + weiteren Punkt antippen = zur Auswahl hinzufügen
            bandSelected[(size_t) hit] = true;
            repaint();
        }
        else if (pinchTouchIndex < 0 && gesture == Gesture::bandDrag)
        {
            // freier zweiter Finger = Pinch-Q am aktiven Band
            // (nicht während des Typ-Selectors)
            stopTimer (longPressTimerId);
            pinchTouchIndex = touchIndex;
            pinchStartDistance = juce::jmax (1.0f, position.getDistanceFrom (
                touches[primaryTouchIndex].current));
            pinchStartResonance = bands[(size_t) selectedBand].resonanceNorm;
            gesture = Gesture::pinchQ;
        }

        return;
    }

    if (hit >= 0 && touches.size() == 1)
    {
        // Erster Finger auf einem Punkt → aktives Band; Drag greift erst
        // ab Bewegungsschwelle (RELATIV — der Punkt springt nie zum
        // Finger), Stillhalten öffnet den Typ-Selector (Long-Press)
        primaryTouchIndex = touchIndex;

        if (! bandSelected[(size_t) hit])
        {
            bandSelected = {};              // Einzel-Auswahl ersetzt die alte
            bandSelected[(size_t) hit] = true;
        }

        selectBand (hit);
        gesture = Gesture::bandDrag;
        dragMoved = false;
        dragStartFrequencyNorm = bands[(size_t) hit].frequencyNorm;
        dragStartGainDb = bands[(size_t) hit].gainDb;
        dragStartResonanceNorm = bands[(size_t) hit].resonanceNorm;
        startTimer (longPressTimerId, longPressMs);
        return;
    }

    // freie Familie (kein Punkt gehalten): Finger-Anzahl wählt die Geste
    beginFreeGesture();
}

//==============================================================================
void TouchLiveEq8Panel::timerCallback (int timerId)
{
    if (timerId == longPressTimerId)
    {
        stopTimer (longPressTimerId);
        triggerLongPress();
        return;
    }

    // Spektrum: nur repainten, wenn ein neuer Analyse-Frame vorliegt
    if (timerId == spectrumTimerId && spectrumTap != nullptr && isShowing())
    {
        const auto revisionNow = spectrumTap->getRevision();

        if (revisionNow != lastSpectrumRevision)
        {
            lastSpectrumRevision = revisionNow;
            repaint();
        }
    }
}

void TouchLiveEq8Panel::triggerLongPress()
{
    // Nur wenn der Primärfinger noch still und allein auf seinem Punkt liegt
    if (gesture != Gesture::bandDrag || dragMoved || touches.size() != 1
        || primaryTouchIndex < 0)
        return;

    const auto& band = bands[(size_t) selectedBand];

    if (! band.isMapped() || band.typeItems.isEmpty())
        return;

    gesture = Gesture::typeSelect;
    typeSelectorHover = band.typeValue;
    repaint();
}

juce::Rectangle<float> TouchLiveEq8Panel::typeSelectorBounds() const
{
    const auto& band = bands[(size_t) selectedBand];
    const auto rows = juce::jmax (1, band.typeItems.size());
    const auto rowHeight = 40.0f;
    const auto area = plotArea();

    auto bounds = juce::Rectangle<float> (76.0f, rowHeight * (float) rows)
                      .withCentre (bandPosition (selectedBand));

    // vollständig in den Plot schieben
    bounds.setX (juce::jlimit (area.getX() + 2.0f,
                               juce::jmax (area.getX() + 2.0f,
                                           area.getRight() - bounds.getWidth() - 2.0f),
                               bounds.getX()));
    bounds.setY (juce::jlimit (area.getY() + 2.0f,
                               juce::jmax (area.getY() + 2.0f,
                                           area.getBottom() - bounds.getHeight() - 2.0f),
                               bounds.getY()));
    return bounds;
}

void TouchLiveEq8Panel::commitTypeSelector()
{
    auto& band = bands[(size_t) selectedBand];

    if (band.isMapped() && typeSelectorHover >= 0
        && typeSelectorHover < band.typeItems.size()
        && typeSelectorHover != band.typeValue)
    {
        band.typeValue = typeSelectorHover;
        sendParameter (band.typeIndex, (float) band.typeValue, false);
        curveDirty = true;
    }

    typeSelectorHover = -1;
    repaint();
}

void TouchLiveEq8Panel::touchMove (int touchIndex, juce::Point<float> position)
{
    const auto it = touches.find (touchIndex);

    if (it == touches.end())
        return;

    it->second.current = position;

    switch (gesture)
    {
        case Gesture::bandDrag:
        {
            if (touchIndex != primaryTouchIndex)
                break;

            const auto delta = position - it->second.start;

            // Schwelle: kleine Zitterbewegungen starten keinen Drag
            // (und lassen dem Long-Press seine Ruhe)
            if (! dragMoved && delta.getDistanceFromOrigin() < dragThreshold)
                break;

            if (! dragMoved)
            {
                dragMoved = true;
                stopTimer (longPressTimerId);
            }

            dragActiveBandBy (delta);
            break;
        }

        case Gesture::typeSelect:
        {
            if (touchIndex != primaryTouchIndex)
                break;

            const auto& band = bands[(size_t) selectedBand];
            const auto bounds = typeSelectorBounds();
            const auto rows = juce::jmax (1, band.typeItems.size());
            const auto row = (int) std::floor ((position.y - bounds.getY())
                                               / (bounds.getHeight() / (float) rows));
            typeSelectorHover = juce::jlimit (0, band.typeItems.size() - 1, row);
            repaint();
            break;
        }

        case Gesture::pinchQ:
        {
            if (touchIndex != primaryTouchIndex && touchIndex != pinchTouchIndex)
                break;

            auto& band = bands[(size_t) selectedBand];

            if (! band.isMapped())
                break;

            const auto distance = touches[primaryTouchIndex].current
                                      .getDistanceFrom (touches[pinchTouchIndex].current);
            // Abstand verdoppeln ≈ +0.25 auf der Q-Norm (Faktor ~3.7 in Q)
            const auto octaves = std::log2 (juce::jmax (1.0f, distance)
                                            / pinchStartDistance);
            setResonanceNorm (band, pinchStartResonance + 0.25 * octaves);
            break;
        }

        case Gesture::moveSelection:
        {
            const auto area = plotArea();
            const auto delta = touchCentroid() - gestureStartCentroid;
            const auto freqDelta = area.getWidth() > 0.0f
                                       ? (double) (delta.x / area.getWidth()) : 0.0;
            const auto dbDelta = area.getHeight() > 0.0f
                                     ? -(double) delta.y / (area.getHeight() * 0.5)
                                           * plotDbRange
                                     : 0.0;

            for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex)
            {
                if (! bandSelected[(size_t) bandIndex])
                    continue;

                auto& band = bands[(size_t) bandIndex];

                if (! band.isMapped())
                    continue;

                const auto& start = selectionStart[(size_t) bandIndex];
                band.frequencyNorm = juce::jlimit (0.0, 1.0, start.first + freqDelta);
                sendParameter (band.frequencyIndex, (float) band.frequencyNorm, true);

                if (shapeHasGain (shapeOf (band)))
                {
                    band.gainDb = juce::jlimit (band.gainMin, band.gainMax,
                                                start.second + dbDelta);
                    sendParameter (band.gainIndex, (float) band.gainDb, true);
                }
            }

            curveDirty = true;
                    repaint();
            break;
        }

        case Gesture::trimOutput:
        {
            // fein: volle 200 px Bewegung ≈ 10 % der Range
            const auto dy = (double) (gestureStartCentroid.y - touchCentroid().y);
            outputValue = juce::jlimit (outputMin, outputMax,
                                        gestureStartValue
                                            + dy * (outputMax - outputMin) * 0.0005);
            sendParameter (outputIndex, (float) outputValue, true);
            repaint();
            break;
        }

        case Gesture::trimScale:
        {
            // gröber als der Output-Trim (User-Feedback 10.07.2026)
            const auto dy = (double) (gestureStartCentroid.y - touchCentroid().y);
            scaleValue = juce::jlimit (scaleMin, scaleMax,
                                       gestureStartValue
                                           + dy * (scaleMax - scaleMin) * 0.0015);
            sendParameter (scaleIndex, (float) scaleValue, true);
            curveDirty = true;   // Scale verformt die Kurve (§10j)
            repaint();
            break;
        }

        case Gesture::none:
        case Gesture::idle:
        default:
            break;
    }
}

void TouchLiveEq8Panel::touchUp (int touchIndex)
{
    touches.erase (touchIndex);

    if (touchIndex == primaryTouchIndex)
    {
        stopTimer (longPressTimerId);

        if (gesture == Gesture::typeSelect)
            commitTypeSelector();   // Loslassen übernimmt die Hover-Wahl

        // Haltender Finger weg → Gesten enden; Restfinger bleiben wirkungslos
        primaryTouchIndex = -1;
        pinchTouchIndex = -1;
        gesture = touches.empty() ? Gesture::none : Gesture::idle;
        return;
    }

    if (touchIndex == pinchTouchIndex)
    {
        pinchTouchIndex = -1;

        if (gesture == Gesture::pinchQ)
            gesture = Gesture::bandDrag;

        return;
    }

    if (gesture == Gesture::idle || primaryTouchIndex >= 0)
    {
        if (touches.empty())
            gesture = Gesture::none;

        return;
    }

    // freie Familie: Finger-Anzahl geändert → Geste mit neuer Baseline
    if (touches.empty())
        gesture = Gesture::none;
    else
        beginFreeGesture();
}

void TouchLiveEq8Panel::dragActiveBandBy (juce::Point<float> delta)
{
    auto& band = bands[(size_t) selectedBand];

    if (! band.isMapped())
        return;

    const auto area = plotArea();
    const auto freqDelta = area.getWidth() > 0.0f
                               ? (double) (delta.x / area.getWidth()) : 0.0;

    band.frequencyNorm = juce::jlimit (0.0, 1.0, dragStartFrequencyNorm + freqDelta);
    sendParameter (band.frequencyIndex, (float) band.frequencyNorm, true);

    if (shapeHasGain (shapeOf (band)))
    {
        const auto dbDelta = area.getHeight() > 0.0f
                                 ? -(double) delta.y / (area.getHeight() * 0.5)
                                       * plotDbRange
                                 : 0.0;
        band.gainDb = juce::jlimit (band.gainMin, band.gainMax,
                                    dragStartGainDb + dbDelta);
        sendParameter (band.gainIndex, (float) band.gainDb, true);
    }
    else
    {
        // Cut/Notch: vertikales Ziehen steuert den Q (Live-Verhalten,
        // User-Feedback 10.07.2026) — hoch = schärfer
        const auto qDelta = area.getHeight() > 0.0f
                                ? -(double) delta.y / area.getHeight()
                                : 0.0;
        band.resonanceNorm = juce::jlimit (0.0, 1.0,
                                           dragStartResonanceNorm + qDelta);
        sendParameter (band.resonanceIndex, (float) band.resonanceNorm, true);
    }

    curveDirty = true;
    repaint();
}

void TouchLiveEq8Panel::toggleBandOn (int band)
{
    if (band < 0 || band >= bandCount || ! bands[(size_t) band].isMapped())
        return;

    auto& state = bands[(size_t) band];
    state.on = ! state.on;
    sendParameter (state.onIndex, state.on ? 1.0f : 0.0f, false);

    curveDirty = true;
    repaint();
}


//==============================================================================
bool TouchLiveEq8Panel::isBandOn (int band) const
{
    return band >= 0 && band < bandCount && bands[(size_t) band].on;
}

bool TouchLiveEq8Panel::isBandSelected (int band) const
{
    return band >= 0 && band < bandCount && bandSelected[(size_t) band];
}

double TouchLiveEq8Panel::getResonanceNorm (int band) const
{
    return band >= 0 && band < bandCount ? bands[(size_t) band].resonanceNorm : 0.0;
}

double TouchLiveEq8Panel::getFrequencyNorm (int band) const
{
    return band >= 0 && band < bandCount ? bands[(size_t) band].frequencyNorm : 0.0;
}

double TouchLiveEq8Panel::getGainDb (int band) const
{
    return band >= 0 && band < bandCount ? bands[(size_t) band].gainDb : 0.0;
}

int TouchLiveEq8Panel::frequencyIndexOf (int band) const
{
    return band >= 0 && band < bandCount ? bands[(size_t) band].frequencyIndex : -1;
}

int TouchLiveEq8Panel::gainIndexOf (int band) const
{
    return band >= 0 && band < bandCount ? bands[(size_t) band].gainIndex : -1;
}

int TouchLiveEq8Panel::resonanceIndexOf (int band) const
{
    return band >= 0 && band < bandCount ? bands[(size_t) band].resonanceIndex : -1;
}

juce::Point<float> TouchLiveEq8Panel::bandPosition (int band) const
{
    if (band < 0 || band >= bandCount)
        return {};

    const auto& state = bands[(size_t) band];

    // Bell/Shelf: y = Gain. Cut/Notch: y zeigt den Q (Live-Verhalten,
    // Feedback-Runde 3) — Q-Norm 0.5 liegt auf der 0-Linie, und die
    // Skalierung folgt dem Y-Drag exakt 1:1 (beide spannen die volle
    // Plot-Höhe), der Punkt klebt also unter dem Finger.
    const auto gainForY = shapeHasGain (shapeOf (state))
                              ? state.gainDb
                              : (state.resonanceNorm - 0.5) * 2.0 * plotDbRange;

    return { xForNorm (state.frequencyNorm), yForDb (gainForY) };
}

//==============================================================================
TouchLiveEq8Panel::Shape TouchLiveEq8Panel::shapeForType (const juce::StringArray& typeItems,
                                                          int typeValue)
{
    // Semantik aus den items-Strings (nie feste Indizes annehmen)
    const auto label = typeValue >= 0 && typeValue < typeItems.size()
                           ? typeItems[typeValue]
                                 .toLowerCase().removeCharacters (" -/")
                           : juce::String();

    const auto has48 = label.contains ("48");

    if (label.contains ("lowcut") || label.contains ("highpass"))
        return has48 ? Shape::lowCut48 : Shape::lowCut12;

    if (label.contains ("highcut") || label.contains ("lowpass"))
        return has48 ? Shape::highCut48 : Shape::highCut12;

    if (label.contains ("lowshelf"))
        return Shape::lowShelf;

    if (label.contains ("highshelf"))
        return Shape::highShelf;

    if (label.contains ("notch"))
        return Shape::notch;

    if (label.contains ("bell") || label.contains ("peak"))
        return Shape::bell;

    // Fallback: Live-12-Reihenfolge (LC48 LC12 LS Bell Notch HS HC12 HC48)
    switch (typeValue)
    {
        case 0:  return Shape::lowCut48;
        case 1:  return Shape::lowCut12;
        case 2:  return Shape::lowShelf;
        case 4:  return Shape::notch;
        case 5:  return Shape::highShelf;
        case 6:  return Shape::highCut12;
        case 7:  return Shape::highCut48;
        default: return Shape::bell;
    }
}

TouchLiveEq8Panel::Shape TouchLiveEq8Panel::shapeOf (const Band& band) const
{
    return shapeForType (band.typeItems, band.typeValue);
}

bool TouchLiveEq8Panel::shapeHasGain (Shape shape) const noexcept
{
    return shape == Shape::bell || shape == Shape::lowShelf
        || shape == Shape::highShelf;
}

double TouchLiveEq8Panel::effectiveQ (Shape shape, double q, double gainDb) const
{
    // Kalibrier-Kampagne 10.07.2026 (§10j): Lives Anzeige-Q relativ zum
    // RBJ-Prototyp. Der Gain-Term ist Lives "Adaptive Q" (alle Messungen
    // mit On); Off lässt nach dieser Modellierung nur den Term entfallen.
    const auto g = adaptiveQ ? std::abs (gainDb) : 0.0;

    switch (shape)
    {
        case Shape::bell:
            return q * 0.5151 * std::pow (10.0, 0.04908 * g);

        case Shape::lowShelf:
        case Shape::highShelf:
        {
            const auto lq = std::log10 (juce::jmax (1.0e-3, q));
            return std::pow (10.0, -0.36661 + 0.45166 * lq
                                       + 0.04382 * g - 0.00685 * lq * g);
        }

        case Shape::lowCut12:
        case Shape::lowCut48:
        case Shape::highCut12:
        case Shape::highCut48:
        case Shape::notch:
        default:
            return q;   // Cuts/Notch: 1:1 (gemessen, kein Adaptive Q)
    }
}

double TouchLiveEq8Panel::shapeResponseDb (Shape shape, double hzRatio,
                                           double q, double gainDb)
{
    const auto a = std::pow (10.0, gainDb / 40.0);
    const auto sqrtA = std::sqrt (a);

    // Analoge Prototypen (s-Domain) — Lives Anzeige zeigt keine
    // Bilinear-Stauchung, und alle Fits liefen analog (§10j)
    const std::complex<double> s (0.0, hzRatio);
    const auto s2 = s * s;
    std::complex<double> h;

    switch (shape)
    {
        case Shape::bell:
            h = (s2 + s * (a / q) + 1.0) / (s2 + s / (a * q) + 1.0);
            break;

        case Shape::lowShelf:
            h = a * (s2 + s * (sqrtA / q) + a)
                    / (a * s2 + s * (sqrtA / q) + 1.0);
            break;

        case Shape::highShelf:
            h = a * (a * s2 + s * (sqrtA / q) + 1.0)
                    / (s2 + s * (sqrtA / q) + a);
            break;

        case Shape::notch:
            h = (s2 + 1.0) / (s2 + s / q + 1.0);
            break;

        case Shape::lowCut12:
            h = s2 / (s2 + s / q + 1.0);
            break;

        case Shape::highCut12:
            h = 1.0 / (s2 + s / q + 1.0);
            break;

        case Shape::lowCut48:
        case Shape::highCut48:
        default:
        {
            // Butterworth-8-Kaskade, alle Stufen-Qs skaliert (§10j)
            const auto lambda = juce::jmax (
                0.15, 1.097 + 0.611 * std::log10 (juce::jmax (1.0e-3, q)));
            h = 1.0;

            for (const auto stageQ : butter8)
            {
                const auto denom = s2 + s / (lambda * stageQ) + 1.0;
                h *= shape == Shape::lowCut48 ? s2 / denom : 1.0 / denom;
            }
            break;
        }
    }

    return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (h)));
}

double TouchLiveEq8Panel::responseDbAt (double hz) const
{
    double totalDb = 0.0;

    for (const auto& band : bands)
    {
        if (! band.isMapped() || ! band.on)
            continue;

        const auto shape = shapeOf (band);
        const auto f0 = normToHz (band.frequencyNorm);

        // Scale wirkt auf die BAND-GAINS (geclampt, Cuts/Notch unberührt)
        // — Messkampagne 10.07.2026 (§10j): Cut-Anteile blieben bei jedem
        // Scale identisch, große Scales sättigen am ±15-dB-Clamp,
        // negative Scales invertieren die Kurve
        const auto gain = shapeHasGain (shape)
                              ? juce::jlimit (band.gainMin, band.gainMax,
                                              band.gainDb * scaleValue)
                              : band.gainDb;
        const auto q = juce::jmax (0.025,
                                   effectiveQ (shape, normToQ (band.resonanceNorm),
                                               gain));

        totalDb += shapeResponseDb (shape, hz / f0, q, gain);
    }

    return totalDb;
}

void TouchLiveEq8Panel::rebuildCurve()
{
    curve.clear();
    const auto area = plotArea();

    // Auflösung an die Pixelbreite gekoppelt (User-Feedback: Spitzen
    // wurden mit fixen 96 Stützstellen eckig)
    const auto points = juce::jlimit (128, 1024, (int) (area.getWidth() / 2.0f));

    for (int i = 0; i < points; ++i)
    {
        const auto norm = (double) i / (points - 1);
        const auto db = juce::jlimit (-plotDbRange * 1.4, plotDbRange * 1.4,
                                      responseDbAt (normToHz (norm)));
        const juce::Point<float> point { xForNorm (norm), yForDb (db) };

        if (i == 0)
            curve.startNewSubPath (point);
        else
            curve.lineTo (point);
    }

    curveDirty = false;
}

//==============================================================================
juce::Rectangle<float> TouchLiveEq8Panel::plotArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 4.0f);
}

float TouchLiveEq8Panel::xForNorm (double norm) const
{
    const auto area = plotArea();
    return area.getX() + (float) juce::jlimit (0.0, 1.0, norm) * area.getWidth();
}

double TouchLiveEq8Panel::normForX (float x) const
{
    const auto area = plotArea();
    return area.getWidth() <= 0.0f
               ? 0.5 : juce::jlimit (0.0, 1.0, (double) ((x - area.getX())
                                                         / area.getWidth()));
}

float TouchLiveEq8Panel::yForDb (double db) const
{
    const auto area = plotArea();
    return area.getCentreY()
         - (float) (db / plotDbRange) * area.getHeight() * 0.5f;
}

double TouchLiveEq8Panel::dbForY (float y) const
{
    const auto area = plotArea();
    return area.getHeight() <= 0.0f
               ? 0.0 : (double) (area.getCentreY() - y)
                           / (area.getHeight() * 0.5) * plotDbRange;
}

//==============================================================================
void TouchLiveEq8Panel::resized()
{
    curveDirty = true;
}

//==============================================================================
void TouchLiveEq8Panel::mouseDown (const juce::MouseEvent& event)
{
    touchDown (event.source.getIndex(), event.position);
}

void TouchLiveEq8Panel::mouseDrag (const juce::MouseEvent& event)
{
    touchMove (event.source.getIndex(), event.position);
}

void TouchLiveEq8Panel::mouseUp (const juce::MouseEvent& event)
{
    touchUp (event.source.getIndex());
}

void TouchLiveEq8Panel::mouseDoubleClick (const juce::MouseEvent& event)
{
    toggleBandOn (bandAt (event.position));
}

//==============================================================================
void TouchLiveEq8Panel::paint (juce::Graphics& g)
{
    const auto area = plotArea();

    g.setColour (juce::Colour (0xff0e1112));   // Lives Plot-Hintergrund
    g.fillRoundedRectangle (area, 4.0f);

    if (! isUsable())
    {
        g.setColour (push::colours::textDim);
        g.setFont (push::scaledFont (13.0f));
        g.drawText (juce::String::fromUTF8 (
                        "EQ-Eight-Parameter nicht zuordenbar — BANK nutzen"),
                    area.toNearestInt(), juce::Justification::centred);
        return;
    }

    // Frequenz-Raster wie Live: Zwischenlinien je Dekade dim,
    // Dekaden (100/1k/10k) heller + Label
    const auto logSpan = std::log10 (maxHz / minHz);

    for (double decade = 10.0; decade < maxHz; decade *= 10.0)
    {
        for (int k = 1; k < 10; ++k)
        {
            const auto hz = decade * k;

            if (hz <= minHz || hz >= maxHz)
                continue;

            const auto x = xForNorm (std::log10 (hz / minHz) / logSpan);
            const auto major = k == 1;

            g.setColour (juce::Colours::white.withAlpha (major ? 0.14f : 0.05f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());

            if (major && hz >= 100.0)
            {
                g.setColour (push::colours::textDim.withAlpha (0.8f));
                g.setFont (push::scaledFont (10.0f));
                g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k"
                                         : juce::String (hz, 0),
                            (int) x + 4, (int) area.getBottom() - 15, 40, 12,
                            juce::Justification::left);
            }
        }
    }

    // Spektrum-Hintergrund (Link Audio / Input, §10k) — hinter der Kurve
    drawSpectrum (g, area);

    // dB-Raster ±12/±6, 0-Linie heller — Zahlen links wie Live
    g.setFont (push::scaledFont (10.0f));

    for (const auto db : { 12.0, 6.0, 0.0, -6.0, -12.0 })
    {
        const auto y = yForDb (db);
        g.setColour (juce::Colours::white.withAlpha (db == 0.0 ? 0.20f : 0.08f));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());

        g.setColour (push::colours::textDim.withAlpha (0.8f));
        g.drawText (juce::String ((int) db),
                    (int) area.getX() + 4, (int) y - 13, 30, 12,
                    juce::Justification::left);
    }

    if (curveDirty)
        rebuildCurve();

    g.setColour (curveColour);
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Band-Punkte im Ableton-Look: orange Kreise mit Nummer; SELEKTIERTE
    // gefüllt (dunkle Zahl), das aktive größer, ausgeschaltete grau
    for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex)
    {
        const auto& band = bands[(size_t) bandIndex];

        if (! band.isMapped())
            continue;

        const auto active = bandIndex == selectedBand;
        const auto selected = bandSelected[(size_t) bandIndex];
        const auto diameter = active ? selectedHandleDiameter : handleDiameter;
        const auto circle = juce::Rectangle<float> (diameter, diameter)
                                .withCentre (bandPosition (bandIndex));

        if (selected)
        {
            g.setColour (band.on ? handleColour : handleOff);
            g.fillEllipse (circle);
            g.setColour (juce::Colour (0xff141414));
        }
        else
        {
            const auto ring = band.on ? handleColour : handleOff;
            g.setColour (juce::Colour (0xff0e1112).withAlpha (0.55f));
            g.fillEllipse (circle);   // Punkt hebt sich von der Kurve ab
            g.setColour (ring);
            g.drawEllipse (circle.reduced (1.0f), 2.0f);
        }

        g.setFont (push::scaledFont (active ? 18.0f : 15.0f));
        g.drawText (juce::String (bandIndex + 1), circle.toNearestInt(),
                    juce::Justification::centred);
    }

    // Typ-Selector-Overlay (Long-Press): vertikale Symbolliste wie Lives
    // Dropdown — hoch/runter wischen wählt, Loslassen übernimmt
    if (gesture == Gesture::typeSelect)
    {
        const auto& band = bands[(size_t) selectedBand];
        const auto bounds = typeSelectorBounds();
        const auto rows = juce::jmax (1, band.typeItems.size());
        const auto rowHeight = bounds.getHeight() / (float) rows;

        g.setColour (juce::Colour (0xff23272b));
        g.fillRoundedRectangle (bounds.expanded (3.0f), 6.0f);

        for (int row = 0; row < rows; ++row)
        {
            const auto rowBounds = juce::Rectangle<float> (
                bounds.getX(), bounds.getY() + rowHeight * (float) row,
                bounds.getWidth(), rowHeight);
            const auto hovered = row == typeSelectorHover;

            if (hovered)
            {
                g.setColour (juce::Colour (0xffb9ecf5));   // Lives Hell-Cyan
                g.fillRect (rowBounds);
            }

            // Mini-Frequenzgang des Typs — dieselbe Mathematik wie die
            // grosse Kurve (shapeResponseDb), feste Anschauungswerte
            const auto shape = shapeForType (band.typeItems, row);
            const auto gainForIcon = shapeHasGain (shape) ? 9.0 : 0.0;
            const auto qForIcon = shape == Shape::notch ? 1.5 : 0.8;
            const auto icon = rowBounds.reduced (16.0f, 11.0f);

            juce::Path miniCurve;

            for (int i = 0; i <= 20; ++i)
            {
                const auto ratio = std::pow (10.0, -1.3 + 2.6 * i / 20.0);
                const auto db = juce::jlimit (-14.0, 14.0,
                                              shapeResponseDb (shape, ratio,
                                                               qForIcon, gainForIcon));
                const juce::Point<float> point {
                    icon.getX() + icon.getWidth() * (float) i / 20.0f,
                    icon.getCentreY() - (float) (db / 14.0) * icon.getHeight() * 0.5f };

                if (i == 0)
                    miniCurve.startNewSubPath (point);
                else
                    miniCurve.lineTo (point);
            }

            g.setColour (hovered ? juce::Colour (0xff10151a)
                                 : push::colours::text.withAlpha (0.85f));
            g.strokePath (miniCurve,
                          juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        }
    }

    // Gesten-Readout (3/4 Finger): Output-Gain bzw. Scale gross mittig oben
    if (gesture == Gesture::trimOutput || gesture == Gesture::trimScale)
    {
        const auto text = gesture == Gesture::trimOutput
            ? "Output  " + juce::String (outputValue >= 0.0 ? "+" : "")
                  + juce::String (outputValue, 2) + " dB"
            : "Scale  " + juce::String (scaleValue * 100.0, 0) + " %";

        g.setColour (push::colours::text);
        g.setFont (push::scaledFont (18.0f));
        g.drawText (text, area.toNearestInt().reduced (0, 10),
                    juce::Justification::centredTop);
    }

    // Readout des gewählten Bands (oben rechts, Live-Look) — inkl. Typ,
    // seit der Footer entfallen ist
    const auto& selected = bands[(size_t) selectedBand];

    if (selected.isMapped())
    {
        const auto separator = juce::String::fromUTF8 ("  \xC2\xB7  ");
        juce::String readout;

        if (selected.typeValue >= 0 && selected.typeValue < selected.typeItems.size())
            readout << selected.typeItems[selected.typeValue] << separator;

        readout << hzText (normToHz (selected.frequencyNorm));

        if (shapeHasGain (shapeOf (selected)))
            readout << separator
                    << (selected.gainDb >= 0.0 ? "+" : "")
                    << juce::String (selected.gainDb, 1) << " dB";

        readout << separator << "Q "
                << juce::String (normToQ (selected.resonanceNorm), 2);

        g.setColour (push::colours::text.withAlpha (0.9f));
        g.setFont (push::scaledFont (12.0f));
        g.drawText (readout, area.toNearestInt().reduced (8, 4),
                    juce::Justification::topRight);
    }
}

//==============================================================================
void TouchLiveEq8Panel::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (spectrumTap == nullptr || ! spectrumTap->isReceiving())
        return;

    const auto& magnitudes = spectrumTap->getMagnitudesDb();
    const auto logSpan = std::log (maxHz / minHz);

    // dB-Skala des Spektrums: 0 dBFS oben, −78 dB am Boden (Lives Look)
    constexpr float topDb = 0.0f, bottomDb = -78.0f;

    juce::Path spectrum;
    spectrum.startNewSubPath (area.getX(), area.getBottom());
    auto lastX = area.getX();

    for (int bin = 1; bin < LiveSpectrumTap::numBins; ++bin)
    {
        const auto hz = spectrumTap->binToHz (bin);

        if (hz < minHz)
            continue;

        if (hz > maxHz)
            break;

        const auto x = area.getX()
                     + (float) (std::log (hz / minHz) / logSpan) * area.getWidth();

        if (x - lastX < 1.0f && bin > 1)
            continue;   // mehrere Bins pro Pixel: erster gewinnt (log-x)

        lastX = x;
        const auto db = juce::jlimit (bottomDb, topDb,
                                      magnitudes[(size_t) bin]);
        const auto y = juce::jmap (db, bottomDb, topDb,
                                   area.getBottom(), area.getY());
        spectrum.lineTo (x, y);
    }

    spectrum.lineTo (area.getRight(), area.getBottom());
    spectrum.closeSubPath();

    g.setColour (juce::Colour (0xff3a3f43).withAlpha (0.55f));   // Lives Grau
    g.fillPath (spectrum);
}

//==============================================================================
std::unique_ptr<TouchLiveBespokePanel>
createBespokePanel (const juce::String& className, TouchLiveClient& client,
                    LiveSpectrumTap* spectrumTap)
{
    if (className == "Eq8")
        return std::make_unique<TouchLiveEq8Panel> (client, spectrumTap);

    return nullptr;   // kein bespoke UI → generische Bank (§6b)
}

} // namespace conduit

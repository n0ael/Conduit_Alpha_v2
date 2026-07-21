#include "GridKeyboardComponent.h"

#include <algorithm>

#include "Core/GridSensitivity.h"
#include "PushLookAndFeel.h"

namespace conduit
{

GridKeyboardComponent::GridKeyboardComponent (grid::GridVoiceEngine& engineToUse,
                                               const grid::PadGridLayout::Config& layoutConfig)
    : engine (engineToUse), layout (layoutConfig),
      baseYRangeNorm (layoutConfig.yRangeNorm),
      baseSemitonesPerPadWidth (layoutConfig.semitonesPerPadWidth),
      baseRingMinPx (grid::RingTouchModel::Config{}.minRadiusPx),
      baseRingSpanPx (grid::RingTouchModel::Config{}.maxRadiusPx - grid::RingTouchModel::Config{}.minRadiusPx),
      baseLowestNote (layoutConfig.lowestNote),
      rootPadColour (push::colours::padRoot)
{
    setWantsKeyboardFocus (false);
    setInterceptsMouseClicks (true, false);
}

void GridKeyboardComponent::setRootPadColour (juce::Colour newColour) noexcept
{
    if (newColour == rootPadColour)
        return;

    rootPadColour = newColour;
    repaint();
}

void GridKeyboardComponent::setPressureSensitivity (double sensitivity0to100) noexcept
{
    const auto scale = grid::sensitivityToRangeScale (sensitivity0to100);
    layout.setYRangeNorm (baseYRangeNorm * scale);
}

void GridKeyboardComponent::setSlideSensitivity (double sensitivity0to100) noexcept
{
    const auto scale = grid::sensitivityToRangeScale (sensitivity0to100);
    ring.setRadiusRange (baseRingMinPx, baseRingMinPx + baseRingSpanPx * scale);
}

void GridKeyboardComponent::setPitchBendMultiplier (float multiplier) noexcept
{
    layout.setSemitonesPerPadWidth (baseSemitonesPerPadWidth * multiplier);
}

void GridKeyboardComponent::octaveUp() noexcept
{
    if (octaveShiftValue >= kMaxOctaveShift)
        return;

    ++octaveShiftValue;
    layout.setLowestNote (baseLowestNote + octaveShiftValue * 12);
}

void GridKeyboardComponent::octaveDown() noexcept
{
    if (octaveShiftValue <= -kMaxOctaveShift)
        return;

    --octaveShiftValue;
    layout.setLowestNote (baseLowestNote + octaveShiftValue * 12);
}

juce::Point<float> GridKeyboardComponent::normalisedPosition (juce::Point<float> positionPx) const noexcept
{
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
        return {};

    return { positionPx.x / bounds.getWidth(), positionPx.y / bounds.getHeight() };
}

int GridKeyboardComponent::fingerIdFor (const juce::MouseEvent& event) noexcept
{
    // VoiceAllocator reserviert 0 als frei-Sentinel — Touch-Source-Indizes
    // sind 0-basiert, daher +1.
    return event.source.getIndex() + 1;
}

void GridKeyboardComponent::setScale (int newRootNote, ScaleType newScaleType)
{
    if (scaleRootNote == newRootNote && sessionScale == newScaleType)
        return;

    scaleRootNote = newRootNote;
    sessionScale  = newScaleType;
    repaint();
}

juce::Colour GridKeyboardComponent::padBaseColour (int midiNote, int rootNote,
                                                   ScaleType type) noexcept
{
    const auto semitoneAboveRoot = ((midiNote - rootNote) % 12 + 12) % 12;

    if (semitoneAboveRoot == 0)
        return push::colours::padRoot;

    return scale::isInScale (semitoneAboveRoot, type) ? push::colours::tile
                                                      : push::colours::padUnlit;
}

void GridKeyboardComponent::mouseDown (const juce::MouseEvent& event)
{
    touchDown (fingerIdFor (event), event.position);
}

void GridKeyboardComponent::mouseDrag (const juce::MouseEvent& event)
{
    // Beim Spielen mit der Maus folgt die „Sonne" dem Zeiger absolut →
    // Cursor ausblenden, damit der Pfeil den Leuchtpunkt nicht verdeckt.
    cursorHider.begin (*this, event, ui::DragCursorHider::Mode::absolute);
    touchMove (fingerIdFor (event), event.position);
}

void GridKeyboardComponent::mouseUp (const juce::MouseEvent& event)
{
    cursorHider.end();
    touchUp (fingerIdFor (event), event.position);
}

void GridKeyboardComponent::touchDown (int fingerId, juce::Point<float> position)
{
    // Grab (Block M/M2): Aufsetzen auf eine fingerlose Sonne (Drone ODER
    // latched) übernimmt die Stimme nahtlos — VOR der Ring-Zuordnung (der
    // Finger darf weder Sonne noch Mond werden) und ohne Neuanschlag;
    // Bend/Pressure wirken RELATIV ab dem Aufsetzpunkt. Ein kurzer Tap
    // (touchUp) beendet die einzelne Note. Der Sonnen-Check kommt VOR dem
    // Mond-Check: das 90-px-Mond-Griffband überdeckt bei kleinem Orbit die
    // 40-px-Sonnenzone vollständig — der kleinere, spezifischere Treffer
    // gewinnt (sonst wäre die Tap-zum-Beenden-Geste unerreichbar).
    if (auto* grabbedSun = grabbableSunAt (position); grabbedSun != nullptr)
    {
        const auto pos = normalisedPosition (position);

        FingerState state;
        state.currentNormX = pos.x;
        state.currentNormY = pos.y;
        state.anchorNormX  = grabbedSun->anchorNormX;
        state.grabbedSunId      = grabbedSun->voiceFingerId;
        state.grabNormX         = pos.x;
        state.grabNormY         = pos.y;
        state.grabBendSemitones = grabbedSun->lastBendSemitones;
        state.grabPressure01    = grabbedSun->lastPressure01;
        state.downPx     = position;
        state.downTimeMs = juce::Time::getMillisecondCounterHiRes();
        fingers[fingerId] = state;

        repaint();
        return;
    }

    // Mond-Reakquisition (Block M2): ein Finger auf dem eingefrorenen Mond
    // einer fingerlosen Sonne wird wieder deren Mond (Radius → Slide) —
    // VOR ring.onDown, sonst stiehlt eine lebende Sonne im 90-px-Umkreis
    // die Reakquisition und der Drone-/Latch-Mond bliebe unerreichbar.
    if (auto* moonSun = grabbableMoonAt (position); moonSun != nullptr)
    {
        const auto pos = normalisedPosition (position);

        FingerState state;
        state.currentNormX = pos.x;
        state.currentNormY = pos.y;
        state.grabbedMoonOfId = moonSun->voiceFingerId;
        state.downPx     = position;
        state.downTimeMs = juce::Time::getMillisecondCounterHiRes();
        fingers[fingerId] = state;

        // Der Mond dockt sofort an der Finger-Position an (RingTouchModel-
        // Semantik: nur ein aktiv bewegter Mond verändert die Umlaufbahn).
        moonSun->orbitOffset = position - moonSun->centre;
        moonSun->hasOrbit    = true;

        if (moonSun->hasVoice)
            engine.setSlide (moonSun->voiceFingerId,
                             slideFromOrbitRadius (moonSun->orbitOffset.getDistanceFromOrigin()));

        repaint();
        return;
    }

    const auto downResult = ring.onDown (static_cast<uint32_t> (fingerId), position);

    if (downResult.kind == grid::RingTouchModel::TouchKind::Ring)
    {
        const auto moveResult = ring.onMove (static_cast<uint32_t> (fingerId), position);
        if (moveResult.hasSlide)
            engine.setSlide (moveResult.owner, moveResult.slide01);

        repaint();
        return;
    }

    const auto pos = normalisedPosition (position);
    const auto pad = layout.padIndexAt (pos.x, pos.y);

    if (pad < 0)
        return;

    // In-Tune-Anker (Block B1): pad-Modus = Pad-Zentrum (der Finger bendet
    // ABSOLUT nach Distanz -- Re-Hit derselben Position ergibt denselben
    // Pitch), finger-Modus = Aufsetzpunkt (0 Bend beim Anschlag).
    const auto anchorX = inTuneLocation == grid::InTuneLocation::pad
                             ? layout.padCentreNormX (pad)
                             : pos.x;

    FingerState state;
    state.startNormX   = pos.x;
    state.startNormY   = pos.y;
    state.anchorNormX  = anchorX;
    state.currentNormX = pos.x;
    state.currentNormY = pos.y;
    state.lastBendSemitones = layout.pitchBendFromAnchor (anchorX, pos.x);
    state.lastPressure01    = layout.expressionFromDrag (pos.y, pos.y);
    fingers[fingerId] = state;

    // Block J1: Finger beim Magneten registrieren (Pad-Einheiten) — auch
    // bei ausgeschaltetem Gravity (billige Map-Pflege; getickt wird nur,
    // wenn das Settings-Toggle an ist).
    gravity.onDown (fingerId, pos.x * (float) layout.cols(), anchorX * (float) layout.cols());

    // MPE-Member-Kanäle sind gepoolt (VoiceAllocator) und behalten Bend/
    // Pressure vom LETZTEN Voice-Nutzer, bis die neue Note etwas Eigenes
    // sendet — ohne expliziten Startwert läse das Instrument beim ersten
    // Ton also einen zufälligen Alt-Zustand statt 0/Ist-Position (Fund
    // 06.07.2026: Pressure "mal 0, mal 50%" direkt nach dem Touch).
    // Im pad-Modus ist der Startwert des Bends die absolute Distanz zum
    // Pad-Zentrum (innerhalb der In-Tune-Zone = 0).
    engine.noteOn (static_cast<uint32_t> (fingerId), layout.noteForPad (pad), 100);
    engine.setPitchBend (static_cast<uint32_t> (fingerId), state.lastBendSemitones);
    engine.setPressure (static_cast<uint32_t> (fingerId), state.lastPressure01);
    repaint();
}

void GridKeyboardComponent::touchMove (int fingerId, juce::Point<float> position)
{
    // Grab (Block M/M2): der Finger steuert die gehaltene Stimme (Drone
    // oder latched) RELATIV — Bend/Pressure setzen an den eingefrorenen
    // Werten an (nie zur Fingerposition springen), die Sonne folgt dem
    // Finger. Bewusst KEIN setSlide hier: der Mond klebt starr an der
    // Sonne, sein Radius ändert sich nicht (RingTouchModel-Semantik) —
    // Slide ändert nur ein eigener Mond-Finger (Reakquisition unten).
    if (const auto grabIt = fingers.find (fingerId);
        grabIt != fingers.end() && grabIt->second.grabbedSunId != 0)
    {
        auto& state = grabIt->second;
        state.maxMovePx = juce::jmax (state.maxMovePx, position.getDistanceFrom (state.downPx));

        auto* sun = heldSunById (state.grabbedSunId);
        if (sun == nullptr)
            return;

        const auto pos = normalisedPosition (position);
        state.currentNormX = pos.x;
        state.currentNormY = pos.y;

        sun->centre = position;
        sun->lastBendSemitones = state.grabBendSemitones
            + layout.pitchBendFromAnchor (state.anchorNormX, pos.x)
            - layout.pitchBendFromAnchor (state.anchorNormX, state.grabNormX);
        sun->lastPressure01 = state.grabPressure01
            + layout.expressionFromDrag (state.grabNormY, pos.y) - 0.5f;

        if (sun->hasVoice)
        {
            engine.setPitchBend (sun->voiceFingerId, sun->lastBendSemitones);
            engine.setPressure (sun->voiceFingerId, sun->lastPressure01);
        }

        repaint();
        return;
    }

    // Mond-Grab (Block M2): der Finger definiert die Umlaufbahn der
    // gehaltenen Sonne neu — relativ zum AKTUELLEN Zentrum, funktioniert
    // damit auch parallel zu einem gleichzeitigen Sonnen-Grab.
    if (const auto moonIt = fingers.find (fingerId);
        moonIt != fingers.end() && moonIt->second.grabbedMoonOfId != 0)
    {
        auto& state = moonIt->second;
        state.maxMovePx = juce::jmax (state.maxMovePx, position.getDistanceFrom (state.downPx));

        auto* sun = heldSunById (state.grabbedMoonOfId);
        if (sun == nullptr)
            return;

        const auto pos = normalisedPosition (position);
        state.currentNormX = pos.x;
        state.currentNormY = pos.y;

        sun->orbitOffset = position - sun->centre;

        if (sun->hasVoice)
            engine.setSlide (sun->voiceFingerId,
                             slideFromOrbitRadius (sun->orbitOffset.getDistanceFromOrigin()));

        repaint();
        return;
    }

    const auto moveResult = ring.onMove (static_cast<uint32_t> (fingerId), position);

    if (moveResult.hasSlide)
    {
        engine.setSlide (moveResult.owner, moveResult.slide01);
        repaint();
        return;
    }

    const auto it = fingers.find (fingerId);
    if (it == fingers.end())
        return;

    const auto pos = normalisedPosition (position);
    it->second.currentNormX = pos.x;
    it->second.currentNormY = pos.y;

    // Block J1: Bewegung an den Magneten melden (Threshold-Messung); der
    // gesendete Bend nutzt den EFFEKTIVEN Rohwert — exakt der Touch-Wert,
    // solange der Magnet nicht greift (verlustfreier Bypass).
    gravity.onMove (fingerId, pos.x * (float) layout.cols());

    it->second.lastBendSemitones = layout.pitchBendFromAnchor (it->second.anchorNormX,
                                                               effectiveNormX (fingerId, pos.x));
    it->second.lastPressure01 = layout.expressionFromDrag (it->second.startNormY, pos.y);

    engine.setPitchBend (static_cast<uint32_t> (fingerId), it->second.lastBendSemitones);
    engine.setPressure (static_cast<uint32_t> (fingerId), it->second.lastPressure01);
    repaint();
}

void GridKeyboardComponent::touchUp (int fingerId, juce::Point<float> position)
{
    // Grab-Ende (Block M/M2): kurzer, unbewegter Tap beendet die EINZELNE
    // Note (Drone wie latched — der ChordMemory-Slot bleibt unberührt,
    // ChordMemory hält eine eigene Kopie); alles andere legt die Sonne an
    // neuer Position/Werten wieder ab.
    if (const auto grabIt = fingers.find (fingerId);
        grabIt != fingers.end() && grabIt->second.grabbedSunId != 0)
    {
        auto& state = grabIt->second;
        state.maxMovePx = juce::jmax (state.maxMovePx, position.getDistanceFrom (state.downPx));

        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - state.downTimeMs;
        const auto isTap = elapsedMs < (double) kGrabTapMaxMs
                           && state.maxMovePx < kGrabTapTolerancePx;
        const auto sunId = state.grabbedSunId;

        if (isTap)
        {
            if (const auto* sun = heldSunById (sunId); sun != nullptr && sun->hasVoice)
                engine.noteOff (sun->voiceFingerId, 0);

            // Entfernt die Sonne und löst evtl. fremde Mond-Grabs auf sie
            // (state kann dabei mit-erased werden — danach nicht mehr
            // anfassen, nur noch über sunId/fingerId arbeiten).
            removeHeldSunById (sunId);
        }
        else if (sunId >= kLatchedFingerBase && sunId < kDroneFingerBase)
        {
            // Latched Sonne nach einem Grab: die Anker der LINEAREN
            // moveLatchedBy-Kennlinie re-verankern (Inverse der linearen
            // Bend-/Pressure-Formeln), damit ein folgender Strip-Drag am
            // Release-Punkt sprungfrei fortsetzt.
            const auto bounds = getLocalBounds().toFloat();

            for (auto& sun : latched)
            {
                if (sun.voiceFingerId != sunId)
                    continue;

                if (bounds.getWidth() > 0.0f && bounds.getHeight() > 0.0f)
                {
                    sun.startNormX = sun.centre.x / bounds.getWidth()
                        - sun.lastBendSemitones
                              / (layout.semitonesPerPadWidth() * (float) layout.cols());
                    sun.startNormY = sun.centre.y / bounds.getHeight()
                        + (sun.lastPressure01 - 0.5f) * layout.yRangeNorm();
                }

                break;
            }
        }

        fingers.erase (fingerId);
        repaint();
        return;
    }

    // Mond-Grab-Ende (Block M2): der neue Orbit ist bereits in der Sonne
    // eingefroren — kein Tap-Kill auf dem Mond (Beenden bleibt exklusiv
    // die Sonnen-Geste).
    if (const auto moonIt = fingers.find (fingerId);
        moonIt != fingers.end() && moonIt->second.grabbedMoonOfId != 0)
    {
        fingers.erase (fingerId);
        repaint();
        return;
    }

    const auto upResult = ring.onUp (static_cast<uint32_t> (fingerId));

    if (upResult.wasRing)
    {
        // Mond-Orbit (User-Entscheidung 06.07.2026): kein Reset-Slide mehr --
        // der letzte gesendete CC74-Wert bleibt am Instrument stehen, der
        // Kreis bleibt sichtbar eingefroren (RingTouchModel::onUp), bis ein
        // neuer Touch die Umlaufbahn wieder aufgreift.
        repaint();
        return;
    }

    if (upResult.wasPrimary)
    {
        // Drone-Start (Block M, Abhebe-Reihenfolge „Sonne zuerst"): liegt
        // der Mond noch, dront die Note weiter — Stimme auf eine
        // synthetische Id umschlüsseln (Touch-Ids werden wiederverwendet),
        // Sonne + eingefrorener Orbit bleiben fingerlos auf dem Grid. Der
        // noch liegende Mond-Finger ist ab jetzt tot (RingTouchModel hat
        // ihn mit der Sonne vergessen; sein touchUp ist ein No-op).
        const auto it = fingers.find (fingerId);

        if (upResult.hadActiveMoon && it != fingers.end()
            && engine.rekeyVoice (static_cast<uint32_t> (fingerId), nextDroneFingerId))
        {
            DroneSun drone;
            drone.centre        = upResult.center;
            drone.orbitOffset   = upResult.ringOffset;
            drone.hasOrbit      = upResult.hasOrbit;
            drone.voiceFingerId = nextDroneFingerId++;
            drone.anchorNormX   = it->second.anchorNormX;
            drone.lastBendSemitones = it->second.lastBendSemitones;
            drone.lastPressure01    = it->second.lastPressure01;
            drones.push_back (drone);

            fingers.erase (fingerId);
            gravity.onUp (fingerId);
            repaint();
            return;
        }

        fingers.erase (fingerId);
        gravity.onUp (fingerId);
        engine.noteOff (static_cast<uint32_t> (upResult.primaryFinger), 0);
        repaint();
    }
}

//==============================================================================
// Hold/Drone + fingerlose Sonnen (Block M/M2)

GridKeyboardComponent::HeldSun* GridKeyboardComponent::heldSunById (uint32_t voiceFingerId) noexcept
{
    // Id-Räume sind disjunkt (Drones 0x20000+, latched 0x10000+) — die
    // Reihenfolge der Suche ist damit egal.
    for (auto& drone : drones)
        if (drone.voiceFingerId == voiceFingerId)
            return &drone;

    for (auto& sun : latched)
        if (sun.voiceFingerId == voiceFingerId)
            return &sun;

    return nullptr;
}

GridKeyboardComponent::HeldSun* GridKeyboardComponent::grabbableSunAt (juce::Point<float> positionPx) noexcept
{
    // Trefferzone = Sonnen-Zeichnradius (restRadiusPx ~ 40 px, über der
    // 44-px-Faustregel zusammen mit dem Zentrum-Snapping akzeptiert);
    // nächstgelegener Treffer über Drones UND latched. Bereits gegriffene
    // Sonnen bleiben ihrem Finger (Analogie ringFinger != 0).
    const auto isGrabbed = [this] (uint32_t id)
    {
        for (const auto& entry : fingers)
            if (entry.second.grabbedSunId == id)
                return true;
        return false;
    };

    HeldSun* best = nullptr;
    auto bestDistance = ring.restRadiusPx();

    const auto consider = [&] (HeldSun& sun)
    {
        if (isGrabbed (sun.voiceFingerId))
            return;

        const auto distance = sun.centre.getDistanceFrom (positionPx);
        if (distance <= bestDistance)
        {
            best = &sun;
            bestDistance = distance;
        }
    };

    for (auto& drone : drones)
        consider (drone);
    for (auto& sun : latched)
        consider (sun);

    return best;
}

GridKeyboardComponent::HeldSun* GridKeyboardComponent::grabbableMoonAt (juce::Point<float> positionPx) noexcept
{
    // Greifpunkt = eingefrorene Mond-Position (centre + orbitOffset),
    // Bandbreite wie RingTouchModel::onDown (grabRadiusPx). Bereits von
    // einem Finger gehaltene Monde bleiben diesem.
    const auto isHeld = [this] (uint32_t id)
    {
        for (const auto& entry : fingers)
            if (entry.second.grabbedMoonOfId == id)
                return true;
        return false;
    };

    HeldSun* best = nullptr;
    auto bestDistance = ring.grabRadiusPx();

    const auto consider = [&] (HeldSun& sun)
    {
        if (! sun.hasOrbit || isHeld (sun.voiceFingerId))
            return;

        const auto distance = (sun.centre + sun.orbitOffset).getDistanceFrom (positionPx);
        if (distance <= bestDistance)
        {
            best = &sun;
            bestDistance = distance;
        }
    };

    for (auto& drone : drones)
        consider (drone);
    for (auto& sun : latched)
        consider (sun);

    return best;
}

void GridKeyboardComponent::removeHeldSunById (uint32_t voiceFingerId)
{
    drones.erase (std::remove_if (drones.begin(), drones.end(),
                      [voiceFingerId] (const DroneSun& d)
                      { return d.voiceFingerId == voiceFingerId; }),
                  drones.end());
    latched.erase (std::remove_if (latched.begin(), latched.end(),
                       [voiceFingerId] (const LatchedSun& s)
                       { return s.voiceFingerId == voiceFingerId; }),
                   latched.end());

    // Fremde Finger, die diese Sonne bzw. ihren Mond hielten, werden inert.
    releaseStaleGrabs();
}

float GridKeyboardComponent::slideFromOrbitRadius (float radiusPx) const noexcept
{
    // Dieselbe Formel wie RingTouchModel::onMove — ungeklemmt, die
    // slideAxis klemmt am Ausgang (ADR 003).
    const auto range = ring.maxRadiusPx() - ring.restRadiusPx();
    return range > 0.0f ? (radiusPx - ring.restRadiusPx()) / range : 0.0f;
}

void GridKeyboardComponent::releaseStaleGrabs()
{
    // Nach Re-Recall vergibt latchConstellation IDENTISCHE Ids
    // (kLatchedFingerBase + i) neu — ohne diese Bereinigung steuerte ein
    // liegen gebliebener Grab-Finger die Sonne eines FREMDEN Akkords.
    for (auto it = fingers.begin(); it != fingers.end();)
    {
        const auto& state = it->second;
        const auto stale =
            (state.grabbedSunId    != 0 && heldSunById (state.grabbedSunId)    == nullptr)
         || (state.grabbedMoonOfId != 0 && heldSunById (state.grabbedMoonOfId) == nullptr);

        if (stale)
            it = fingers.erase (it);   // Finger wird inert — sein Move/Up
                                       // ist ab jetzt ein No-op
        else
            ++it;
    }
}

void GridKeyboardComponent::clearDrones()
{
    if (drones.empty())
        return;

    for (const auto& drone : drones)
        engine.noteOff (drone.voiceFingerId, 0);

    drones.clear();
    releaseStaleGrabs();
    repaint();
}

//==============================================================================
// Akkord-Speicher (Grid-Page v2, Feature 6)

void GridKeyboardComponent::latchConstellation (const std::vector<grid::StoredSun>& suns)
{
    clearLatched();   // vorherigen Akkord sauber per noteOff beenden

    const auto bounds = getLocalBounds().toFloat();
    const auto w = bounds.getWidth();
    const auto h = bounds.getHeight();
    if (w <= 0.0f || h <= 0.0f)
        return;

    latched.reserve (suns.size());

    for (size_t i = 0; i < suns.size(); ++i)
    {
        const auto& sun = suns[i];

        LatchedSun entry;
        entry.voiceFingerId = kLatchedFingerBase + (uint32_t) i;
        entry.centre        = { sun.x * w, sun.y * h };
        // ox/oy sind BEIDE über die Flächen-BREITE normalisiert
        // (ChordMemory-Konvention — der Orbit bleibt beim Rescale rund).
        entry.orbitOffset = { sun.ox * w, sun.oy * w };
        entry.hasOrbit    = sun.hasOrbit;
        entry.startNormX  = sun.x;
        entry.startNormY  = sun.y;

        const auto pad = layout.padIndexAt (sun.x, sun.y);
        entry.hasVoice = pad >= 0;

        if (pad >= 0)
        {
            entry.note = layout.noteForPad (pad);

            // Grab-Referenzen wie beim physischen Touch (Block M2): der
            // In-Tune-Anker folgt dem aktuellen Modus, Bend startet bei 0,
            // Pressure neutral am Aufsetzpunkt.
            entry.anchorNormX = inTuneLocation == grid::InTuneLocation::pad
                                    ? layout.padCentreNormX (pad)
                                    : sun.x;
            entry.lastBendSemitones = 0.0f;
            entry.lastPressure01    = layout.expressionFromDrag (sun.y, sun.y);

            // Startwerte wie beim physischen Touch (Fund 06.07.2026, s.
            // mouseDown): Bend 0, Pressure neutral am Aufsetzpunkt.
            engine.noteOn (entry.voiceFingerId, entry.note, 100);
            engine.setPitchBend (entry.voiceFingerId, 0.0f);
            engine.setPressure (entry.voiceFingerId, entry.lastPressure01);

            if (sun.hasOrbit)
                engine.setSlide (entry.voiceFingerId,
                                 slideFromOrbitRadius (entry.orbitOffset.getDistanceFromOrigin()));
        }

        // Pad ungültig (außerhalb): kein noteOn, Sonne trotzdem visuell
        // latchen (note bleibt -1).
        latched.push_back (entry);
    }

    repaint();
}

void GridKeyboardComponent::moveLatchedBy (float dxPx, float dyPx)
{
    if (latched.empty())
        return;

    const auto bounds = getLocalBounds().toFloat();
    const auto w = bounds.getWidth();
    const auto h = bounds.getHeight();

    for (auto& sun : latched)
    {
        // KEIN Clamping — Sonnen dürfen über den Rand (Komponentengrenzen
        // clippen die Zeichnung ohnehin).
        sun.centre += juce::Point<float> (dxPx, dyPx);

        if (sun.note >= 0 && w > 0.0f && h > 0.0f)
        {
            // X = Pitch, Y = Ausdruck — exakt wie ein Finger-Drag; der
            // Mond-Offset (Slide) bleibt starr. Die gesendeten Werte
            // reisen in last* mit — ein folgender Einzel-Grab (Block M2)
            // setzt sonst an veralteten eingefrorenen Werten an (Sprung).
            sun.lastBendSemitones = layout.pitchBendSemitones (sun.startNormX, sun.centre.x / w);
            sun.lastPressure01    = layout.expressionFromDrag (sun.startNormY, sun.centre.y / h);
            engine.setPitchBend (sun.voiceFingerId, sun.lastBendSemitones);
            engine.setPressure (sun.voiceFingerId, sun.lastPressure01);
        }
    }

    repaint();
}

void GridKeyboardComponent::clearLatched()
{
    if (latched.empty())
        return;

    for (const auto& sun : latched)
        if (sun.note >= 0)
            engine.noteOff (sun.voiceFingerId, 0);

    latched.clear();
    releaseStaleGrabs();
    repaint();
}

std::vector<grid::StoredSun> GridKeyboardComponent::constellationNormalized() const
{
    std::vector<grid::StoredSun> result;

    const auto bounds = getLocalBounds().toFloat();
    const auto w = bounds.getWidth();
    const auto h = bounds.getHeight();
    if (w <= 0.0f || h <= 0.0f)
        return result;

    const auto circles = ring.activeCircles();
    result.reserve (circles.size() + latched.size() + drones.size());

    for (const auto& circle : circles)
    {
        const auto offset = circle.orbitPos - circle.center;
        result.push_back ({ circle.center.x / w, circle.center.y / h,
                            offset.x / w, offset.y / w, circle.hasOrbit });
    }

    for (const auto& sun : latched)
        result.push_back ({ sun.centre.x / w, sun.centre.y / h,
                            sun.orbitOffset.x / w, sun.orbitOffset.y / w, sun.hasOrbit });

    // Drones (Block M) sind Teil der klingenden Konstellation — der
    // Akkord-Speicher nimmt sie mit auf.
    for (const auto& drone : drones)
        result.push_back ({ drone.centre.x / w, drone.centre.y / h,
                            drone.orbitOffset.x / w, drone.orbitOffset.y / w, drone.hasOrbit });

    return result;
}

//==============================================================================
void GridKeyboardComponent::paint (juce::Graphics& g)
{
    g.fillAll (push::colours::background);

    const auto bounds = getLocalBounds().toFloat();
    const auto cols = layout.cols();
    const auto rows = layout.rows();
    if (cols <= 0 || rows <= 0)
        return;

    const auto padWidth  = bounds.getWidth()  / (float) cols;
    const auto padHeight = bounds.getHeight() / (float) rows;
    constexpr float gap = 2.0f;

    // Sonnen/Monde EINMAL holen — Basis für den Pad-Glow und die
    // Kreis-Zeichnung darunter.
    const auto circles = ring.activeCircles();

    // Pad-Glow (Design-Mock Grid-Page v2): JEDES Pad hellt nach Distanz zur
    // nächstgelegenen Sonne auf (Maximum über alle Kreis-Zentren) — ersetzt
    // das frühere Nur-Ursprungs-Pad-Highlight; fadeDistance unverändert.
    const auto fadeDistance = juce::jmax (padWidth, padHeight);

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const auto padIndex = row * cols + col;
            const auto padBounds = juce::Rectangle<float> (padWidth * (float) col, padHeight * (float) row,
                                                            padWidth, padHeight)
                                        .reduced (gap * 0.5f);

            // Grundfarbe nach Session-Skala: Grundton > Skalenton > skalenfremd.
            const auto baseColour = padBaseColour (layout.noteForPad (padIndex),
                                                   scaleRootNote, sessionScale);

            auto glow = 0.0f;
            const auto padCentre = padBounds.getCentre();

            for (const auto& circle : circles)
            {
                const auto distance = circle.center.getDistanceFrom (padCentre);
                glow = juce::jmax (glow, juce::jlimit (0.0f, 1.0f, 1.0f - distance / fadeDistance));
            }

            // Latched Sonnen (Akkord-Speicher) glimmen wie Live-Finger —
            // Maximum über beide Mengen.
            for (const auto& sun : latched)
            {
                const auto distance = sun.centre.getDistanceFrom (padCentre);
                glow = juce::jmax (glow, juce::jlimit (0.0f, 1.0f, 1.0f - distance / fadeDistance));
            }

            // Drone-Sonnen (Block M) glimmen ebenfalls — sie klingen ja.
            for (const auto& drone : drones)
            {
                const auto distance = drone.centre.getDistanceFrom (padCentre);
                glow = juce::jmax (glow, juce::jlimit (0.0f, 1.0f, 1.0f - distance / fadeDistance));
            }

            // Noten-Echo (Block H4): extern gespielte Noten färben das Pad
            // in der Fokus-Track-Farbe (Stärke nach Velocity) — auf ALLEN
            // isomorphen Positionen der Note, unabhängig vom Finger-Glow.
            auto colour = baseColour;
            const auto padNote = layout.noteForPad (padIndex);

            // Block I: Root-Pads optional in der Fokus-Track-Farbe (wie
            // Push) — padBaseColour bleibt die pure Grau-Referenz.
            if (((padNote - scaleRootNote) % 12 + 12) % 12 == 0)
                colour = rootPadColour;
            if (padNote >= 0 && padNote < 128 && echoVelocity[(size_t) padNote] > 0.0f)
                colour = colour.interpolatedWith (
                    echoColour, 0.35f + 0.45f * echoVelocity[(size_t) padNote]);

            g.setColour (colour.interpolatedWith (push::colours::padGlow, glow));
            g.fillRoundedRectangle (padBounds, 4.0f);
        }
    }

    // Pitch-Schatten (Block J2): Abdunkelung mit weicher Kante (radialer
    // Alpha-Gradient statt Live-Blur, Muster fillSoftCircle) an der
    // X-Position des tatsächlich KLINGENDEN Pitch — wandert mit dem
    // Pad-Magneten (J1) zum Pad-Zentrum, während die Sonne am Finger
    // bleibt. UNTER den Sonnen gezeichnet.
    if (gravityEnabled())
    {
        const auto padWidthNorm = 1.0f / (float) cols;

        for (const auto& [fingerId, state] : fingers)
        {
            if (state.grabbedSunId != 0 || state.grabbedMoonOfId != 0)
                continue;   // Block M/M2: Grab-/Mond-Finger steuern fingerlose
                            // Sonnen, kein eigener Schatten

            const auto bend = layout.pitchBendFromAnchor (state.anchorNormX,
                                                          effectiveNormX (fingerId, state.currentNormX));
            const auto shadowNormX = state.anchorNormX
                                     + bend / layout.semitonesPerPadWidth() * padWidthNorm;

            const juce::Point<float> shadowCentre { shadowNormX * bounds.getWidth(),
                                                    state.currentNormY * bounds.getHeight() };
            const auto shadowRadius = padWidth * 0.55f;

            juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.45f),
                                         shadowCentre.x, shadowCentre.y,
                                         juce::Colours::black.withAlpha (0.0f),
                                         shadowCentre.x + shadowRadius, shadowCentre.y, true);
            g.setGradientFill (shadow);
            g.fillEllipse (juce::Rectangle<float> (shadowRadius * 2.0f, shadowRadius * 2.0f)
                               .withCentre (shadowCentre));
        }
    }

    const auto sunDiameter  = ring.restRadiusPx() * 2.0f;
    const auto moonDiameter = sunDiameter * 0.6f; // 60% der Sonnengröße (User 06.07.2026)

    // Weiche Kanten (Design-Mock Grid-Page v2): radialer Alpha-Gradient am
    // Rand statt Live-Blur — voll deckend bis (r - blur)/r, transparent bei
    // r. DPI-sauber, kein vorgerendertes Image nötig.
    const auto fillSoftCircle = [&g] (juce::Point<float> centre, float diameter, float edgeBlurPx)
    {
        const auto radius = diameter * 0.5f;
        juce::ColourGradient gradient (push::colours::ledWhite, centre.x, centre.y,
                                       push::colours::ledWhite.withAlpha (0.0f),
                                       centre.x + radius, centre.y, true);
        gradient.addColour (juce::jlimit (0.0, 1.0, (double) ((radius - edgeBlurPx) / radius)),
                            push::colours::ledWhite);
        g.setGradientFill (gradient);
        g.fillEllipse (juce::Rectangle<float> (diameter, diameter).withCentre (centre));
    };

    for (const auto& circle : circles)
    {
        // "Sonne": ausgemalter Punkt am (ggf. mitwandernden) Zentrum des
        // primären Fingers — fixer Zielpunkt für Pitch/Press unabhängig vom
        // Orbit-Radius (User 06.07.2026, wichtig sobald Hold dazukommt).
        fillSoftCircle (circle.center, sunDiameter, 2.5f);

        if (circle.hasOrbit)
        {
            // Umlaufbahn (Orbit): dünner Ring durch die aktuelle (ggf.
            // eingefrorene) Ring-Distanz — bleibt scharf —, Mond an der
            // Ring-Position mit weicher Kante.
            const auto orbitDiameter = circle.radiusPx * 2.0f;
            g.setColour (push::colours::ledWhite);
            g.drawEllipse (juce::Rectangle<float> (orbitDiameter, orbitDiameter).withCentre (circle.center), 1.5f);
            fillSoftCircle (circle.orbitPos, moonDiameter, 2.0f);
        }
    }

    // Latched Konstellation (Akkord-Speicher): identische Optik zu den
    // Live-Kreisen — Orbit nur bei hasOrbit (Design-Mock).
    for (const auto& sun : latched)
    {
        fillSoftCircle (sun.centre, sunDiameter, 2.5f);

        if (sun.hasOrbit)
        {
            const auto orbitDiameter = sun.orbitOffset.getDistanceFromOrigin() * 2.0f;
            g.setColour (push::colours::ledWhite);
            g.drawEllipse (juce::Rectangle<float> (orbitDiameter, orbitDiameter).withCentre (sun.centre), 1.5f);
            fillSoftCircle (sun.centre + sun.orbitOffset, moonDiameter, 2.0f);
        }
    }

    // Drone-Sonnen (Block M): Optik wie latched, PLUS ein feiner cyaner
    // Halo-Ring — markiert „fingerlos gehalten, tappbar zum Beenden".
    for (const auto& drone : drones)
    {
        fillSoftCircle (drone.centre, sunDiameter, 2.5f);

        g.setColour (push::colours::ledCyan.withAlpha (0.85f));
        const auto haloDiameter = sunDiameter + 8.0f;
        g.drawEllipse (juce::Rectangle<float> (haloDiameter, haloDiameter).withCentre (drone.centre), 1.5f);

        if (drone.hasOrbit)
        {
            const auto orbitDiameter = drone.orbitOffset.getDistanceFromOrigin() * 2.0f;
            g.setColour (push::colours::ledWhite);
            g.drawEllipse (juce::Rectangle<float> (orbitDiameter, orbitDiameter).withCentre (drone.centre), 1.5f);
            fillSoftCircle (drone.centre + drone.orbitOffset, moonDiameter, 2.0f);
        }
    }
}

//==============================================================================
// Grid-Gravity (Block J1) + Pitch-Schatten (J2)

float GridKeyboardComponent::effectiveNormX (int fingerId, float touchNormX) const noexcept
{
    if (! gravityEnabled())
        return touchNormX;

    const auto cols = (float) layout.cols();
    return gravity.effectiveX (fingerId, touchNormX * cols) / cols;
}

void GridKeyboardComponent::gravityTick()
{
    if (! gravityEnabled())
    {
        lastGravityTickMs = 0.0;
        return;
    }

    // Konfiguration live aus den Settings (Dev-Panel-Tuning wirkt sofort,
    // Muster TrackTabsStrip-Poll).
    grid::GridGravity::Config config;
    config.spring.force     = (float) panelSettings->getPhysicsForce();
    config.spring.mass      = (float) panelSettings->getPhysicsMass();
    config.spring.inertia01 = (float) panelSettings->getPhysicsInertia() / 100.0f;
    config.delayMs          = panelSettings->getGravityDelayMs();
    config.movementThresholdPadsPerSec = (float) panelSettings->getGravityThreshold();
    config.fadeMs           = panelSettings->getGravityFadeMs();
    gravity.setConfig (config);

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto dtSeconds = lastGravityTickMs > 0.0
                               ? (float) ((nowMs - lastGravityTickMs) / 1000.0)
                               : 0.0f;
    lastGravityTickMs = nowMs;

    if (dtSeconds <= 0.0f || ! gravity.hasFingers())
        return;

    if (! gravity.tick (juce::jmin (dtSeconds, 0.1f)))
        return;

    // Effektive Rohwerte in Bends übersetzen — derselbe Pfad wie touchMove,
    // nur mit dem Magnet-Anteil (VOR der Treppen-Kennlinie, Masterplan J1).
    // Drone-Grab-Finger (Block M) sind hier außen vor: ihre Bends laufen
    // relativ über die Drone-Stimme, der Magnet greift nicht.
    for (auto& [fingerId, state] : fingers)
    {
        if (state.grabbedSunId != 0 || state.grabbedMoonOfId != 0)
            continue;

        state.lastBendSemitones = layout.pitchBendFromAnchor (
            state.anchorNormX, effectiveNormX (fingerId, state.currentNormX));
        engine.setPitchBend (static_cast<uint32_t> (fingerId), state.lastBendSemitones);
    }

    repaint();
}

//==============================================================================
// Noten-Echo (Block H4)

void GridKeyboardComponent::setEchoColour (juce::Colour newColour) noexcept
{
    if (newColour == echoColour)
        return;

    echoColour = newColour;
    repaint();
}

void GridKeyboardComponent::echoNoteOn (int midiNote, float velocity01) noexcept
{
    if (midiNote < 0 || midiNote >= 128)
        return;

    echoVelocity[(size_t) midiNote] = juce::jlimit (0.0f, 1.0f, velocity01);
    repaint();
}

void GridKeyboardComponent::echoNoteOff (int midiNote) noexcept
{
    if (midiNote < 0 || midiNote >= 128)
        return;

    echoVelocity[(size_t) midiNote] = 0.0f;
    repaint();
}

void GridKeyboardComponent::clearEchoNotes() noexcept
{
    echoVelocity.fill (0.0f);
    repaint();
}

float GridKeyboardComponent::echoLevel (int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < 128 ? echoVelocity[(size_t) midiNote] : 0.0f;
}

} // namespace conduit

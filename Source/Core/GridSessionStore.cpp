#include "GridSessionStore.h"

namespace conduit::grid
{

namespace
{
    // Tree-/Property-Namen der Session-Datei — Schema-Änderungen NUR mit
    // kStateVersion-Erhöhung + Migration.
    const juce::Identifier kSession     ("GridSession");
    const juce::Identifier kVersion     ("version");

    const juce::Identifier kDiyControls ("DiyControls");
    const juce::Identifier kControl     ("Control");

    const juce::Identifier kChords      ("Chords");
    const juce::Identifier kChordSlot   ("Slot");
    const juce::Identifier kSun         ("Sun");

    const juce::Identifier kMidiIn      ("MidiIn");
    const juce::Identifier kMidiInBind  ("Binding");

    const juce::Identifier kMacros      ("Macros");
    const juce::Identifier kMacroControl ("MacroControl");
    const juce::Identifier kMacroSlot   ("MacroSlot");

    const juce::Identifier kAxes        ("Axes");
    const juce::Identifier kAxis        ("Axis");

    const juce::Identifier kCurve       ("Curve");
    const juce::Identifier kPoint       ("Point");

    [[nodiscard]] juce::ValueTree controlToState (const CcControl& control)
    {
        juce::ValueTree state (kControl);
        state.setProperty ("id", control.id, nullptr);
        // CcTool als int — Enum-Reihenfolge (none,fader,push,toggle,xy) ist
        // Serialisierungs-API, nie umsortieren.
        state.setProperty ("type", (int) control.type, nullptr);
        state.setProperty ("c0", control.c0, nullptr);
        state.setProperty ("r0", control.r0, nullptr);
        state.setProperty ("c1", control.c1, nullptr);
        state.setProperty ("r1", control.r1, nullptr);
        state.setProperty ("rx", control.rx, nullptr);
        state.setProperty ("ry", control.ry, nullptr);
        state.setProperty ("rw", control.rw, nullptr);
        state.setProperty ("rh", control.rh, nullptr);
        state.setProperty ("value", control.value, nullptr);
        state.setProperty ("on", control.on, nullptr);
        state.setProperty ("x", control.x, nullptr);
        state.setProperty ("y", control.y, nullptr);
        return state;
    }

    [[nodiscard]] CcControl controlFromState (const juce::ValueTree& state)
    {
        CcControl control;
        control.id   = (int) state.getProperty ("id", 0);
        control.type = static_cast<CcTool> ((int) state.getProperty ("type", 0));
        control.c0 = (int) state.getProperty ("c0", 0);
        control.r0 = (int) state.getProperty ("r0", 0);
        control.c1 = (int) state.getProperty ("c1", 0);
        control.r1 = (int) state.getProperty ("r1", 0);
        control.rx = (float) (double) state.getProperty ("rx", 0.0);
        control.ry = (float) (double) state.getProperty ("ry", 0.0);
        control.rw = (float) (double) state.getProperty ("rw", 0.0);
        control.rh = (float) (double) state.getProperty ("rh", 0.0);
        control.value = (float) (double) state.getProperty ("value", 0.75);
        control.on = (bool) state.getProperty ("on", false);
        control.x = (float) (double) state.getProperty ("x", 0.5);
        control.y = (float) (double) state.getProperty ("y", 0.5);
        return control;
    }

    [[nodiscard]] juce::ValueTree keyToState (const juce::Identifier& type,
                                              const MacroControlKey& key)
    {
        juce::ValueTree state (type);
        state.setProperty ("layer", key.layer, nullptr);
        state.setProperty ("controlId", key.controlId, nullptr);
        state.setProperty ("axis", key.axis, nullptr);
        return state;
    }

    [[nodiscard]] MacroControlKey keyFromState (const juce::ValueTree& state)
    {
        MacroControlKey key;
        key.layer     = (int) state.getProperty ("layer", 0);
        key.controlId = (int) state.getProperty ("controlId", 0);
        key.axis      = (int) state.getProperty ("axis", 0);
        return key;
    }
}

//==============================================================================
juce::ValueTree GridSessionStore::curveToState (const ResponseCurve& curve)
{
    juce::ValueTree state (kCurve);
    state.setProperty ("outMin", curve.getOutputMin(), nullptr);
    state.setProperty ("outMax", curve.getOutputMax(), nullptr);

    juce::StringArray curvatures;
    for (int i = 0; i < curve.numSegments(); ++i)
        curvatures.add (juce::String (curve.segmentCurvature (i), 6));
    state.setProperty ("curvatures", curvatures.joinIntoString (";"), nullptr);

    for (const auto& point : curve.points())
    {
        juce::ValueTree pointState (kPoint);
        pointState.setProperty ("x", point.x, nullptr);
        pointState.setProperty ("y", point.y, nullptr);
        state.appendChild (pointState, nullptr);
    }

    return state;
}

void GridSessionStore::curveFromState (const juce::ValueTree& state, ResponseCurve& curve)
{
    if (! state.hasType (kCurve))
        return;

    std::vector<ResponseCurve::Point> points;
    for (const auto& child : state)
        if (child.hasType (kPoint))
            points.push_back ({ (float) (double) child.getProperty ("x", 0.0),
                                (float) (double) child.getProperty ("y", 0.0) });

    if (points.size() >= 2)
        curve.setPoints (points);

    juce::StringArray curvatures;
    curvatures.addTokens (state.getProperty ("curvatures").toString(), ";", {});
    for (int i = 0; i < curvatures.size(); ++i)
        curve.setSegmentCurvature (i, curvatures[i].getFloatValue());

    curve.setOutputRange ((float) (double) state.getProperty ("outMin", 0.0),
                          (float) (double) state.getProperty ("outMax", 1.0));
}

//==============================================================================
juce::ValueTree GridSessionStore::capture (const Refs& refs)
{
    juce::ValueTree session (kSession);
    session.setProperty (kVersion, kStateVersion, nullptr);

    // DIY-Controls (System-Controls werden deterministisch neu gebaut —
    // buildXyFaderLayout vergibt stabile Ids, nichts zu speichern).
    juce::ValueTree controls (kDiyControls);
    for (const auto& control : refs.diyControls.controls())
        controls.appendChild (controlToState (control), nullptr);
    session.appendChild (controls, nullptr);

    // Akkord-Slots.
    juce::ValueTree chords (kChords);
    for (int slot = 0; slot < ChordMemory::kNumSlots; ++slot)
    {
        if (! refs.chords.isOccupied (slot))
            continue;

        juce::ValueTree slotState (kChordSlot);
        slotState.setProperty ("index", slot, nullptr);

        for (const auto& sun : refs.chords.slot (slot))
        {
            juce::ValueTree sunState (kSun);
            sunState.setProperty ("x", sun.x, nullptr);
            sunState.setProperty ("y", sun.y, nullptr);
            sunState.setProperty ("ox", sun.ox, nullptr);
            sunState.setProperty ("oy", sun.oy, nullptr);
            sunState.setProperty ("orbit", sun.hasOrbit, nullptr);
            slotState.appendChild (sunState, nullptr);
        }

        chords.appendChild (slotState, nullptr);
    }
    session.appendChild (chords, nullptr);

    // MIDI-In-Bindungen (nur die Adresse — Takeover/Glättung sind flüchtig).
    juce::ValueTree midiIn (kMidiIn);
    for (const auto& binding : refs.midiIn.all())
    {
        auto bindingState = keyToState (kMidiInBind, binding.key);
        bindingState.setProperty ("channel", binding.channel, nullptr);
        bindingState.setProperty ("cc", binding.cc, nullptr);
        midiIn.appendChild (bindingState, nullptr);
    }
    session.appendChild (midiIn, nullptr);

    // Macro-Bindings: pro Key die Slot-Liste; Ziele als opakes toState()-Tree
    // (ungültig = Slot ohne Ziel bzw. nicht persistierbarer Typ).
    juce::ValueTree macros (kMacros);
    for (const auto& key : refs.macros.allKeys())
    {
        auto controlState = keyToState (kMacroControl, key);

        for (int i = 0; i < refs.macros.count (key); ++i)
        {
            const auto* binding = refs.macros.get (key, i);
            if (binding == nullptr)
                continue;

            juce::ValueTree slotState (kMacroSlot);
            slotState.appendChild (curveToState (binding->curve), nullptr);

            if (binding->target != nullptr)
                if (auto targetState = binding->target->toState(); targetState.isValid())
                    slotState.appendChild (targetState, nullptr);

            controlState.appendChild (slotState, nullptr);
        }

        macros.appendChild (controlState, nullptr);
    }
    session.appendChild (macros, nullptr);

    // MPE-Achsen (Block B/C): Kurve + offsetBeyondMax je Achse (Index ==
    // GridVoiceEngine::Axis: Pressure 0, Slide 1, PitchBend 2).
    juce::ValueTree axes (kAxes);
    for (int i = 0; i < 3; ++i)
    {
        const auto axis = static_cast<GridVoiceEngine::Axis> (i);

        juce::ValueTree axisState (kAxis);
        axisState.setProperty ("index", i, nullptr);
        axisState.setProperty ("offsetBeyondMax", refs.engine.offsetBeyondMax (axis), nullptr);
        axisState.appendChild (curveToState (refs.engine.responseCurve (axis)), nullptr);
        axes.appendChild (axisState, nullptr);
    }
    session.appendChild (axes, nullptr);

    return session;
}

void GridSessionStore::apply (const juce::ValueTree& session, const Refs& refs,
                              const TargetFactory& makeTarget)
{
    if (! session.hasType (kSession))
        return;

    for (const auto& control : session.getChildWithName (kDiyControls))
        if (control.hasType (kControl))
            refs.diyControls.restore (controlFromState (control));

    for (const auto& slotState : session.getChildWithName (kChords))
    {
        if (! slotState.hasType (kChordSlot))
            continue;

        std::vector<StoredSun> suns;
        for (const auto& sunState : slotState)
            if (sunState.hasType (kSun))
                suns.push_back ({ (float) (double) sunState.getProperty ("x", 0.0),
                                  (float) (double) sunState.getProperty ("y", 0.0),
                                  (float) (double) sunState.getProperty ("ox", 0.0),
                                  (float) (double) sunState.getProperty ("oy", 0.0),
                                  (bool) sunState.getProperty ("orbit", false) });

        refs.chords.store ((int) slotState.getProperty ("index", -1), std::move (suns));
    }

    for (const auto& bindingState : session.getChildWithName (kMidiIn))
        if (bindingState.hasType (kMidiInBind))
            refs.midiIn.bind (keyFromState (bindingState),
                              (int) bindingState.getProperty ("channel", 1),
                              (int) bindingState.getProperty ("cc", 1));

    for (const auto& controlState : session.getChildWithName (kMacros))
    {
        if (! controlState.hasType (kMacroControl))
            continue;

        const auto key = keyFromState (controlState);

        for (const auto& slotState : controlState)
        {
            if (! slotState.hasType (kMacroSlot))
                continue;

            auto* binding = refs.macros.add (key);
            if (binding == nullptr)
                break;   // kMaxTargetsPerControl erreicht

            curveFromState (slotState.getChildWithName (kCurve), binding->curve);

            // Ziel: erstes Kind, das keine Kurve ist — Typ entscheidet die
            // Factory (unbekannt/nullptr = Slot bleibt ohne Ziel).
            if (makeTarget != nullptr)
            {
                for (const auto& child : slotState)
                {
                    if (child.hasType (kCurve))
                        continue;

                    binding->target = makeTarget (child);
                    break;
                }
            }
        }
    }

    for (const auto& axisState : session.getChildWithName (kAxes))
    {
        if (! axisState.hasType (kAxis))
            continue;

        const auto index = (int) axisState.getProperty ("index", -1);
        if (index < 0 || index > 2)
            continue;

        const auto axis = static_cast<GridVoiceEngine::Axis> (index);
        refs.engine.setOffsetBeyondMax (axis, (bool) axisState.getProperty ("offsetBeyondMax", false));
        curveFromState (axisState.getChildWithName (kCurve), refs.engine.responseCurve (axis));
    }
}

//==============================================================================
bool GridSessionStore::saveToFile (const juce::File& file, const juce::ValueTree& session)
{
    if (const auto xml = session.createXml())
    {
        file.getParentDirectory().createDirectory();
        return xml->writeTo (file);
    }

    return false;
}

juce::ValueTree GridSessionStore::loadFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};

    if (const auto xml = juce::parseXML (file))
        return juce::ValueTree::fromXml (*xml);

    return {};
}

} // namespace conduit::grid

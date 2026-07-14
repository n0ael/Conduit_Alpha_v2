#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/GridSessionStore.h"
#include "FakeVoiceSink.h"
#include "TouchLive/AbletonParamTarget.h"
#include "TouchLive/LiveTargetResolver.h"

namespace grid = conduit::grid;
using Catch::Approx;

namespace
{

/** Sammelnder MIDI-Ausgang (Muster FakeMidiTarget). */
struct NullMidiOutput final : grid::IMidiOutputTarget
{
    void send (const juce::MidiMessage&) override {}
};

/** Opakes Test-Ziel: prüft, dass der Store toState()-Trees unangetastet
    durchreicht und die Factory den Rückweg baut. */
struct FakeStateTarget final : grid::MacroTarget
{
    explicit FakeStateTarget (juce::String payloadToUse) : payload (std::move (payloadToUse)) {}

    void sendValue (float) override {}
    [[nodiscard]] juce::String describe() const override { return payload; }

    [[nodiscard]] juce::ValueTree toState() const override
    {
        juce::ValueTree state ("FakeTarget");
        state.setProperty ("payload", payload, nullptr);
        return state;
    }

    juce::String payload;
};

/** Frischer Satz Session-Objekte für capture/apply-Roundtrips. */
struct SessionObjects
{
    grid::CcControlModel diyControls;
    grid::ChordMemory chords;
    grid::MidiInBindings midiIn;
    grid::MacroBindings macros;
    grid::FakeVoiceSink sink;
    grid::GridVoiceEngine engine { sink };

    [[nodiscard]] grid::GridSessionStore::Refs refs()
    {
        return { diyControls, chords, midiIn, macros, engine };
    }
};

} // namespace

//==============================================================================
TEST_CASE ("GridSessionStore: DIY-Controls Roundtrip erhaelt Ids (auch mit Luecke)", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    SessionObjects source;

    const auto id1 = source.diyControls.addControl (grid::CcTool::fader, 0, 0, 0, 1);
    const auto id2 = source.diyControls.addControl (grid::CcTool::xy, 2, 0, 3, 1);
    const auto id3 = source.diyControls.addControl (grid::CcTool::toggle, 4, 0, 4, 0);
    source.diyControls.remove (id2);   // Id-Luecke!

    if (auto* fader = source.diyControls.find (id1))
    {
        fader->value = 0.25f;
        fader->rx = 0.1f; fader->ry = 0.2f; fader->rw = 0.3f; fader->rh = 0.4f;
    }

    const auto session = grid::GridSessionStore::capture (source.refs());

    SessionObjects loaded;
    grid::GridSessionStore::apply (session, loaded.refs(), nullptr);

    REQUIRE (loaded.diyControls.controls().size() == 2);
    const auto* fader = loaded.diyControls.find (id1);
    REQUIRE (fader != nullptr);
    REQUIRE (fader->type == grid::CcTool::fader);
    REQUIRE (fader->value == Approx (0.25f));
    REQUIRE (fader->rx == Approx (0.1f));
    REQUIRE (fader->rh == Approx (0.4f));
    REQUIRE (loaded.diyControls.find (id2) == nullptr);
    REQUIRE (loaded.diyControls.find (id3) != nullptr);

    // Die Id-Vergabe zaehlt oberhalb der hoechsten geladenen Id weiter.
    REQUIRE (loaded.diyControls.addControl (grid::CcTool::push, 0, 0, 0, 0) == id3 + 1);
}

TEST_CASE ("GridSessionStore: Akkord-Slots Roundtrip", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    SessionObjects source;

    REQUIRE (source.chords.store (2, { { 0.25f, 0.5f, 0.05f, 0.0f, true },
                                       { 0.75f, 0.5f, 0.0f, 0.0f, false } }));
    REQUIRE (source.chords.store (7, { { 0.1f, 0.9f, 0.0f, 0.0f, false } }));

    const auto session = grid::GridSessionStore::capture (source.refs());

    SessionObjects loaded;
    grid::GridSessionStore::apply (session, loaded.refs(), nullptr);

    REQUIRE (loaded.chords.isOccupied (2));
    REQUIRE_FALSE (loaded.chords.isOccupied (0));
    REQUIRE (loaded.chords.isOccupied (7));

    const auto& slot2 = loaded.chords.slot (2);
    REQUIRE (slot2.size() == 2);
    REQUIRE (slot2[0].x == Approx (0.25f));
    REQUIRE (slot2[0].ox == Approx (0.05f));
    REQUIRE (slot2[0].hasOrbit);
    REQUIRE_FALSE (slot2[1].hasOrbit);
}

TEST_CASE ("GridSessionStore: MIDI-In-Bindungen Roundtrip", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    SessionObjects source;

    source.midiIn.bind ({ grid::MacroControlKey::system, 3, 0 }, 5, 21);
    source.midiIn.bind ({ grid::MacroControlKey::diy, 1, 1 }, 1, 74);

    // M5: Shift-Ebene (Modifier-Set) + Suppress-Flag reisen mit.
    source.midiIn.bind ({ grid::MacroControlKey::diy, 2, 0 }, 1, 74, false,
                        { { 1, 36 }, { 2, 40 } }, true);

    const auto session = grid::GridSessionStore::capture (source.refs());

    SessionObjects loaded;
    grid::GridSessionStore::apply (session, loaded.refs(), nullptr);

    REQUIRE (loaded.midiIn.count() == 3);
    const auto* binding = loaded.midiIn.bindingFor ({ grid::MacroControlKey::system, 3, 0 });
    REQUIRE (binding != nullptr);
    REQUIRE (binding->channel == 5);
    REQUIRE (binding->cc == 21);
    REQUIRE (binding->modifiers.empty());
    REQUIRE_FALSE (binding->suppressWhileShift);

    const auto* shifted = loaded.midiIn.bindingFor ({ grid::MacroControlKey::diy, 2, 0 });
    REQUIRE (shifted != nullptr);
    REQUIRE (shifted->cc == 74);
    REQUIRE (shifted->modifiers == grid::ModifierSet { { 1, 36 }, { 2, 40 } });
    REQUIRE (shifted->suppressWhileShift);
}

TEST_CASE ("GridSessionStore: Macro-Slots Roundtrip (Kurve + opakes Ziel)", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    SessionObjects source;

    const grid::MacroControlKey key { grid::MacroControlKey::diy, 2, 1 };

    auto* slot0 = source.macros.add (key);
    REQUIRE (slot0 != nullptr);
    slot0->curve.setPoints ({ { 0.0f, 0.1f }, { 0.5f, 0.8f }, { 1.0f, 0.9f } });
    slot0->curve.setSegmentCurvature (0, 0.5f);
    slot0->curve.setSegmentCurvature (1, -0.25f);
    slot0->curve.setOutputRange (0.2f, 0.8f);
    slot0->target = std::make_unique<FakeStateTarget> ("hello");

    auto* slot1 = source.macros.add (key);
    REQUIRE (slot1 != nullptr);   // zweiter Slot ohne Ziel, Default-Kurve

    const auto session = grid::GridSessionStore::capture (source.refs());

    SessionObjects loaded;
    grid::GridSessionStore::apply (session, loaded.refs(),
        [] (const juce::ValueTree& state) -> std::unique_ptr<grid::MacroTarget>
        {
            if (state.hasType ("FakeTarget"))
                return std::make_unique<FakeStateTarget> (state.getProperty ("payload").toString());
            return nullptr;
        });

    REQUIRE (loaded.macros.count (key) == 2);

    auto* loaded0 = loaded.macros.get (key, 0);
    REQUIRE (loaded0 != nullptr);
    REQUIRE (loaded0->curve.numPoints() == 3);
    REQUIRE (loaded0->curve.points()[1].x == Approx (0.5f));
    REQUIRE (loaded0->curve.points()[1].y == Approx (0.8f));
    REQUIRE (loaded0->curve.segmentCurvature (0) == Approx (0.5f));
    REQUIRE (loaded0->curve.segmentCurvature (1) == Approx (-0.25f));
    REQUIRE (loaded0->curve.getOutputMin() == Approx (0.2f));
    REQUIRE (loaded0->curve.getOutputMax() == Approx (0.8f));
    REQUIRE (loaded0->target != nullptr);
    REQUIRE (loaded0->target->describe() == "hello");

    auto* loaded1 = loaded.macros.get (key, 1);
    REQUIRE (loaded1 != nullptr);
    REQUIRE (loaded1->target == nullptr);
    REQUIRE (loaded1->curve.numPoints() == 2);
}

TEST_CASE ("GridSessionStore: MidiCcTarget-Zustand traegt Kanal + CC", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    NullMidiOutput output;

    const grid::MidiCcTarget target (output, 7, 42);
    const auto state = target.toState();

    REQUIRE (state.hasType (grid::MidiCcTarget::kStateType));
    REQUIRE ((int) state.getProperty ("channel") == 7);
    REQUIRE ((int) state.getProperty ("cc") == 42);
}

TEST_CASE ("GridSessionStore: MPE-Achsen-Kurven + offsetBeyondMax Roundtrip", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    SessionObjects source;

    using Axis = grid::GridVoiceEngine::Axis;
    auto& pressureCurve = source.engine.responseCurve (Axis::Pressure);
    pressureCurve.setPoints ({ { 0.0f, 0.0f }, { 0.3f, 0.7f }, { 1.0f, 1.0f } });
    pressureCurve.setSegmentCurvature (0, 0.8f);
    source.engine.setOffsetBeyondMax (Axis::Slide, true);

    const auto session = grid::GridSessionStore::capture (source.refs());

    SessionObjects loaded;
    grid::GridSessionStore::apply (session, loaded.refs(), nullptr);

    const auto& loadedCurve = loaded.engine.responseCurve (Axis::Pressure);
    REQUIRE (loadedCurve.numPoints() == 3);
    REQUIRE (loadedCurve.points()[1].x == Approx (0.3f));
    REQUIRE (loadedCurve.segmentCurvature (0) == Approx (0.8f));
    REQUIRE (loaded.engine.offsetBeyondMax (Axis::Slide));
    REQUIRE_FALSE (loaded.engine.offsetBeyondMax (Axis::Pressure));
}

TEST_CASE ("GridSessionStore: Datei-Roundtrip (XML)", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("ConduitGridSessionTests")
                          .getChildFile (juce::Uuid().toString() + ".xml");

    SessionObjects source;
    source.diyControls.addControl (grid::CcTool::fader, 0, 0, 0, 1);

    REQUIRE (grid::GridSessionStore::saveToFile (file,
                                                 grid::GridSessionStore::capture (source.refs())));

    const auto reloaded = grid::GridSessionStore::loadFromFile (file);
    REQUIRE (reloaded.isValid());

    SessionObjects loaded;
    grid::GridSessionStore::apply (reloaded, loaded.refs(), nullptr);
    REQUIRE (loaded.diyControls.controls().size() == 1);

    // Fehlende Datei: ungueltiger Tree, apply ist ein No-op.
    REQUIRE_FALSE (grid::GridSessionStore::loadFromFile (file.getSiblingFile ("missing.xml")).isValid());

    file.getParentDirectory().deleteRecursively();
}

//==============================================================================
TEST_CASE ("LiveParamSpec: toState/fromState Roundtrip", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    grid::LiveParamSpec spec;
    spec.trackName = "Bass";
    spec.deviceName = "EQ Eight";
    spec.deviceOrdinal = 1;
    spec.paramName = "1 Freq A";
    spec.displayName = "EQ Eight: 1 Freq A";

    const auto state = grid::AbletonParamTarget::specToState (spec);
    REQUIRE (state.hasType (grid::AbletonParamTarget::kStateType));

    const auto restored = grid::AbletonParamTarget::specFromState (state);
    REQUIRE (restored.trackName == "Bass");
    REQUIRE (restored.deviceName == "EQ Eight");
    REQUIRE (restored.deviceOrdinal == 1);
    REQUIRE (restored.paramName == "1 Freq A");
    REQUIRE (restored.displayName == "EQ Eight: 1 Freq A");
}

TEST_CASE ("LiveTargetResolver: findet Track/Device/Parameter ueber Namen", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LiveSetModel model;
    model.applySnapshot ("tracks", juce::JSON::parse (
        R"({"t1": {"name": "Drums"}, "t2": {"name": "Bass"}})"));
    model.applySnapshot ("devices", juce::JSON::parse (R"({
        "chain:t2": ["d1", "d2", "d3"],
        "dev:d1": {"name": "EQ Eight"},
        "dev:d2": {"name": "Compressor"},
        "dev:d3": {"name": "EQ Eight"},
        "parmeta:d3": [{"name": "Device On"},
                       {"name": "1 Freq A", "min": 0.0, "max": 1.0, "quant": false},
                       {"name": "1 Gain A", "min": -15.0, "max": 15.0, "quant": true}]
    })"));

    grid::LiveParamSpec spec;
    spec.trackName = "Bass";
    spec.deviceName = "EQ Eight";
    spec.deviceOrdinal = 1;   // das ZWEITE EQ Eight (d3)
    spec.paramName = "1 Gain A";

    const auto resolved = grid::resolveLiveParam (model, spec);
    REQUIRE (resolved.found);
    REQUIRE (resolved.deviceId == "d3");
    REQUIRE (resolved.parameterIndex == 2);
    REQUIRE (resolved.minValue == Approx (-15.0f));
    REQUIRE (resolved.maxValue == Approx (15.0f));
    REQUIRE (resolved.quantised);
}

TEST_CASE ("LiveTargetResolver: fehlende Merkmale = not found", "[gridsession]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    conduit::LiveSetModel model;
    model.applySnapshot ("tracks", juce::JSON::parse (R"({"t1": {"name": "Bass"}})"));
    model.applySnapshot ("devices", juce::JSON::parse (R"({
        "chain:t1": ["d1"],
        "dev:d1": {"name": "EQ Eight"},
        "parmeta:d1": [{"name": "Device On"}, {"name": "1 Freq A"}]
    })"));

    grid::LiveParamSpec spec;
    spec.trackName = "Bass";
    spec.deviceName = "EQ Eight";
    spec.paramName = "1 Freq A";

    // Basisfall findet.
    REQUIRE (grid::resolveLiveParam (model, spec).found);

    // Unbekannter Track.
    auto wrongTrack = spec;
    wrongTrack.trackName = "Keys";
    REQUIRE_FALSE (grid::resolveLiveParam (model, wrongTrack).found);

    // Unbekanntes Device.
    auto wrongDevice = spec;
    wrongDevice.deviceName = "Operator";
    REQUIRE_FALSE (grid::resolveLiveParam (model, wrongDevice).found);

    // Ordinal ueber der Anzahl gleichnamiger Devices.
    auto wrongOrdinal = spec;
    wrongOrdinal.deviceOrdinal = 1;
    REQUIRE_FALSE (grid::resolveLiveParam (model, wrongOrdinal).found);

    // Unbekannter Parameter.
    auto wrongParam = spec;
    wrongParam.paramName = "2 Freq A";
    REQUIRE_FALSE (grid::resolveLiveParam (model, wrongParam).found);

    // Leere Spec.
    REQUIRE_FALSE (grid::resolveLiveParam (model, {}).found);
}

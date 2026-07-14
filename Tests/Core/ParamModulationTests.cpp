#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <vector>

#include "Core/ConduitMacroTargets.h"
#include "Core/ParamModulation.h"
#include "Modules/ConduitModule.h"

namespace grid = conduit::grid;
using Catch::Approx;

//==============================================================================
TEST_CASE ("computeOffsetNorm: unipolar/bipolar Endpunkte und Amount", "[midirig][macromod]")
{
    // Unipolar: 0 -> 0, 1 -> amount (nur aufwaerts).
    REQUIRE (conduit::computeOffsetNorm (0.0f, false, 1.0f) == Approx (0.0f));
    REQUIRE (conduit::computeOffsetNorm (1.0f, false, 1.0f) == Approx (1.0f));
    REQUIRE (conduit::computeOffsetNorm (0.5f, false, 0.5f) == Approx (0.25f));

    // Bipolar: 0 -> -amount, 0.5 -> 0, 1 -> +amount.
    REQUIRE (conduit::computeOffsetNorm (0.0f, true, 1.0f) == Approx (-1.0f));
    REQUIRE (conduit::computeOffsetNorm (0.5f, true, 1.0f) == Approx (0.0f));
    REQUIRE (conduit::computeOffsetNorm (1.0f, true, 1.0f) == Approx (1.0f));
    REQUIRE (conduit::computeOffsetNorm (1.0f, true, 0.3f) == Approx (0.3f));

    // Eingaben werden geklemmt.
    REQUIRE (conduit::computeOffsetNorm (2.0f, false, 2.0f) == Approx (1.0f));
}

TEST_CASE ("computeModulatedValue: Range-Skalierung + Doppel-Clamp", "[midirig][macromod]")
{
    // Offset skaliert ueber die USER-Range (hier 0..10): +0.5 -> +5.
    REQUIRE (conduit::computeModulatedValue (2.0f, 0.5f, 0.0f, 10.0f, 0.0f, 10.0f)
             == Approx (7.0f));

    // Clamp auf die User-Range: Basis 8 + 5 -> 10.
    REQUIRE (conduit::computeModulatedValue (8.0f, 0.5f, 0.0f, 10.0f, 0.0f, 10.0f)
             == Approx (10.0f));

    // Engere User-Range: Offset skaliert ueber SIE und clamped in ihr.
    REQUIRE (conduit::computeModulatedValue (3.0f, 1.0f, 2.0f, 4.0f, 0.0f, 10.0f)
             == Approx (4.0f));

    // Bipolar nach unten.
    REQUIRE (conduit::computeModulatedValue (5.0f, -0.5f, 0.0f, 10.0f, 0.0f, 10.0f)
             == Approx (0.0f));

    // Degenerierte Range kollabiert auf den einen Wert.
    REQUIRE (conduit::computeModulatedValue (3.0f, 1.0f, 3.0f, 3.0f, 3.0f, 3.0f)
             == Approx (3.0f));

    // Verdrehte Range (min > max) wird normalisiert statt zu explodieren.
    REQUIRE (conduit::computeModulatedValue (5.0f, 0.5f, 10.0f, 0.0f, 10.0f, 0.0f)
             == Approx (10.0f));
}

TEST_CASE ("ParamModKey: Ordnung und Gleichheit", "[midirig][macromod]")
{
    const conduit::ParamModKey a { "node1", "cutoff" };
    const conduit::ParamModKey b { "node1", "resonance" };
    const conduit::ParamModKey c { "node2", "cutoff" };

    REQUIRE (a == conduit::ParamModKey { "node1", "cutoff" });
    REQUIRE_FALSE (a == b);
    REQUIRE ((a < b) != (b < a));
    REQUIRE ((a < c) != (c < a));

    std::map<conduit::ParamModKey, float> map;
    map[a] = 0.1f;
    map[a] = 0.2f;   // gleicher Key ueberschreibt
    map[b] = 0.3f;
    REQUIRE (map.size() == 2);
}

//==============================================================================
namespace
{
    struct FakeParamSink final : conduit::IParamModulationSink
    {
        void setParamModulation (const conduit::ParamModKey& key, float offsetNorm) override
        {
            sets.emplace_back (key, offsetNorm);
        }

        void clearParamModulation (const conduit::ParamModKey& key) override
        {
            clears.push_back (key);
        }

        std::vector<std::pair<conduit::ParamModKey, float>> sets;
        std::vector<conduit::ParamModKey> clears;
    };

    struct FakeGridSink final : grid::IGridControlModSink
    {
        void setControlModulation (const grid::MacroControlKey& key, float offsetNorm) override
        {
            sets.emplace_back (key, offsetNorm);
        }

        void clearControlModulation (const grid::MacroControlKey& key) override
        {
            clears.push_back (key);
        }

        std::vector<std::pair<grid::MacroControlKey, float>> sets;
        std::vector<grid::MacroControlKey> clears;
    };

    juce::ValueTree makeRootWithNode (const juce::String& uuid)
    {
        juce::ValueTree root (conduit::id::root);
        juce::ValueTree nodes (conduit::id::nodes);
        juce::ValueTree node (conduit::id::node);
        node.setProperty (conduit::id::nodeId, uuid, nullptr);
        node.setProperty (conduit::id::moduleId, "lfo_1", nullptr);
        nodes.appendChild (node, nullptr);
        root.appendChild (nodes, nullptr);
        return root;
    }
}

TEST_CASE ("ConduitParamTarget: Offset-Mapping, Dedupe, Dtor-Clear", "[midirig][macromod]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    FakeParamSink sink;
    const auto root = makeRootWithNode ("uuid-1");

    {
        grid::ConduitParamTarget target (sink, root, "uuid-1", "rate", true, 1.0f,
                                         "lfo_1: rate");

        target.sendValue (1.0f);   // bipolar, voll -> +1
        REQUIRE (sink.sets.size() == 1);
        REQUIRE (sink.sets.back().first == conduit::ParamModKey { "uuid-1", "rate" });
        REQUIRE (sink.sets.back().second == Approx (1.0f));

        target.sendValue (1.0f);   // Dedupe auf dem Offset
        REQUIRE (sink.sets.size() == 1);

        target.setAmount (0.5f);   // Setter re-appliziert den letzten Wert
        REQUIRE (sink.sets.size() == 2);
        REQUIRE (sink.sets.back().second == Approx (0.5f));

        target.sendValue (0.25f);  // bipolar: (0.25-0.5)*2*0.5 = -0.25
        REQUIRE (sink.sets.size() == 3);
        REQUIRE (sink.sets.back().second == Approx (-0.25f));

        target.setBipolar (false); // unipolar: 0.25 * 0.5
        REQUIRE (sink.sets.size() == 4);
        REQUIRE (sink.sets.back().second == Approx (0.125f));

        REQUIRE (target.describe().contains ("lfo_1: rate"));
    }

    // Dtor raeumt ab -- Basis kehrt zurueck.
    REQUIRE (sink.clears.size() == 1);
    REQUIRE (sink.clears.back() == conduit::ParamModKey { "uuid-1", "rate" });
}

TEST_CASE ("ConduitParamTarget: describe zeigt 'fehlt' bei geloeschtem Node, Roundtrip", "[midirig][macromod]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    FakeParamSink sink;
    const auto root = makeRootWithNode ("uuid-1");

    grid::ConduitParamTarget target (sink, root, "uuid-GONE", "rate", false, 0.75f,
                                     "lfo_1: rate");
    REQUIRE (target.describe().startsWith ("fehlt:"));

    const auto state = target.toState();
    REQUIRE (state.hasType (grid::ConduitParamTarget::kStateType));
    REQUIRE (state.getProperty ("nodeUuid").toString() == "uuid-GONE");
    REQUIRE (state.getProperty ("paramId").toString() == "rate");
    REQUIRE_FALSE ((bool) state.getProperty ("bipolar"));
    REQUIRE ((float) (double) state.getProperty ("amount") == Approx (0.75f));
    REQUIRE (state.getProperty ("name").toString() == "lfo_1: rate");
}

TEST_CASE ("GridControlModTarget: Offset an die Grid-Senke, Dtor-Clear, Roundtrip", "[midirig][macromod]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;

    FakeGridSink sink;
    const grid::MacroControlKey key { grid::MacroControlKey::diy, 3, 1 };

    {
        grid::GridControlModTarget target (sink, key, true, 0.5f, "Fader 3 · Y");

        target.sendValue (0.0f);   // bipolar -> -0.5
        REQUIRE (sink.sets.size() == 1);
        REQUIRE (sink.sets.back().first == key);
        REQUIRE (sink.sets.back().second == Approx (-0.5f));

        const auto state = target.toState();
        REQUIRE (state.hasType (grid::GridControlModTarget::kStateType));
        REQUIRE ((int) state.getProperty ("layer") == grid::MacroControlKey::diy);
        REQUIRE ((int) state.getProperty ("controlId") == 3);
        REQUIRE ((int) state.getProperty ("axis") == 1);
    }

    REQUIRE (sink.clears.size() == 1);
    REQUIRE (sink.clears.back() == key);
}

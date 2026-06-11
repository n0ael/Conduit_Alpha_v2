#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/NodeUiRegistry.h"
#include "Core/OscController.h"
#include "Modules/ModuleFactory.h"
#include "Modules/StepSequencerModule.h"
#include "Util/ScaleQuantizer.h"

using Catch::Approx;

namespace
{

// Synthetischer Takt: bpm so gewählt, dass bei rate=1 genau 16 Samples
// pro Step vergehen (beatsPerSample = 1/16) — Mathematik bleibt exakt.
constexpr double testSampleRate = 48000.0;
constexpr double bpmFor16SamplesPerStep = 60.0 * testSampleRate / 16.0;

conduit::ClockBus makeBus (double beat, int scaleRoot = 0, int scaleType = 0)
{
    conduit::ClockBus bus;
    bus.current = { bpmFor16SamplesPerStep, beat, testSampleRate, true, scaleRoot, scaleType };
    return bus;
}

void setParam (conduit::StepSequencerModule& sequencer, const juce::String& parameterId, float value)
{
    auto* target = sequencer.getParameterTarget (parameterId);
    REQUIRE (target != nullptr);
    target->store (value, std::memory_order_relaxed);
}

juce::ValueTree makeRootTree()
{
    juce::ValueTree root (conduit::id::root);
    root.appendChild (juce::ValueTree (conduit::id::nodes),               nullptr);
    root.appendChild (juce::ValueTree (conduit::id::connections),         nullptr);
    root.appendChild (juce::ValueTree (conduit::id::calibrationProfiles), nullptr);
    return root;
}

} // namespace

//==============================================================================
TEST_CASE ("Sequencer 4×16: Reihen parallel, Position beat-gelockt", "[sequencer]")
{
    conduit::StepSequencerModule sequencer;
    REQUIRE (sequencer.prepareForGraph (testSampleRate, 64).wasOk());

    auto bus = makeBus (0.0);
    sequencer.setClockBus (&bus);

    setParam (sequencer, "rate", 1.0f);   // → 16 Samples pro Step
    setParam (sequencer, "a1", 0.1f);
    setParam (sequencer, "a2", 0.9f);
    setParam (sequencer, "b1", 0.3f);
    setParam (sequencer, "d1", 0.7f);

    juce::AudioBuffer<float> buffer (8, 64);
    juce::MidiBuffer midi;
    sequencer.processBlock (buffer, midi);

    // Row A (ch0): Samples 0–15 = a1, 16–31 = a2
    REQUIRE (buffer.getSample (0, 0)  == Approx (0.1f));
    REQUIRE (buffer.getSample (0, 15) == Approx (0.1f));
    REQUIRE (buffer.getSample (0, 16) == Approx (0.9f));

    // Reihen teilen die Position: ch2 = b1, ch6 = d1
    REQUIRE (buffer.getSample (2, 0) == Approx (0.3f));
    REQUIRE (buffer.getSample (6, 0) == Approx (0.7f));

    // Gate (ch1): gate=0.5 → erste 8 Samples high, dann low
    REQUIRE (buffer.getSample (1, 0) == Approx (1.0f));
    REQUIRE (buffer.getSample (1, 7) == Approx (1.0f));
    REQUIRE (buffer.getSample (1, 8) == Approx (0.0f));
    REQUIRE (buffer.getSample (1, 16) == Approx (1.0f));  // neuer Step → Gate wieder high

    REQUIRE (sequencer.getCurrentCell() == 3);  // letzter Sample im Block: Step a4 (col 3)
}

//==============================================================================
TEST_CASE ("Sequencer-Modi: 1×64 verkettet die Reihen, übrige Outs still", "[sequencer]")
{
    conduit::StepSequencerModule sequencer;
    REQUIRE (sequencer.prepareForGraph (testSampleRate, 32).wasOk());

    auto bus = makeBus (16.0);  // bei rate=1: Position = Step 16 → erste Zelle von Row B
    sequencer.setClockBus (&bus);

    setParam (sequencer, "rate", 1.0f);
    setParam (sequencer, "mode", 2.0f);   // 1×64
    setParam (sequencer, "b1", 0.6f);

    juce::AudioBuffer<float> buffer (8, 32);
    juce::MidiBuffer midi;
    sequencer.processBlock (buffer, midi);

    REQUIRE (buffer.getSample (0, 0) == Approx (0.6f));  // b1 auf den Row-A-Outs
    REQUIRE (buffer.getSample (2, 0) == Approx (0.0f));  // übrige Kanäle still
    REQUIRE (buffer.getSample (4, 0) == Approx (0.0f));
    REQUIRE (buffer.getSample (6, 0) == Approx (0.0f));
    REQUIRE (sequencer.getCurrentCell() == 1 * 16 + 1);  // Row b, col 2 am Blockende
}

//==============================================================================
TEST_CASE ("Sequencer-Richtungen: reverse und pendulum", "[sequencer]")
{
    conduit::StepSequencerModule sequencer;
    REQUIRE (sequencer.prepareForGraph (testSampleRate, 16).wasOk());

    auto bus = makeBus (0.0);
    sequencer.setClockBus (&bus);
    setParam (sequencer, "rate", 1.0f);
    setParam (sequencer, "length", 4.0f);
    setParam (sequencer, "a1", 0.1f);
    setParam (sequencer, "a2", 0.2f);
    setParam (sequencer, "a3", 0.3f);
    setParam (sequencer, "a4", 0.4f);

    juce::AudioBuffer<float> buffer (8, 16);  // genau 1 Step pro Block
    juce::MidiBuffer midi;

    const auto stepValueAtBeat = [&] (double beat, float directionValue)
    {
        bus.current.beatAtBlockStart = beat;
        setParam (sequencer, "direction", directionValue);
        sequencer.processBlock (buffer, midi);
        return buffer.getSample (0, 0);
    };

    // Reverse: Position 0 → letzte Zelle (a4), Position 1 → a3
    REQUIRE (stepValueAtBeat (0.0, 1.0f) == Approx (0.4f));
    REQUIRE (stepValueAtBeat (1.0, 1.0f) == Approx (0.3f));

    // Pendulum (len 4, Periode 6): 0,1,2,3,2,1,0...
    const float expected[] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.3f, 0.2f, 0.1f };

    for (int k = 0; k < 7; ++k)
        REQUIRE (stepValueAtBeat (static_cast<double> (k), 2.0f) == Approx (expected[k]));
}

//==============================================================================
TEST_CASE ("Sequencer-Swing verschiebt ungerade Steps sample-genau", "[sequencer]")
{
    conduit::StepSequencerModule sequencer;
    REQUIRE (sequencer.prepareForGraph (testSampleRate, 64).wasOk());

    auto bus = makeBus (0.0);
    sequencer.setClockBus (&bus);
    setParam (sequencer, "rate", 1.0f);
    setParam (sequencer, "swing", 0.5f);  // Step-Paar: 24 + 8 Samples statt 16 + 16
    setParam (sequencer, "a1", 0.1f);
    setParam (sequencer, "a2", 0.9f);

    juce::AudioBuffer<float> buffer (8, 64);
    juce::MidiBuffer midi;
    sequencer.processBlock (buffer, midi);

    REQUIRE (buffer.getSample (0, 23) == Approx (0.1f));  // a1 bis Sample 23 (1.5 Steps)
    REQUIRE (buffer.getSample (0, 24) == Approx (0.9f));  // a2 ab Sample 24 (verkürzt)
    REQUIRE (buffer.getSample (0, 31) == Approx (0.9f));
    REQUIRE (buffer.getSample (0, 32) == Approx (0.0f));  // a3 (Default 0) — neues Paar pünktlich
}

//==============================================================================
TEST_CASE ("Sequencer-Probability: deterministisch je Seed (IStochastic)", "[sequencer]")
{
    const auto renderGates = [] (std::uint64_t seed)
    {
        conduit::StepSequencerModule sequencer;
        REQUIRE (sequencer.prepareForGraph (testSampleRate, 256).wasOk());
        auto bus = makeBus (0.0);
        sequencer.setClockBus (&bus);
        sequencer.setRandomSeed (seed);
        setParam (sequencer, "rate", 1.0f);
        setParam (sequencer, "prob", 0.5f);
        setParam (sequencer, "gate", 1.0f);

        juce::AudioBuffer<float> buffer (8, 256);  // 16 Steps
        juce::MidiBuffer midi;
        sequencer.processBlock (buffer, midi);

        juce::String pattern;
        for (int step = 0; step < 16; ++step)
            pattern << (buffer.getSample (1, step * 16) > 0.5f ? "1" : "0");
        return pattern;
    };

    REQUIRE (renderGates (42) == renderGates (42));          // reproduzierbar
    REQUIRE (renderGates (42).contains ("0"));               // prob wirkt überhaupt
    REQUIRE (renderGates (42).contains ("1"));

    // prob=0 → kein Gate, CV läuft trotzdem weiter
    conduit::StepSequencerModule silent;
    REQUIRE (silent.prepareForGraph (testSampleRate, 64).wasOk());
    auto bus = makeBus (0.0);
    silent.setClockBus (&bus);
    setParam (silent, "prob", 0.0f);
    setParam (silent, "rate", 1.0f);
    setParam (silent, "a1", 0.8f);

    juce::AudioBuffer<float> buffer (8, 64);
    juce::MidiBuffer midi;
    silent.processBlock (buffer, midi);
    REQUIRE (buffer.getMagnitude (1, 0, 64) == Approx (0.0f));
    REQUIRE (buffer.getSample (0, 0) == Approx (0.8f));
}

//==============================================================================
TEST_CASE ("Sequencer quantisiert auf die Session-Skala aus dem ClockState", "[sequencer]")
{
    conduit::StepSequencerModule sequencer;
    REQUIRE (sequencer.prepareForGraph (testSampleRate, 16).wasOk());

    // Session: C-Dur (scaleType-Index 1 = major)
    auto bus = makeBus (0.0, 0, static_cast<int> (conduit::ScaleType::major));
    sequencer.setClockBus (&bus);
    setParam (sequencer, "rate", 1.0f);
    setParam (sequencer, "quantize", 1.0f);
    setParam (sequencer, "a1", 0.5f);  // F# → rastet auf G (31 Halbtöne)

    juce::AudioBuffer<float> buffer (8, 16);
    juce::MidiBuffer midi;
    sequencer.processBlock (buffer, midi);

    REQUIRE (buffer.getSample (0, 0)
             == Approx (static_cast<float> (31.0 / conduit::scale::semitonesPerUnit)));

    // quantize aus → roher Wert
    setParam (sequencer, "quantize", 0.0f);
    sequencer.processBlock (buffer, midi);
    REQUIRE (buffer.getSample (0, 0) == Approx (0.5f));
}

//==============================================================================
TEST_CASE ("Sequencer: OSC-Adressen für alle 72 Parameter (a3, rate, …)", "[sequencer]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };
    conduit::SpscQueue<conduit::ParameterUpdate> audioQueue { 64 };
    conduit::OscController osc { root, manager, audioQueue };
    conduit::registerDefaultModules (factory);

    REQUIRE (manager.addModuleNode (conduit::StepSequencerModule::staticModuleId, {}).isValid());
    manager.flushPendingTopologyUpdate();
    osc.flushPendingUpdates();

    const auto addresses = osc.getRegisteredAddresses();
    REQUIRE (addresses.size() == 8 + 64);
    REQUIRE (addresses.contains ("/conduit/generator/sequencer_1/rate"));
    REQUIRE (addresses.contains ("/conduit/generator/sequencer_1/a3"));
    REQUIRE (addresses.contains ("/conduit/generator/sequencer_1/d16"));

    // Parameter-Parsing strikt
    conduit::StepSequencerModule sequencer;
    REQUIRE (sequencer.getParameterTarget ("a1") != nullptr);
    REQUIRE (sequencer.getParameterTarget ("d16") != nullptr);
    REQUIRE (sequencer.getParameterTarget ("e1") == nullptr);
    REQUIRE (sequencer.getParameterTarget ("a17") == nullptr);
    REQUIRE (sequencer.getParameterTarget ("a0") == nullptr);
    REQUIRE (sequencer.getParameterTarget ("a01") == nullptr);
    REQUIRE (sequencer.getParameterTarget ("gain") == nullptr);
}

//==============================================================================
TEST_CASE ("Sequencer-Freilauf ohne ClockBus bewegt sich", "[sequencer]")
{
    conduit::StepSequencerModule sequencer;
    REQUIRE (sequencer.prepareForGraph (testSampleRate, 512).wasOk());
    REQUIRE (sequencer.getParameterTarget ("rate") != nullptr);
    sequencer.getParameterTarget ("rate")->store (8.0f, std::memory_order_relaxed);
    // Freilauf: 8 Steps/Sekunde → 6000 Samples pro Step

    juce::AudioBuffer<float> buffer (8, 512);
    juce::MidiBuffer midi;

    const auto firstCell = sequencer.getCurrentCell();
    for (int block = 0; block < 30; ++block)  // ~15360 Samples ≈ 2,5 Steps
        sequencer.processBlock (buffer, midi);

    REQUIRE (sequencer.getCurrentCell() != firstCell);
}

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Core/GraphManager.h"
#include "Core/LinkClock.h"
#include "Core/NodeUiRegistry.h"
#include "Modules/LfoModule.h"
#include "Modules/ModuleFactory.h"

using Catch::Approx;

namespace
{

juce::ValueTree makeRootTree()
{
    juce::ValueTree root (conduit::id::root);
    root.appendChild (juce::ValueTree (conduit::id::nodes),               nullptr);
    root.appendChild (juce::ValueTree (conduit::id::connections),         nullptr);
    root.appendChild (juce::ValueTree (conduit::id::calibrationProfiles), nullptr);
    return root;
}

/** Erwartetes LFO-Sample für einen Beat-Stand (Phase = Beat × rate). */
float expectedLfoSample (double beat, double rate, float depth)
{
    return static_cast<float> (std::sin (beat * rate * juce::MathConstants<double>::twoPi)) * depth;
}

} // namespace

//==============================================================================
TEST_CASE ("LinkClock: Session-Beat läuft monoton, Tempo ist setzbar", "[clock]")
{
    conduit::LinkClock clock (120.0);
    clock.prepare (48000.0);

    const auto first = clock.captureClockState (32);
    REQUIRE (first.bpm == Approx (120.0));
    REQUIRE (first.sampleRate == Approx (48000.0));

    const auto second = clock.captureClockState (32);
    REQUIRE (second.beatAtBlockStart >= first.beatAtBlockStart);  // Wall-Clock läuft

    // Ohne Peers greift ein Tempo-Vorschlag sofort
    clock.setTempo (140.0);
    REQUIRE (clock.getTempo() == Approx (140.0));
    REQUIRE (clock.captureClockState (32).bpm == Approx (140.0));

    REQUIRE (first.beatsPerSample() == Approx (120.0 / (60.0 * 48000.0)));
}

//==============================================================================
TEST_CASE ("LfoModule: beat-gelockte Phase ist eine reine Funktion des Session-Beats", "[clock]")
{
    conduit::LfoModule lfo;
    REQUIRE (lfo.prepareForGraph (48000.0, 32).wasOk());

    conduit::ClockBus bus;
    bus.current = { 120.0, 0.0, 48000.0, true };
    lfo.setClockBus (&bus);
    REQUIRE (lfo.isClockSynced());

    juce::AudioBuffer<float> buffer (1, 32);
    juce::MidiBuffer midi;

    SECTION ("Beat 0: Sinus startet bei 0 und folgt der Beat-Achse")
    {
        lfo.processBlock (buffer, midi);

        REQUIRE (buffer.getSample (0, 0) == Approx (0.0f).margin (1e-6));

        const auto beatsPerSample = bus.current.beatsPerSample();

        for (int i = 1; i < 32; i += 10)
            REQUIRE (buffer.getSample (0, i)
                     == Approx (expectedLfoSample (i * beatsPerSample, 0.25, 1.0f)).margin (1e-4));
    }

    SECTION ("Beliebiger Beat-Stand: Phase deterministisch — kein interner Drift")
    {
        bus.current.beatAtBlockStart = 17.3;
        lfo.processBlock (buffer, midi);

        REQUIRE (buffer.getSample (0, 0)
                 == Approx (expectedLfoSample (17.3, 0.25, 1.0f)).margin (1e-4));

        // Zwei LFO-Instanzen am selben Bus sind phasenstarr
        conduit::LfoModule twin;
        REQUIRE (twin.prepareForGraph (48000.0, 32).wasOk());
        twin.setClockBus (&bus);

        juce::AudioBuffer<float> twinBuffer (1, 32);
        twin.processBlock (twinBuffer, midi);
        REQUIRE (twinBuffer.getSample (0, 0) == Approx (buffer.getSample (0, 0)).margin (1e-6));
    }
}

//==============================================================================
TEST_CASE ("LfoModule: Freilauf ohne ClockBus bleibt klick-frei über Blockgrenzen", "[clock]")
{
    conduit::LfoModule lfo;
    REQUIRE (lfo.prepareForGraph (48000.0, 32).wasOk());
    REQUIRE_FALSE (lfo.isClockSynced());

    if (auto* rate = lfo.getParameterTarget ("rate"))
        rate->store (2.0f, std::memory_order_relaxed);  // 2 Hz im Freilauf

    juce::AudioBuffer<float> buffer (1, 32);
    juce::MidiBuffer midi;

    float lastSample = 0.0f;
    bool continuous = true;

    // Maximaler Schritt eines 2-Hz-Sinus bei 48kHz: 2π·2/48000 ≈ 0.00026
    constexpr float maxStep = 0.001f;

    for (int block = 0; block < 50; ++block)
    {
        lfo.processBlock (buffer, midi);

        if (block > 0 && std::abs (buffer.getSample (0, 0) - lastSample) > maxStep)
            continuous = false;

        lastSample = buffer.getSample (0, buffer.getNumSamples() - 1);
    }

    REQUIRE (continuous);
}

//==============================================================================
TEST_CASE ("GraphManager injiziert den ClockBus bei der Materialisierung (4.2)", "[clock]")
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    auto root = makeRootTree();
    juce::AudioProcessorGraph graph;
    conduit::GraphFader fader;
    conduit::ModuleFactory factory;
    juce::UndoManager undoManager;
    conduit::NodeUiRegistry uiRegistry;
    conduit::GraphManager manager { root, graph, fader, factory, undoManager, uiRegistry };

    conduit::registerDefaultModules (factory);

    conduit::ClockBus bus;
    manager.setClockBus (&bus);

    const auto node = manager.addModuleNode (conduit::LfoModule::staticModuleId, {});
    REQUIRE (node.isValid());
    manager.flushPendingTopologyUpdate();

    auto* lfo = dynamic_cast<conduit::LfoModule*> (
        manager.getModuleFor (node.getProperty (conduit::id::nodeId).toString()));
    REQUIRE (lfo != nullptr);
    REQUIRE (lfo->isClockSynced());

    // OSC-Adressen beider Parameter sind automatisch ableitbar (Schema 7):
    // /conduit/generator/lfo/rate + /conduit/generator/lfo/depth — die
    // Targets existieren
    REQUIRE (lfo->getParameterTarget ("rate") != nullptr);
    REQUIRE (lfo->getParameterTarget ("depth") != nullptr);
    REQUIRE (lfo->getParameterTarget ("gain") == nullptr);
}

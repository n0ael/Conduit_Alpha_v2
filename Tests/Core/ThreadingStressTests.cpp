#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Core/GraphFader.h"
#include "Modules/AttenuatorModule.h"

// Treibt die Cross-Thread-Pfade (std::atomic-Handshakes) mit echter
// Nebenläufigkeit — primär für die Sanitizer-Presets (CLAUDE.md 13.4):
//   TSan: cmake --preset tsan  (Clang, Linux/macOS/WSL)
//   ASan: cmake --preset asan  (MSVC/Clang)
// Läuft auch im normalen Build als Smoke-Test.

namespace
{

void fillWithOnes (juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (channel),
                                           1.0f, buffer.getNumSamples());
}

// Begrenztes Warten statt Endlos-Spin — hängt nie, auch wenn etwas bricht
template <typename Predicate>
bool spinUntil (Predicate&& predicate)
{
    for (int i = 0; i < 10'000'000; ++i)
    {
        if (predicate())
            return true;

        std::this_thread::yield();
    }

    return false;
}

} // namespace

//==============================================================================
TEST_CASE ("GraphFader: nebenläufige Fade-Zyklen gegen laufenden Audio Thread",
           "[threading]")
{
    conduit::GraphFader fader;
    fader.prepare (48000.0);

    std::atomic<bool> running { true };

    // Simulierter Audio Thread: prozessiert ununterbrochen Blöcke à 32 Samples
    std::thread audioThread ([&fader, &running]
    {
        juce::AudioBuffer<float> buffer (2, 32);

        while (running.load (std::memory_order_acquire))
        {
            fillWithOnes (buffer);
            fader.process (buffer);
        }
    });

    // Message Thread: viele vollständige Fade-Zyklen, inklusive
    // Richtungswechsel mitten in laufenden Fade-In-Rampen
    bool allCyclesCompleted = true;

    for (int cycle = 0; cycle < 200 && allCyclesCompleted; ++cycle)
    {
        fader.beginFadeOut();
        allCyclesCompleted = spinUntil ([&fader] { return fader.isFadeOutComplete(); });
        fader.beginFadeIn();
    }

    running.store (false, std::memory_order_release);
    audioThread.join();

    REQUIRE (allCyclesCompleted);  // kein Fade-Out ging verloren (CAS-Pfad)
}

//==============================================================================
TEST_CASE ("AttenuatorModule: setGain nebenläufig zu processBlock", "[threading]")
{
    conduit::AttenuatorModule attenuator;
    REQUIRE (attenuator.prepareForGraph (48000.0, 32).wasOk());

    std::atomic<bool> running { true };

    std::thread audioThread ([&attenuator, &running]
    {
        juce::AudioBuffer<float> buffer (2, 32);
        juce::MidiBuffer midi;

        while (running.load (std::memory_order_acquire))
        {
            fillWithOnes (buffer);
            attenuator.processBlock (buffer, midi);
        }
    });

    // Message/Netzwerk-Thread: Parameter-Updates im OSC-Takt (6.1)
    for (int i = 0; i < 50'000; ++i)
        attenuator.setGain (static_cast<float> (i % 101) * 0.01f);

    running.store (false, std::memory_order_release);
    audioThread.join();

    // Modul ist nach dem Stress weiterhin funktional und clamped korrekt
    attenuator.setGain (2.0f);  // wird auf 1.0 begrenzt
    juce::AudioBuffer<float> buffer (2, 32);
    juce::MidiBuffer midi;

    for (int block = 0; block < 12; ++block)  // Rampe ausklingen lassen
    {
        fillWithOnes (buffer);
        attenuator.processBlock (buffer, midi);
    }

    CHECK (buffer.getMagnitude (0, 32) <= 1.0f);
}

#include "GraphFader.h"

namespace conduit
{

//==============================================================================
// Message Thread (Audio gestoppt)

void GraphFader::prepare (double sampleRate)
{
    gain.reset (sampleRate, rampSeconds);
    gain.setCurrentAndTargetValue (1.0f);
    lastAppliedPhase = Phase::active;

    requestedPhase.store (Phase::active, std::memory_order_release);
    fadeComplete.store (false, std::memory_order_release);
    prepared.store (true, std::memory_order_release);
}

void GraphFader::reset() noexcept
{
    prepared.store (false, std::memory_order_release);
}

bool GraphFader::isPrepared() const noexcept
{
    return prepared.load (std::memory_order_acquire);
}

void GraphFader::setRampSeconds (double seconds) noexcept
{
    jassert (seconds > 0.0);
    rampSeconds = seconds;
}

//==============================================================================
// Message Thread (Audio läuft)

void GraphFader::beginFadeOut() noexcept
{
    // Reihenfolge: erst fadeComplete zurücksetzen, dann Phase setzen —
    // der Audio Thread darf nie ein veraltetes Complete-Flag bestätigen.
    fadeComplete.store (false, std::memory_order_release);
    requestedPhase.store (Phase::fadingOut, std::memory_order_release);
}

void GraphFader::beginFadeIn() noexcept
{
    requestedPhase.store (Phase::fadingIn, std::memory_order_release);
}

bool GraphFader::isFadeOutComplete() const noexcept
{
    return fadeComplete.load (std::memory_order_acquire);
}

GraphFader::Phase GraphFader::getCurrentPhase() const noexcept
{
    return requestedPhase.load (std::memory_order_acquire);
}

//==============================================================================
// Audio Thread

void GraphFader::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! prepared.load (std::memory_order_acquire))
        return;

    const auto phase = requestedPhase.load (std::memory_order_acquire);

    if (phase != lastAppliedPhase)
    {
        // SmoothedValue retargetet ab dem aktuellen Wert — auch ein
        // Richtungswechsel mitten in einer Rampe bleibt click-frei.
        if (phase == Phase::fadingOut)
            gain.setTargetValue (0.0f);
        else if (phase == Phase::fadingIn)
            gain.setTargetValue (1.0f);

        lastAppliedPhase = phase;
    }

    gain.applyGain (buffer, buffer.getNumSamples());

    if (phase == Phase::fadingOut
        && juce::exactlyEqual (gain.getCurrentValue(), 0.0f)
        && ! fadeComplete.load (std::memory_order_relaxed))
    {
        // Stille erreicht — der Topologie-Swap kann starten (Schritt 2 → 3).
        // Jeder weitere Block wird mit Gain 0 vollständig genullt.
        fadeComplete.store (true, std::memory_order_release);
    }

    if (phase == Phase::fadingIn && ! gain.isSmoothing())
    {
        // Fade-In abgeschlossen → zurück zu Active. CAS statt store: falls der
        // Message Thread gerade einen neuen Fade-Out angefordert hat, darf
        // dieser nicht überschrieben werden.
        auto expected = Phase::fadingIn;

        if (requestedPhase.compare_exchange_strong (expected, Phase::active,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_relaxed))
            lastAppliedPhase = Phase::active;
    }
}

} // namespace conduit

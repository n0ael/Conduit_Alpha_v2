#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/ExpressionAxis.h"

namespace grid = conduit::grid;
using Catch::Approx;

//==============================================================================
TEST_CASE ("ExpressionAxis: inaktiver Slot ohne Rohwert/Offset ist outMin-geklemmt", "[grid]")
{
    grid::ExpressionAxis axis;

    REQUIRE_FALSE (axis.isActive (0));
    REQUIRE (juce::exactlyEqual (axis.combined (0), 0.0f));
}

TEST_CASE ("ExpressionAxis: setRaw ohne Offset liefert den reinen Rohwert", "[grid]")
{
    grid::ExpressionAxis axis;

    axis.activate (0);
    axis.setRaw (0, 0.5f);

    REQUIRE (axis.isActive (0));
    REQUIRE (juce::exactlyEqual (axis.combined (0), 0.5f));
}

TEST_CASE ("ExpressionAxis: setOffset addiert/subtrahiert auf den Rohwert", "[grid]")
{
    grid::ExpressionAxis axis;

    axis.activate (0);
    axis.setRaw (0, 0.5f);

    axis.setOffset (0.3f);
    REQUIRE (axis.combined (0) == Approx (0.8f));

    axis.setOffset (-0.3f);
    REQUIRE (axis.combined (0) == Approx (0.2f));
}

TEST_CASE ("ExpressionAxis: combined klemmt auf [outMin, outMax]", "[grid]")
{
    grid::ExpressionAxis axis; // Default-Config: 0..1

    axis.activate (0);
    axis.setRaw (0, 0.9f);
    axis.setOffset (0.5f);
    REQUIRE (juce::exactlyEqual (axis.combined (0), 1.0f));

    // Bipolare Config (PitchBend-Vorbereitung, noch ungenutzt)
    grid::ExpressionAxis::Config bendConfig { -48.0f, 48.0f, 48.0f };
    grid::ExpressionAxis bendAxis (bendConfig);

    bendAxis.activate (0);
    bendAxis.setRaw (0, 40.0f);
    bendAxis.setOffset (40.0f);
    REQUIRE (juce::exactlyEqual (bendAxis.combined (0), 48.0f));
}

TEST_CASE ("ExpressionAxis: setOffset klemmt auf ±offsetScale", "[grid]")
{
    grid::ExpressionAxis axis; // Default: offsetScale 1.0

    axis.setOffset (5.0f);
    REQUIRE (juce::exactlyEqual (axis.offset(), 1.0f));

    axis.setOffset (-5.0f);
    REQUIRE (juce::exactlyEqual (axis.offset(), -1.0f));
}

TEST_CASE ("ExpressionAxis: deactivate/reset setzen Aktiv-Status und Rohwerte zurück, Offset bleibt", "[grid]")
{
    grid::ExpressionAxis axis;

    axis.activate (0);
    axis.setRaw (0, 0.7f);
    axis.setOffset (0.2f);

    axis.deactivate (0);
    REQUIRE_FALSE (axis.isActive (0));

    axis.activate (0);
    axis.setRaw (0, 0.7f);

    axis.reset();
    REQUIRE_FALSE (axis.isActive (0));
    // Rohwert zurückgesetzt -- kombiniert entspricht jetzt exakt dem Offset
    REQUIRE (juce::exactlyEqual (axis.combined (0), 0.2f));
    // Offset selbst bleibt unverändert
    REQUIRE (juce::exactlyEqual (axis.offset(), 0.2f));
}

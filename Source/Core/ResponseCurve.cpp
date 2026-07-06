#include "ResponseCurve.h"

#include <algorithm>
#include <cmath>

namespace conduit::grid
{

namespace
{
    constexpr float kCurvatureScale = 6.0f;
    constexpr float kMinXGap        = 1.0e-6f;

    float shape (float t, float c) noexcept
    {
        if (juce::exactlyEqual (c, 0.0f))
            return t;

        const auto k = c * kCurvatureScale;
        return (std::exp (k * t) - 1.0f) / (std::exp (k) - 1.0f);
    }
}

ResponseCurve::ResponseCurve() noexcept = default;

float ResponseCurve::apply (float input) const noexcept
{
    const auto frontX = pts.front().x;
    const auto backX  = pts.back().x;

    // Extrapolation: lineare Fortsetzung mit der Sekantensteigung des
    // ersten/letzten Segments (Krümmung außerhalb ignoriert) -- erhält das
    // Weiterwischen der ungeklemmten Quelle (RingTouchModel/PadGridLayout).
    if (input < frontX)
    {
        const auto slope = (pts[1].y - pts[0].y) / (pts[1].x - pts[0].x);
        const auto formY = pts.front().y + slope * (input - frontX);
        return outputMin + (outputMax - outputMin) * formY;
    }

    if (input > backX)
    {
        const auto n = pts.size();
        const auto slope = (pts[n - 1].y - pts[n - 2].y) / (pts[n - 1].x - pts[n - 2].x);
        const auto formY = pts.back().y + slope * (input - backX);
        return outputMin + (outputMax - outputMin) * formY;
    }

    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
    {
        if (input <= pts[i + 1].x)
        {
            const auto t = (input - pts[i].x) / (pts[i + 1].x - pts[i].x);
            const auto formY = pts[i].y + (pts[i + 1].y - pts[i].y) * shape (t, curvature[i]);
            return outputMin + (outputMax - outputMin) * formY;
        }
    }

    return outputMin + (outputMax - outputMin) * pts.back().y;
}

void ResponseCurve::setPoints (const std::vector<Point>& newPoints) noexcept
{
    if (newPoints.size() < 2)
        return;

    pts = newPoints;
    std::sort (pts.begin(), pts.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });

    // X streng aufsteigend erzwingen -- gleiche/abgestiegene X-Werte (durch
    // den Sort nur noch gleich möglich) um ein Epsilon auseinanderziehen,
    // damit die Transferfunktion eindeutig bleibt.
    for (std::size_t i = 1; i < pts.size(); ++i)
        if (pts[i].x <= pts[i - 1].x)
            pts[i].x = pts[i - 1].x + kMinXGap;

    curvature.resize (pts.size() - 1, 0.0f);
}

void ResponseCurve::setSegmentCurvature (int segmentIndex, float c) noexcept
{
    if (segmentIndex < 0 || segmentIndex >= (int) curvature.size())
        return;

    curvature[(std::size_t) segmentIndex] = juce::jlimit (-1.0f, 1.0f, c);
}

void ResponseCurve::setOutputRange (float outMin, float outMax) noexcept
{
    outputMin = outMin;
    outputMax = outMax;
}

int ResponseCurve::numPoints() const noexcept
{
    return (int) pts.size();
}

int ResponseCurve::numSegments() const noexcept
{
    return (int) curvature.size();
}

const std::vector<ResponseCurve::Point>& ResponseCurve::points() const noexcept
{
    return pts;
}

} // namespace conduit::grid

#pragma once

#include <algorithm>
#include <vector>

#include <juce_core/juce_core.h>

namespace conduit
{

//==============================================================================
/**
    Tap-and-Commit-Tempo (User-Entwurf 2026-07-02, DJ/Turntable-Workflow):
    n Taps erfassen das Tempo (Preview in der Tempo-Kachel), der (n+1)-te Tap
    committet es zur Link-Session — weitere Taps committen verfeinert weiter.
    Pause > timeoutSeconds resettet die Messung.

    Tempo-Schätzung über den MEDIAN der Tap-Intervalle (robust gegen einen
    verrissenen Tap). Die Zeitbasis liefert der Aufrufer (Sekunden,
    monoton) — dadurch deterministisch testbar. Message Thread.
*/
class TapTempo
{
public:
    static constexpr double timeoutSeconds = 2.5;

    struct Result
    {
        double previewBpm = 0.0;   // gültig wenn hasPreview
        bool   hasPreview = false; // ab dem zweiten Tap (erstes Intervall)
        bool   committed  = false; // ab Tap requiredTaps + 1
    };

    /** Taps, die das Tempo erfassen, bevor der nächste committet (2..8). */
    void setRequiredTaps (int taps) noexcept { requiredTaps = juce::jlimit (2, 8, taps); }
    [[nodiscard]] int getRequiredTaps() const noexcept { return requiredTaps; }

    Result tap (double timeSeconds)
    {
        if (lastTapTime >= 0.0 && timeSeconds - lastTapTime > timeoutSeconds)
            reset();

        if (lastTapTime >= 0.0)
        {
            intervals.push_back (timeSeconds - lastTapTime);

            if (intervals.size() > maxIntervals)
                intervals.erase (intervals.begin());
        }

        lastTapTime = timeSeconds;
        ++tapCount;

        Result result;
        result.hasPreview = ! intervals.empty();

        if (result.hasPreview)
        {
            const auto median = medianInterval();
            result.previewBpm = median > 0.0 ? 60.0 / median : 0.0;
        }

        result.committed = result.hasPreview && tapCount >= requiredTaps + 1;
        return result;
    }

    /** true, solange die Messung läuft (letzter Tap innerhalb des Timeouts) —
        die UI hält währenddessen die Preview in der Tempo-Kachel. */
    [[nodiscard]] bool isActive (double timeSeconds) const noexcept
    {
        return lastTapTime >= 0.0 && timeSeconds - lastTapTime <= timeoutSeconds;
    }

    void reset() noexcept
    {
        intervals.clear();
        lastTapTime = -1.0;
        tapCount = 0;
    }

private:
    [[nodiscard]] double medianInterval() const
    {
        auto sorted = intervals;
        std::sort (sorted.begin(), sorted.end());

        const auto mid = sorted.size() / 2;
        return sorted.size() % 2 == 1
                   ? sorted[mid]
                   : (sorted[mid - 1] + sorted[mid]) * 0.5;
    }

    static constexpr std::size_t maxIntervals = 16;

    std::vector<double> intervals;
    double lastTapTime = -1.0;
    int tapCount = 0;
    int requiredTaps = 4;
};

} // namespace conduit

#include "LooperTrashCan.h"

namespace conduit
{

namespace
{
    [[nodiscard]] double nowSecondsMt() noexcept
    {
        return juce::Time::getMillisecondCounterHiRes() / 1000.0;
    }
}

LooperTrashCan::LooperTrashCan (LooperBank& bankToUse)
    : bank (bankToUse)
{
}

LooperTrashCan::~LooperTrashCan()
{
    // No-op: die Bank besitzt die Clips (Deklarationsreihenfolge im
    // EngineProcessor stellt sicher, dass sie den Papierkorb überlebt)
    stopTimer();
}

//==============================================================================
void LooperTrashCan::push (Entry entry)
{
    JUCE_ASSERT_MESSAGE_THREAD

    entry.expiresAt = nowSecondsMt() + expirySeconds;
    entries.push_back (std::move (entry));

    startTimer (10'000);   // 10-s-Tick reicht — die Kachel zählt selbst

    if (onChanged != nullptr)
        onChanged();
}

LooperTrashCan::Entry LooperTrashCan::popLatest()
{
    JUCE_ASSERT_MESSAGE_THREAD
    jassert (! entries.empty());

    auto entry = std::move (entries.back());
    entries.pop_back();

    if (entries.empty())
        stopTimer();

    if (onChanged != nullptr)
        onChanged();

    return entry;
}

double LooperTrashCan::secondsRemaining() const noexcept
{
    if (entries.empty())
        return 0.0;

    auto earliest = entries.front().expiresAt;
    for (const auto& entry : entries)
        earliest = juce::jmin (earliest, entry.expiresAt);

    return juce::jmax (0.0, earliest - nowSecondsMt());
}

void LooperTrashCan::clearWithoutDelete() noexcept
{
    entries.clear();
    stopTimer();

    if (onChanged != nullptr)
        onChanged();
}

void LooperTrashCan::expireNow()
{
    JUCE_ASSERT_MESSAGE_THREAD

    for (auto& entry : entries)
        entry.expiresAt = 0.0;

    expireDue (nowSecondsMt());
}

//==============================================================================
void LooperTrashCan::timerCallback()
{
    expireDue (nowSecondsMt());
}

void LooperTrashCan::expireDue (double nowSeconds)
{
    bool changed = false;
    bool expired = false;

    for (int i = (int) entries.size(); --i >= 0;)
    {
        auto& entry = entries[(size_t) i];
        if (entry.expiresAt > nowSeconds)
            continue;

        // Queue voll → Rest bleibt für den nächsten Tick liegen
        if (deleteEntryClips (entry))
        {
            entries.erase (entries.begin() + i);
            changed = true;
            expired = true;
        }
    }

    if (entries.empty())
        stopTimer();

    if (expired && onExpired != nullptr)
        onExpired();
    if (changed && onChanged != nullptr)
        onChanged();
}

bool LooperTrashCan::deleteEntryClips (Entry& entry)
{
    for (int i = (int) entry.clips.size(); --i >= 0;)
    {
        if (bank.deleteClip (entry.clips[(size_t) i].clip).failed())
            return false;   // SPSC-Queue voll — nächster Tick

        entry.clips.erase (entry.clips.begin() + i);
    }

    return true;
}

} // namespace conduit

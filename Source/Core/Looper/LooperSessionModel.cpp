#include "LooperSessionModel.h"

namespace conduit
{

LooperSessionModel::LooperSessionModel (LooperBank& bankToUse)
    : bank (bankToUse)
{
    numTracks.fill (1);
    for (auto& looper : playingSlot)
        looper.fill (-1);
    target.fill (SlotAddress {});
    active.fill (SlotAddress {});
}

//==============================================================================
int LooperSessionModel::getNumTracks (int looperIndex) const noexcept
{
    return validLooper (looperIndex)
         ? numTracks[static_cast<std::size_t> (looperIndex)]
         : 0;
}

bool LooperSessionModel::addLooper() noexcept
{
    if (numLoopers >= LooperBank::maxLoopers)
        return false;

    const auto l = static_cast<std::size_t> (numLoopers);
    numTracks[l] = 1;
    for (auto& track : slots[l])
        track.fill (nullptr);
    playingSlot[l].fill (-1);
    target[l] = SlotAddress {};
    active[l] = SlotAddress {};

    ++numLoopers;
    return true;
}

bool LooperSessionModel::looperHasClips (int looperIndex) const noexcept
{
    if (! validLooper (looperIndex))
        return false;

    const auto l = static_cast<std::size_t> (looperIndex);
    for (const auto& track : slots[l])
        for (const auto* clip : track)
            if (clip != nullptr)
                return true;

    return false;
}

bool LooperSessionModel::trackHasClips (int looperIndex, int trackIndex) const noexcept
{
    if (! validLooper (looperIndex)
        || trackIndex < 0 || trackIndex >= LooperBank::maxTracks)
        return false;

    for (const auto* clip : slots[static_cast<std::size_t> (looperIndex)]
                                 [static_cast<std::size_t> (trackIndex)])
        if (clip != nullptr)
            return true;

    return false;
}

juce::Result LooperSessionModel::removeLastLooper()
{
    if (numLoopers <= 1)
        return juce::Result::fail ("Der letzte Looper bleibt offen");

    const auto looperIndex = numLoopers - 1;
    const auto l = static_cast<std::size_t> (looperIndex);

    // Clips des Loopers verwerfen (Bestätigung ist UI-Sache)
    for (std::size_t t = 0; t < static_cast<std::size_t> (LooperBank::maxTracks); ++t)
    {
        bank.stopTrack (looperIndex, static_cast<int> (t), 0.0);
        for (auto& clip : slots[l][t])
        {
            if (clip != nullptr)
            {
                const auto result = bank.deleteClip (clip);
                if (result.failed())
                    return result;   // Queue voll — Abbruch, Zustand konsistent
                clip = nullptr;
            }
        }
    }

    playingSlot[l].fill (-1);
    target[l] = SlotAddress {};
    active[l] = SlotAddress {};
    numTracks[l] = 1;

    --numLoopers;
    return juce::Result::ok();
}

bool LooperSessionModel::addTrack (int looperIndex) noexcept
{
    if (! validLooper (looperIndex))
        return false;

    auto& tracks = numTracks[static_cast<std::size_t> (looperIndex)];
    if (tracks >= LooperBank::maxTracks)
        return false;

    // Frischer Track: Mix-Defaults zurücksetzen (Bank-Zustand persistiert
    // sonst über Remove/Add hinweg)
    bank.setTrackGain (looperIndex, tracks, 1.0f);
    bank.setTrackPan (looperIndex, tracks, 0.0f);
    bank.setTrackMute (looperIndex, tracks, false);
    bank.setTrackSolo (looperIndex, tracks, false);

    ++tracks;
    return true;
}

juce::Result LooperSessionModel::removeLastTrack (int looperIndex)
{
    if (! validLooper (looperIndex))
        return juce::Result::fail ("Ungültiger Looper");

    const auto l = static_cast<std::size_t> (looperIndex);
    if (numTracks[l] <= 1)
        return juce::Result::fail ("Der letzte Track bleibt");

    const auto trackIndex = numTracks[l] - 1;
    const auto t = static_cast<std::size_t> (trackIndex);

    for (const auto* clip : slots[l][t])
        if (clip != nullptr)
            return juce::Result::fail ("Track enthält Clips — erst löschen");

    if (playingSlot[l][t] >= 0)
        return juce::Result::fail ("Track spielt noch");

    if (target[l].track == trackIndex)
        target[l] = SlotAddress {};
    if (active[l].track == trackIndex)
        active[l] = SlotAddress {};

    --numTracks[l];
    return juce::Result::ok();
}

//==============================================================================
bool LooperSessionModel::validSlotAddress (int l, int t, int s) const noexcept
{
    return validLooper (l)
        && t >= 0 && t < numTracks[static_cast<std::size_t> (l)]
        && s >= 0 && s < maxSlots;
}

LooperSessionModel::SlotAddress LooperSessionModel::getTarget (int looperIndex) const noexcept
{
    return validLooper (looperIndex)
         ? target[static_cast<std::size_t> (looperIndex)]
         : SlotAddress {};
}

void LooperSessionModel::armTarget (int looperIndex, int trackIndex, int slotIndex) noexcept
{
    if (! validSlotAddress (looperIndex, trackIndex, slotIndex))
        return;

    // Belegte Slots sind kein Target (Tap dort = Launch)
    if (clipAt (looperIndex, trackIndex, slotIndex) != nullptr)
        return;

    auto& current = target[static_cast<std::size_t> (looperIndex)];
    const SlotAddress requested { trackIndex, slotIndex };

    current = (current == requested) ? SlotAddress {} : requested;
}

void LooperSessionModel::disarmTarget (int looperIndex) noexcept
{
    if (validLooper (looperIndex))
        target[static_cast<std::size_t> (looperIndex)] = SlotAddress {};
}

int LooperSessionModel::firstFreeSlot (int l, int t, int startSlot) const noexcept
{
    for (int s = juce::jmax (0, startSlot); s < maxSlots; ++s)
        if (clipAt (l, t, s) == nullptr)
            return s;

    return -1;
}

void LooperSessionModel::cycleTargetTrack (int looperIndex) noexcept
{
    if (! validLooper (looperIndex))
        return;

    const auto l = static_cast<std::size_t> (looperIndex);
    const auto tracks = numTracks[l];
    const auto startTrack = target[l].isValid() ? target[l].track : -1;

    // Nächster Track (zyklisch) mit freiem Slot; Target = dessen erster
    // freier Slot. Kein freier Slot irgendwo → Target bleibt unverändert.
    for (int step = 1; step <= tracks; ++step)
    {
        const auto candidate = (startTrack + step) % tracks;
        const auto freeSlot = firstFreeSlot (looperIndex, candidate, 0);
        if (freeSlot >= 0)
        {
            target[l] = { candidate, freeSlot };
            return;
        }
    }
}

//==============================================================================
LooperClip* LooperSessionModel::clipAt (int looperIndex, int trackIndex,
                                        int slotIndex) const noexcept
{
    if (! validLooper (looperIndex)
        || trackIndex < 0 || trackIndex >= LooperBank::maxTracks
        || slotIndex < 0 || slotIndex >= maxSlots)
        return nullptr;

    return slots[static_cast<std::size_t> (looperIndex)]
                [static_cast<std::size_t> (trackIndex)]
                [static_cast<std::size_t> (slotIndex)];
}

void LooperSessionModel::clearReferencesTo (int l, const SlotAddress& address) noexcept
{
    const auto li = static_cast<std::size_t> (l);
    if (active[li] == address)
        active[li] = SlotAddress {};
    if (address.track >= 0
        && playingSlot[li][static_cast<std::size_t> (address.track)] == address.slot)
        playingSlot[li][static_cast<std::size_t> (address.track)] = -1;
}

juce::Result LooperSessionModel::commit (int looperIndex, int bars,
                                         const CaptureService& capture,
                                         int leftIndex, int rightIndex,
                                         const BarSampleAnchors& anchors)
{
    if (! validLooper (looperIndex))
        return juce::Result::fail ("Ungültiger Looper");

    const auto l = static_cast<std::size_t> (looperIndex);
    const auto slot = target[l];
    if (! slot.isValid())
        return juce::Result::fail ("Kein Target-Slot gewählt");

    const auto t = static_cast<std::size_t> (slot.track);
    const auto s = static_cast<std::size_t> (slot.slot);

    // Belegtes Target (Auto-Advance aus): alten Clip überschreiben
    if (auto* old = slots[l][t][s])
    {
        const auto deleted = bank.deleteClip (old);
        if (deleted.failed())
            return deleted;

        slots[l][t][s] = nullptr;
        clearReferencesTo (looperIndex, slot);
    }

    LooperClip* clip = nullptr;
    const auto result = bank.commitClip (looperIndex, slot.track, bars, capture,
                                         leftIndex, rightIndex, anchors, &clip);
    if (result.failed())
        return result;

    slots[l][t][s] = clip;
    playingSlot[l][t] = slot.slot;
    active[l] = slot;

    // Auto-Advance: nächster freier Slot UNTERHALB im selben Track;
    // keiner frei → kein Target (Übergabe §4)
    if (autoAdvance)
    {
        const auto next = firstFreeSlot (looperIndex, slot.track, slot.slot + 1);
        target[l] = next >= 0 ? SlotAddress { slot.track, next } : SlotAddress {};
    }

    return juce::Result::ok();
}

juce::Result LooperSessionModel::startSlot (int looperIndex, int trackIndex,
                                            int slotIndex, double qBeats)
{
    auto* clip = clipAt (looperIndex, trackIndex, slotIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Slot ist leer");

    const auto result = bank.startClip (looperIndex, trackIndex, clip, qBeats);
    if (result.failed())
        return result;

    playingSlot[static_cast<std::size_t> (looperIndex)]
               [static_cast<std::size_t> (trackIndex)] = slotIndex;
    active[static_cast<std::size_t> (looperIndex)] = { trackIndex, slotIndex };
    return juce::Result::ok();
}

juce::Result LooperSessionModel::retriggerSlot (int looperIndex, int trackIndex,
                                                int slotIndex, double qBeats)
{
    auto* clip = clipAt (looperIndex, trackIndex, slotIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Slot ist leer");

    const auto result = bank.retriggerClip (looperIndex, trackIndex, clip, qBeats);
    if (result.failed())
        return result;

    playingSlot[static_cast<std::size_t> (looperIndex)]
               [static_cast<std::size_t> (trackIndex)] = slotIndex;
    active[static_cast<std::size_t> (looperIndex)] = { trackIndex, slotIndex };
    return juce::Result::ok();
}

void LooperSessionModel::stopTrack (int looperIndex, int trackIndex, double qBeats) noexcept
{
    if (! validLooper (looperIndex)
        || trackIndex < 0 || trackIndex >= LooperBank::maxTracks)
        return;

    bank.stopTrack (looperIndex, trackIndex, qBeats);
    playingSlot[static_cast<std::size_t> (looperIndex)]
               [static_cast<std::size_t> (trackIndex)] = -1;
}

juce::Result LooperSessionModel::deleteSlot (int looperIndex, int trackIndex,
                                             int slotIndex)
{
    auto* clip = clipAt (looperIndex, trackIndex, slotIndex);
    if (clip == nullptr)
        return juce::Result::fail ("Slot ist leer");

    const auto result = bank.deleteClip (clip);
    if (result.failed())
        return result;

    slots[static_cast<std::size_t> (looperIndex)]
         [static_cast<std::size_t> (trackIndex)]
         [static_cast<std::size_t> (slotIndex)] = nullptr;
    clearReferencesTo (looperIndex, { trackIndex, slotIndex });
    return juce::Result::ok();
}

LooperClip* LooperSessionModel::detachSlot (int looperIndex, int trackIndex,
                                            int slotIndex) noexcept
{
    auto* clip = clipAt (looperIndex, trackIndex, slotIndex);
    if (clip == nullptr)
        return nullptr;

    // Wie deleteSlot, aber OHNE bank.deleteClip — der Clip bleibt im
    // Store der Bank (Papierkorb-Kontrakt)
    slots[static_cast<std::size_t> (looperIndex)]
         [static_cast<std::size_t> (trackIndex)]
         [static_cast<std::size_t> (slotIndex)] = nullptr;
    clearReferencesTo (looperIndex, { trackIndex, slotIndex });
    return clip;
}

bool LooperSessionModel::attachClip (int looperIndex, int trackIndex, int slotIndex,
                                     LooperClip* clip) noexcept
{
    if (clip == nullptr || ! validSlotAddress (looperIndex, trackIndex, slotIndex)
        || clipAt (looperIndex, trackIndex, slotIndex) != nullptr)
        return false;

    slots[static_cast<std::size_t> (looperIndex)]
         [static_cast<std::size_t> (trackIndex)]
         [static_cast<std::size_t> (slotIndex)] = clip;
    return true;
}

void LooperSessionModel::clearAllClips() noexcept
{
    for (auto& looper : slots)
        for (auto& track : looper)
            track.fill (nullptr);
    for (auto& looper : playingSlot)
        looper.fill (-1);
    target.fill (SlotAddress {});
    active.fill (SlotAddress {});
}

int LooperSessionModel::getPlayingSlot (int looperIndex, int trackIndex) const noexcept
{
    if (! validLooper (looperIndex)
        || trackIndex < 0 || trackIndex >= LooperBank::maxTracks)
        return -1;

    return playingSlot[static_cast<std::size_t> (looperIndex)]
                      [static_cast<std::size_t> (trackIndex)];
}

//==============================================================================
void LooperSessionModel::setActiveSlot (int looperIndex, int trackIndex,
                                        int slotIndex) noexcept
{
    if (clipAt (looperIndex, trackIndex, slotIndex) != nullptr)
        active[static_cast<std::size_t> (looperIndex)] = { trackIndex, slotIndex };
}

LooperSessionModel::SlotAddress LooperSessionModel::getActiveSlot (int looperIndex) const noexcept
{
    return validLooper (looperIndex)
         ? active[static_cast<std::size_t> (looperIndex)]
         : SlotAddress {};
}

LooperClip* LooperSessionModel::getActiveClip (int looperIndex) const noexcept
{
    const auto address = getActiveSlot (looperIndex);
    return address.isValid()
         ? clipAt (looperIndex, address.track, address.slot)
         : nullptr;
}

} // namespace conduit

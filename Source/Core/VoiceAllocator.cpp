#include "VoiceAllocator.h"

namespace conduit::grid
{

VoiceAllocator::VoiceAllocator (int maxVoicesIn) noexcept
    : numVoices (juce::jlimit (1, kMaxVoices, maxVoicesIn))
{
}

int VoiceAllocator::allocate (uint32_t fingerId, uint32_t& stolenFingerOut) noexcept
{
    stolenFingerOut = 0;

    if (fingerId == 0)
        return -1;

    if (const int existing = voiceForFinger (fingerId); existing >= 0)
        return existing;

    for (int i = 0; i < numVoices; ++i)
    {
        if (slotFinger[(size_t) i] == 0)
        {
            slotFinger[(size_t) i] = fingerId;
            slotAge[(size_t) i]    = ++ageCounter;
            return i;
        }
    }

    int oldestIndex = 0;
    for (int i = 1; i < numVoices; ++i)
        if (slotAge[(size_t) i] < slotAge[(size_t) oldestIndex])
            oldestIndex = i;

    stolenFingerOut = slotFinger[(size_t) oldestIndex];
    slotFinger[(size_t) oldestIndex] = fingerId;
    slotAge[(size_t) oldestIndex]    = ++ageCounter;
    return oldestIndex;
}

int VoiceAllocator::release (uint32_t fingerId) noexcept
{
    if (fingerId == 0)
        return -1;

    for (int i = 0; i < numVoices; ++i)
    {
        if (slotFinger[(size_t) i] == fingerId)
        {
            slotFinger[(size_t) i] = 0;
            slotAge[(size_t) i]    = 0;
            return i;
        }
    }

    return -1;
}

int VoiceAllocator::voiceForFinger (uint32_t fingerId) const noexcept
{
    if (fingerId == 0)
        return -1;

    for (int i = 0; i < numVoices; ++i)
        if (slotFinger[(size_t) i] == fingerId)
            return i;

    return -1;
}

void VoiceAllocator::reset() noexcept
{
    slotFinger.fill (0);
    slotAge.fill (0);
    ageCounter = 0;
}

int VoiceAllocator::activeVoices() const noexcept
{
    int count = 0;
    for (int i = 0; i < numVoices; ++i)
        if (slotFinger[(size_t) i] != 0)
            ++count;

    return count;
}

} // namespace conduit::grid

#include "ChordMemory.h"

namespace conduit::grid
{

bool ChordMemory::store (int slotIndex, std::vector<StoredSun> suns)
{
    if (! isValidSlot (slotIndex) || suns.empty() || ! slots[(size_t) slotIndex].empty())
        return false;

    slots[(size_t) slotIndex] = std::move (suns);
    return true;
}

void ChordMemory::clear (int slotIndex)
{
    if (isValidSlot (slotIndex))
        slots[(size_t) slotIndex].clear();
}

bool ChordMemory::isOccupied (int slotIndex) const
{
    return isValidSlot (slotIndex) && ! slots[(size_t) slotIndex].empty();
}

const std::vector<StoredSun>& ChordMemory::slot (int slotIndex) const
{
    static const std::vector<StoredSun> emptySlot;
    return isValidSlot (slotIndex) ? slots[(size_t) slotIndex] : emptySlot;
}

bool ChordMemory::anyOccupied() const
{
    for (const auto& entries : slots)
        if (! entries.empty())
            return true;

    return false;
}

} // namespace conduit::grid

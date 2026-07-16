#include "ChannelStripLayers.h"

namespace conduit::midirig
{

int ChannelStripLayers::decodeSignedDelta (int ccValue7bit) noexcept
{
    return decodeRelativeDelta (ccValue7bit, RelativeEncoding::twosComplement);
}

ChannelStripLayers::Result ChannelStripLayers::feed (const juce::String& column, int ccValue7bit,
                                                     RelativeEncoding encoding)
{
    auto& strip = strips[column];

    const auto oldLayer = strip.layer;
    strip.pos   = juce::jlimit (0, kMaxPos, strip.pos + decodeRelativeDelta (ccValue7bit, encoding));
    strip.layer = layerForPos (strip.pos);

    return { strip.layer, strip.layer != oldLayer };
}

int ChannelStripLayers::layerFor (const juce::String& column) const
{
    const auto it = strips.find (column);
    return it != strips.end() ? it->second.layer : 0;
}

std::map<juce::String, int> ChannelStripLayers::snapshot() const
{
    std::map<juce::String, int> out;
    for (const auto& [column, strip] : strips)
        out[column] = strip.layer;
    return out;
}

void ChannelStripLayers::setLayer (const juce::String& column, int layer)
{
    auto& strip = strips[column];
    strip.layer = juce::jlimit (0, kNumLayers - 1, layer);
    strip.pos   = strip.layer * kStepsPerLayer;   // Zonen-Anfang
}

} // namespace conduit::midirig

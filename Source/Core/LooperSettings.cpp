#include "LooperSettings.h"

namespace conduit
{

namespace
{
    const juce::String keyLooperState = "looperState";

    const juce::Identifier xmlRoot   ("LooperState");
    const juce::Identifier xmlLooper ("Looper");
    const juce::Identifier xmlTrack  ("Track");
}

//==============================================================================
juce::PropertiesFile::Options LooperSettings::defaultOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "Looper";
    options.filenameSuffix      = ".settings";
    options.folderName          = "Conduit";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

LooperSettings::LooperSettings (const juce::PropertiesFile::Options& options)
{
    // Werks-Default OHNE Quelle (Looper-I/O 19.07.2026): "master" steht
    // seit ADR 010 nicht mehr in der Combo, und ein ab Werk gearmter
    // Master-Tap hielte die Capture-Kanäle dauerhaft aktiv — die
    // aufgeschobene Puffersatz-Erweiterung (Tap-Slots jenseits der
    // Reserve) käme nie zum Zug. Alt-Sessions behalten ihren Wert
    // (loadFromFile/Legacy-Migration überschreibt).
    applicationProperties.setStorageParameters (options);
    loadFromFile();
}

LooperSettings::~LooperSettings()
{
    flush();
}

void LooperSettings::flush()
{
    if (auto* file = applicationProperties.getUserSettings())
        file->saveIfNeeded();
}

void LooperSettings::migrateFromLegacy (const juce::String& legacyLooperSource,
                                        bool legacySpectrumView)
{
    if (storedStateLoaded)
        return;

    // "master" war der alte Zwangs-Default (TransportSettings-Fallback) —
    // kein expliziter User-Wunsch. Seit ADR 010 nicht mehr wählbar, und
    // ab Werk gearmter Master blockierte die Puffersatz-Erweiterung.
    loopers[0].sourceKey = legacyLooperSource == "master" ? juce::String()
                                                          : legacyLooperSource;
    loopers[0].spectrum  = legacySpectrumView;
}

//==============================================================================
void LooperSettings::loadFromFile()
{
    const auto xml = applicationProperties.getUserSettings()->getXmlValue (keyLooperState);
    if (xml == nullptr || ! xml->hasTagName (xmlRoot.toString()))
        return;

    storedStateLoaded = true;

    launchQuant = launchQuantFromKey (xml->getStringAttribute ("launchQuant").toStdString(),
                                      LaunchQuant::bar1);
    tapMode = xml->getStringAttribute ("tapMode") == "stop" ? TapMode::toggleStop
                                                            : TapMode::retrigger;
    halveMode = xml->getStringAttribute ("halveMode") == "current"
              ? looper::HalveMode::currentHalf
              : looper::HalveMode::firstHalf;
    reverseMode = xml->getStringAttribute ("reverseMode") == "boundary"
                ? ReverseMode::boundary
                : ReverseMode::immediate;
    variRaster = xml->getStringAttribute ("variRaster") == "scale"
               ? VariRaster::sessionScale
               : VariRaster::semitones;
    variScope = xml->getStringAttribute ("variScope") == "looper"
              ? VariScope::perLooper
              : VariScope::perTrack;
    soloScope = xml->getStringAttribute ("soloScope") == "global"
              ? SoloScope::globalScope
              : SoloScope::perLooper;
    visibleSlots = juce::jlimit (minVisibleSlots, maxVisibleSlots,
                                 xml->getIntAttribute ("visibleSlots", defaultVisibleSlots));
    deleteLatch = xml->getBoolAttribute ("deleteLatch", false);
    autoAdvance = xml->getBoolAttribute ("autoAdvance", true);
    numLoopers = juce::jlimit (1, maxLoopers, xml->getIntAttribute ("numLoopers", 1));

    int looperIndex = 0;
    for (const auto* looperXml : xml->getChildWithTagNameIterator (xmlLooper.toString()))
    {
        if (looperIndex >= maxLoopers)
            break;

        auto& looper = loopers[static_cast<std::size_t> (looperIndex)];
        looper.sourceKey  = looperXml->getStringAttribute ("source");
        looper.spectrum   = looperXml->getBoolAttribute ("spectrum", false);
        looper.sendMaster = looperXml->getBoolAttribute ("sendMaster", true);
        looper.numTracks  = juce::jlimit (1, maxTracks,
                                          looperXml->getIntAttribute ("tracks", 1));

        int trackIndex = 0;
        for (const auto* trackXml : looperXml->getChildWithTagNameIterator (xmlTrack.toString()))
        {
            if (trackIndex >= maxTracks)
                break;

            auto& track = looper.tracks[static_cast<std::size_t> (trackIndex)];
            track.gain = juce::jlimit (0.0f, 2.0f,
                                       (float) trackXml->getDoubleAttribute ("gain", 1.0));
            track.pan = juce::jlimit (-1.0f, 1.0f,
                                      (float) trackXml->getDoubleAttribute ("pan", 0.0));
            track.mute = trackXml->getBoolAttribute ("mute", false);
            track.solo = trackXml->getBoolAttribute ("solo", false);
            track.variQuantized = trackXml->getBoolAttribute ("variQuant", false);
            track.sends = trackXml->getIntAttribute ("sends", 0) & 0xF;
            track.sendPre = trackXml->getBoolAttribute ("sendPre", false);
            ++trackIndex;
        }

        ++looperIndex;
    }
}

void LooperSettings::writeAndNotify()
{
    juce::XmlElement xml (xmlRoot.toString());

    xml.setAttribute ("launchQuant", launchQuantKey (launchQuant));
    xml.setAttribute ("tapMode", tapMode == TapMode::toggleStop ? "stop" : "retrigger");
    xml.setAttribute ("halveMode",
                      halveMode == looper::HalveMode::currentHalf ? "current" : "first");
    xml.setAttribute ("reverseMode",
                      reverseMode == ReverseMode::boundary ? "boundary" : "immediate");
    xml.setAttribute ("variRaster",
                      variRaster == VariRaster::sessionScale ? "scale" : "semi");
    xml.setAttribute ("variScope", variScope == VariScope::perLooper ? "looper" : "track");
    xml.setAttribute ("soloScope", soloScope == SoloScope::globalScope ? "global" : "looper");
    xml.setAttribute ("visibleSlots", visibleSlots);
    xml.setAttribute ("deleteLatch", deleteLatch);
    xml.setAttribute ("autoAdvance", autoAdvance);
    xml.setAttribute ("numLoopers", numLoopers);

    for (int l = 0; l < maxLoopers; ++l)
    {
        const auto& looper = loopers[static_cast<std::size_t> (l)];
        auto* looperXml = xml.createNewChildElement (xmlLooper.toString());
        looperXml->setAttribute ("source", looper.sourceKey);
        looperXml->setAttribute ("spectrum", looper.spectrum);
        looperXml->setAttribute ("sendMaster", looper.sendMaster);
        looperXml->setAttribute ("tracks", looper.numTracks);

        for (int t = 0; t < maxTracks; ++t)
        {
            const auto& track = looper.tracks[static_cast<std::size_t> (t)];
            auto* trackXml = looperXml->createNewChildElement (xmlTrack.toString());
            trackXml->setAttribute ("gain", track.gain);
            trackXml->setAttribute ("pan", track.pan);
            trackXml->setAttribute ("mute", track.mute);
            trackXml->setAttribute ("solo", track.solo);
            trackXml->setAttribute ("variQuant", track.variQuantized);
            trackXml->setAttribute ("sends", track.sends);
            trackXml->setAttribute ("sendPre", track.sendPre);
        }
    }

    applicationProperties.getUserSettings()->setValue (keyLooperState, &xml);
    storedStateLoaded = true;
    sendChangeMessage();
}

//==============================================================================
void LooperSettings::setLaunchQuant (LaunchQuant quant)
{
    if (launchQuant == quant)
        return;
    launchQuant = quant;
    writeAndNotify();
}

void LooperSettings::setTapMode (TapMode mode)
{
    if (tapMode == mode)
        return;
    tapMode = mode;
    writeAndNotify();
}

void LooperSettings::setHalveMode (looper::HalveMode mode)
{
    if (halveMode == mode)
        return;
    halveMode = mode;
    writeAndNotify();
}

void LooperSettings::setReverseMode (ReverseMode mode)
{
    if (reverseMode == mode)
        return;
    reverseMode = mode;
    writeAndNotify();
}

void LooperSettings::setVariRaster (VariRaster raster)
{
    if (variRaster == raster)
        return;
    variRaster = raster;
    writeAndNotify();
}

void LooperSettings::setVariScope (VariScope scope)
{
    if (variScope == scope)
        return;
    variScope = scope;
    writeAndNotify();
}

void LooperSettings::setSoloScope (SoloScope scope)
{
    if (soloScope == scope)
        return;
    soloScope = scope;
    writeAndNotify();
}

void LooperSettings::setVisibleSlots (int slots)
{
    const auto clamped = juce::jlimit (minVisibleSlots, maxVisibleSlots, slots);
    if (visibleSlots == clamped)
        return;
    visibleSlots = clamped;
    writeAndNotify();
}

void LooperSettings::setDeleteLatchEnabled (bool enabled)
{
    if (deleteLatch == enabled)
        return;
    deleteLatch = enabled;
    writeAndNotify();
}

void LooperSettings::setAutoAdvanceEnabled (bool enabled)
{
    if (autoAdvance == enabled)
        return;
    autoAdvance = enabled;
    writeAndNotify();
}

void LooperSettings::setNumLoopers (int count)
{
    const auto clamped = juce::jlimit (1, maxLoopers, count);
    if (numLoopers == clamped)
        return;
    numLoopers = clamped;
    writeAndNotify();
}

//==============================================================================
juce::String LooperSettings::getSourceKey (int looperIndex) const noexcept
{
    return validLooper (looperIndex)
         ? loopers[static_cast<std::size_t> (looperIndex)].sourceKey
         : juce::String();
}

void LooperSettings::setSourceKey (int looperIndex, const juce::String& key)
{
    if (! validLooper (looperIndex)
        || loopers[static_cast<std::size_t> (looperIndex)].sourceKey == key)
        return;

    loopers[static_cast<std::size_t> (looperIndex)].sourceKey = key;
    writeAndNotify();
}

bool LooperSettings::isSpectrumView (int looperIndex) const noexcept
{
    return validLooper (looperIndex)
        && loopers[static_cast<std::size_t> (looperIndex)].spectrum;
}

void LooperSettings::setSpectrumView (int looperIndex, bool spectrum)
{
    if (! validLooper (looperIndex)
        || loopers[static_cast<std::size_t> (looperIndex)].spectrum == spectrum)
        return;

    loopers[static_cast<std::size_t> (looperIndex)].spectrum = spectrum;
    writeAndNotify();
}

bool LooperSettings::isSendToMaster (int looperIndex) const noexcept
{
    return ! validLooper (looperIndex)
        || loopers[static_cast<std::size_t> (looperIndex)].sendMaster;
}

void LooperSettings::setSendToMaster (int looperIndex, bool enabled)
{
    if (! validLooper (looperIndex)
        || loopers[static_cast<std::size_t> (looperIndex)].sendMaster == enabled)
        return;

    loopers[static_cast<std::size_t> (looperIndex)].sendMaster = enabled;
    writeAndNotify();
}

int LooperSettings::getNumTracks (int looperIndex) const noexcept
{
    return validLooper (looperIndex)
         ? loopers[static_cast<std::size_t> (looperIndex)].numTracks
         : 1;
}

void LooperSettings::setNumTracks (int looperIndex, int count)
{
    const auto clamped = juce::jlimit (1, maxTracks, count);
    if (! validLooper (looperIndex)
        || loopers[static_cast<std::size_t> (looperIndex)].numTracks == clamped)
        return;

    loopers[static_cast<std::size_t> (looperIndex)].numTracks = clamped;
    writeAndNotify();
}

float LooperSettings::getTrackGain (int looperIndex, int trackIndex) const noexcept
{
    return validTrack (looperIndex, trackIndex)
         ? loopers[static_cast<std::size_t> (looperIndex)]
               .tracks[static_cast<std::size_t> (trackIndex)].gain
         : 1.0f;
}

void LooperSettings::setTrackGain (int looperIndex, int trackIndex, float gain)
{
    if (! validTrack (looperIndex, trackIndex))
        return;

    const auto clamped = juce::jlimit (0.0f, 2.0f, gain);
    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];
    if (juce::exactlyEqual (track.gain, clamped))
        return;

    track.gain = clamped;
    writeAndNotify();
}

float LooperSettings::getTrackPan (int looperIndex, int trackIndex) const noexcept
{
    return validTrack (looperIndex, trackIndex)
         ? loopers[static_cast<std::size_t> (looperIndex)]
               .tracks[static_cast<std::size_t> (trackIndex)].pan
         : 0.0f;
}

void LooperSettings::setTrackPan (int looperIndex, int trackIndex, float pan)
{
    if (! validTrack (looperIndex, trackIndex))
        return;

    const auto clamped = juce::jlimit (-1.0f, 1.0f, pan);
    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];
    if (juce::exactlyEqual (track.pan, clamped))
        return;

    track.pan = clamped;
    writeAndNotify();
}

bool LooperSettings::isTrackMuted (int looperIndex, int trackIndex) const noexcept
{
    return validTrack (looperIndex, trackIndex)
        && loopers[static_cast<std::size_t> (looperIndex)]
               .tracks[static_cast<std::size_t> (trackIndex)].mute;
}

void LooperSettings::setTrackMuted (int looperIndex, int trackIndex, bool muted)
{
    if (! validTrack (looperIndex, trackIndex))
        return;

    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];
    if (track.mute == muted)
        return;

    track.mute = muted;
    writeAndNotify();
}

bool LooperSettings::isTrackSolo (int looperIndex, int trackIndex) const noexcept
{
    return validTrack (looperIndex, trackIndex)
        && loopers[static_cast<std::size_t> (looperIndex)]
               .tracks[static_cast<std::size_t> (trackIndex)].solo;
}

void LooperSettings::setTrackSolo (int looperIndex, int trackIndex, bool solo)
{
    if (! validTrack (looperIndex, trackIndex))
        return;

    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];
    if (track.solo == solo)
        return;

    track.solo = solo;
    writeAndNotify();
}

bool LooperSettings::isTrackVariQuantized (int looperIndex, int trackIndex) const noexcept
{
    return validTrack (looperIndex, trackIndex)
        && loopers[static_cast<std::size_t> (looperIndex)]
               .tracks[static_cast<std::size_t> (trackIndex)].variQuantized;
}

void LooperSettings::setTrackVariQuantized (int looperIndex, int trackIndex, bool quantized)
{
    if (! validTrack (looperIndex, trackIndex))
        return;

    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];
    if (track.variQuantized == quantized)
        return;

    track.variQuantized = quantized;
    writeAndNotify();
}

int LooperSettings::getTrackSends (int looperIndex, int trackIndex) const noexcept
{
    return validTrack (looperIndex, trackIndex)
         ? loopers[static_cast<std::size_t> (looperIndex)]
               .tracks[static_cast<std::size_t> (trackIndex)].sends
         : 0;
}

void LooperSettings::setTrackSends (int looperIndex, int trackIndex, int mask)
{
    if (! validTrack (looperIndex, trackIndex))
        return;

    const auto clamped = mask & 0xF;
    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];
    if (track.sends == clamped)
        return;

    track.sends = clamped;
    writeAndNotify();
}

bool LooperSettings::isTrackSendPre (int looperIndex, int trackIndex) const noexcept
{
    return validTrack (looperIndex, trackIndex)
        && loopers[static_cast<std::size_t> (looperIndex)]
               .tracks[static_cast<std::size_t> (trackIndex)].sendPre;
}

void LooperSettings::setTrackSendPre (int looperIndex, int trackIndex, bool pre)
{
    if (! validTrack (looperIndex, trackIndex))
        return;

    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];
    if (track.sendPre == pre)
        return;

    track.sendPre = pre;
    writeAndNotify();
}

} // namespace conduit

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

    distanceState.hiDumpDb   = juce::jlimit (0.0f, 18.0f,
        (float) xml->getDoubleAttribute ("distHiDump", 9.0));
    distanceState.hiCutHz    = juce::jlimit (500.0f, 16000.0f,
        (float) xml->getDoubleAttribute ("distHiCut", 8000.0));
    distanceState.baseFreqHz = juce::jlimit (200.0f, 4000.0f,
        (float) xml->getDoubleAttribute ("distBaseFreq", 2000.0));
    distanceState.width01    = juce::jlimit (0.0f, 1.0f,
        (float) xml->getDoubleAttribute ("distWidth", 0.5));
    distanceState.volDumpOn  = xml->getBoolAttribute ("distVolDumpOn", true);
    distanceState.volDumpDb  = juce::jlimit (0.0f, 24.0f,
        (float) xml->getDoubleAttribute ("distVolDump", 12.0));
    distanceState.smoothMs   = juce::jlimit (0.0f, 500.0f,
        (float) xml->getDoubleAttribute ("distSmoothMs", 20.0));
    distanceState.ySens      = juce::jlimit (0.0f, 1.0f,
        (float) xml->getDoubleAttribute ("distYSens", 1.0));

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
            if (trackXml->hasAttribute ("send0"))
            {
                for (int s = 0; s < 4; ++s)
                    track.sendLevel[static_cast<std::size_t> (s)] = juce::jlimit (
                        0.0f, 1.0f,
                        (float) trackXml->getDoubleAttribute ("send" + juce::String (s), 0.0));
            }
            else
            {
                // Migration Alt-Schema: An/Aus-Bitmaske → Level 1.0 pro
                // gesetztem Bit (der frühere Send-Add war Unity-Gain)
                const auto legacyMask = trackXml->getIntAttribute ("sends", 0) & 0xF;
                for (int s = 0; s < 4; ++s)
                    track.sendLevel[static_cast<std::size_t> (s)] =
                        (legacyMask & (1 << s)) != 0 ? 1.0f : 0.0f;
            }
            track.sendPre = trackXml->getBoolAttribute ("sendPre", false);
            track.distance = juce::jlimit (0.0f, 1.0f,
                (float) trackXml->getDoubleAttribute ("dist", 0.0));
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

    xml.setAttribute ("distHiDump", distanceState.hiDumpDb);
    xml.setAttribute ("distHiCut", distanceState.hiCutHz);
    xml.setAttribute ("distBaseFreq", distanceState.baseFreqHz);
    xml.setAttribute ("distWidth", distanceState.width01);
    xml.setAttribute ("distVolDumpOn", distanceState.volDumpOn);
    xml.setAttribute ("distVolDump", distanceState.volDumpDb);
    xml.setAttribute ("distSmoothMs", distanceState.smoothMs);
    xml.setAttribute ("distYSens", distanceState.ySens);

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
            for (int s = 0; s < 4; ++s)
                trackXml->setAttribute ("send" + juce::String (s),
                                        track.sendLevel[static_cast<std::size_t> (s)]);
            trackXml->setAttribute ("sendPre", track.sendPre);
            trackXml->setAttribute ("dist", track.distance);
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

float LooperSettings::getTrackSendLevel (int looperIndex, int trackIndex,
                                         int sendIndex) const noexcept
{
    return validTrack (looperIndex, trackIndex) && sendIndex >= 0 && sendIndex < 4
         ? loopers[static_cast<std::size_t> (looperIndex)]
               .tracks[static_cast<std::size_t> (trackIndex)]
               .sendLevel[static_cast<std::size_t> (sendIndex)]
         : 0.0f;
}

void LooperSettings::setTrackSendLevel (int looperIndex, int trackIndex, int sendIndex,
                                        float level01)
{
    if (! validTrack (looperIndex, trackIndex) || sendIndex < 0 || sendIndex >= 4)
        return;

    const auto clamped = juce::jlimit (0.0f, 1.0f, level01);
    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];
    auto& level = track.sendLevel[static_cast<std::size_t> (sendIndex)];
    if (juce::exactlyEqual (level, clamped))
        return;

    level = clamped;
    writeAndNotify();
}

int LooperSettings::getTrackSends (int looperIndex, int trackIndex) const noexcept
{
    if (! validTrack (looperIndex, trackIndex))
        return 0;

    const auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                            .tracks[static_cast<std::size_t> (trackIndex)];
    int mask = 0;
    for (int s = 0; s < 4; ++s)
        if (track.sendLevel[static_cast<std::size_t> (s)] > 0.0f)
            mask |= 1 << s;
    return mask;
}

void LooperSettings::setTrackSends (int looperIndex, int trackIndex, int mask)
{
    if (! validTrack (looperIndex, trackIndex))
        return;

    const auto clamped = mask & 0xF;
    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];

    bool changed = false;
    for (int s = 0; s < 4; ++s)
    {
        const auto target = (clamped & (1 << s)) != 0 ? 1.0f : 0.0f;
        auto& level = track.sendLevel[static_cast<std::size_t> (s)];
        // Bit gesetzt + Level > 0 bleibt unangetastet (Dialog-Toggle darf
        // ein feines Level nicht auf 1.0 plätten)
        if (target > 0.0f ? level > 0.0f : juce::exactlyEqual (level, 0.0f))
            continue;
        level = target;
        changed = true;
    }

    if (changed)
        writeAndNotify();
}

float LooperSettings::getTrackDistance (int looperIndex, int trackIndex) const noexcept
{
    return validTrack (looperIndex, trackIndex)
         ? loopers[static_cast<std::size_t> (looperIndex)]
               .tracks[static_cast<std::size_t> (trackIndex)].distance
         : 0.0f;
}

void LooperSettings::setTrackDistance (int looperIndex, int trackIndex, float distance01)
{
    if (! validTrack (looperIndex, trackIndex))
        return;

    const auto clamped = juce::jlimit (0.0f, 1.0f, distance01);
    auto& track = loopers[static_cast<std::size_t> (looperIndex)]
                      .tracks[static_cast<std::size_t> (trackIndex)];
    if (juce::exactlyEqual (track.distance, clamped))
        return;

    track.distance = clamped;
    writeAndNotify();
}

void LooperSettings::setDistance (const DistanceState& state)
{
    DistanceState clamped;
    clamped.hiDumpDb   = juce::jlimit (0.0f, 18.0f, state.hiDumpDb);
    clamped.hiCutHz    = juce::jlimit (500.0f, 16000.0f, state.hiCutHz);
    clamped.baseFreqHz = juce::jlimit (200.0f, 4000.0f, state.baseFreqHz);
    clamped.width01    = juce::jlimit (0.0f, 1.0f, state.width01);
    clamped.volDumpOn  = state.volDumpOn;
    clamped.volDumpDb  = juce::jlimit (0.0f, 24.0f, state.volDumpDb);
    clamped.smoothMs   = juce::jlimit (0.0f, 500.0f, state.smoothMs);
    clamped.ySens      = juce::jlimit (0.0f, 1.0f, state.ySens);

    const auto& d = distanceState;
    if (juce::exactlyEqual (d.hiDumpDb, clamped.hiDumpDb)
        && juce::exactlyEqual (d.hiCutHz, clamped.hiCutHz)
        && juce::exactlyEqual (d.baseFreqHz, clamped.baseFreqHz)
        && juce::exactlyEqual (d.width01, clamped.width01)
        && d.volDumpOn == clamped.volDumpOn
        && juce::exactlyEqual (d.volDumpDb, clamped.volDumpDb)
        && juce::exactlyEqual (d.smoothMs, clamped.smoothMs)
        && juce::exactlyEqual (d.ySens, clamped.ySens))
        return;

    distanceState = clamped;
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

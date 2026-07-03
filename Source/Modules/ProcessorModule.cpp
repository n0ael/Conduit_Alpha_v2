#include "ProcessorModule.h"

namespace conduit
{

namespace
{
    /** Gain-Ramp frame-außen, Kanäle innen — der SmoothedValue wird genau
        EINMAL pro Frame fortgeschrieben und identisch auf alle Audio-Kanäle
        angewandt (CV-Kanäle bleiben unberührt). */
    void applyGainRamp (juce::AudioBuffer<float>& buffer, int numChannels, int numFrames,
                        juce::SmoothedValue<float>& gain) noexcept
    {
        if (! gain.isSmoothing())
        {
            const float g = gain.getTargetValue();

            for (int channel = 0; channel < numChannels; ++channel)
                juce::FloatVectorOperations::multiply (buffer.getWritePointer (channel), g, numFrames);

            return;
        }

        for (int frame = 0; frame < numFrames; ++frame)
        {
            const float g = gain.getNextValue();

            for (int channel = 0; channel < numChannels; ++channel)
                buffer.getWritePointer (channel)[frame] *= g;
        }
    }

    [[nodiscard]] float gainFromDb (float db) noexcept
    {
        return juce::Decibels::decibelsToGain (db, ChassisSchema::gainFloorDb);
    }
} // namespace

//==============================================================================
juce::AudioProcessor::BusesProperties ProcessorModule::makeChassisBuses (int numAudioIns,
                                                                         int numAudioOuts,
                                                                         int numDspParams)
{
    // Kanal-Layout FEST (4.6): Audio 0..numAudioIns-1, CV numAudioIns..N.
    // uiHidden ändert dieses Layout NIE — Ausblenden ist reine UI-Sache.
    auto buses = BusesProperties()
                     .withInput ("Audio", juce::AudioChannelSet::canonicalChannelSet (numAudioIns), true)
                     .withOutput ("Audio", juce::AudioChannelSet::canonicalChannelSet (numAudioOuts), true);

    if (numDspParams > 0)
        buses = buses.withInput ("CV", juce::AudioChannelSet::discreteChannels (numDspParams), true);

    return buses;
}

ProcessorModule::ProcessorModule (std::vector<ChassisParamDesc> dspParameterDescs,
                                  int numAudioInsToUse, int numAudioOutsToUse)
    : ConduitModule (makeChassisBuses (numAudioInsToUse, numAudioOutsToUse,
                                       static_cast<int> (dspParameterDescs.size()))),
      dspParams (std::move (dspParameterDescs)),
      numAudioIns (numAudioInsToUse),
      numAudioOuts (numAudioOutsToUse)
{
    jassert (getNumDspParameters() <= maxDspParameters);
    jassert (numAudioIns <= static_cast<int> (audioViewPointers.size())
             && numAudioOuts <= static_cast<int> (audioViewPointers.size()));

    for (size_t i = 0; i < dspParams.size(); ++i)
    {
        dspBase[i].store (dspParams[i].defaultValue, std::memory_order_relaxed);
        userRangeMin[i].store (dspParams[i].hardMin, std::memory_order_relaxed);
        userRangeMax[i].store (dspParams[i].hardMax, std::memory_order_relaxed);
        linkSource[i].store (-1, std::memory_order_relaxed);
        effective[i] = dspParams[i].defaultValue;
        stage1[i]    = dspParams[i].defaultValue;
    }
}

ProcessorModule::~ProcessorModule()
{
    // Der Destruktor läuft erst, wenn der Graph jede Render-Referenz
    // freigegeben hat — der LinkSendTaps-Destruktor balanciert Sinks +
    // enableAudio-Refcount auch ohne vorherige Phase 1 (5.3).
}

//==============================================================================
juce::ValueTree ProcessorModule::createState()
{
    auto nodeTree = ConduitModule::createState();
    nodeTree.setProperty (id::linkSendEnabled, false, nullptr);
    return nodeTree;
}

void ProcessorModule::appendParametersTo (juce::ValueTree& parameters)
{
    const auto addWithRole = [&parameters] (juce::ValueTree parameter, const char* role)
    {
        parameter.setProperty (id::paramRole, role, nullptr);
        parameters.appendChild (parameter, nullptr);
    };

    using S = ChassisSchema;

    addWithRole (makeParameter (S::inputGainId,  S::gainDefaultDb, S::gainMinDb,
                                S::gainMaxDb, S::gainDefaultDb),
                 S::roleChassis);
    addWithRole (makeParameter (S::outputGainId, S::gainDefaultDb, S::gainMinDb,
                                S::gainMaxDb, S::gainDefaultDb),
                 S::roleChassis);

    for (const auto& desc : dspParams)
    {
        addWithRole (makeParameter (desc.id, desc.defaultValue, desc.hardMin,
                                    desc.hardMax, desc.defaultValue),
                     S::roleDsp);
        addWithRole (makeParameter (S::cvAmountIdFor (desc.id), S::cvAmountDefault,
                                    S::cvAmountMin, S::cvAmountMax, S::cvAmountDefault),
                     S::roleCvAmount);
    }
}

void ProcessorModule::setParameterUserRange (const juce::String& dspParamId,
                                             float userMin, float userMax) noexcept
{
    for (size_t i = 0; i < dspParams.size(); ++i)
    {
        if (dspParamId != dspParams[i].id)
            continue;

        const auto lo = juce::jlimit (dspParams[i].hardMin, dspParams[i].hardMax, userMin);
        const auto hi = juce::jlimit (dspParams[i].hardMin, dspParams[i].hardMax, userMax);
        userRangeMin[i].store (juce::jmin (lo, hi), std::memory_order_relaxed);
        userRangeMax[i].store (juce::jmax (lo, hi), std::memory_order_relaxed);
        return;
    }
}

void ProcessorModule::setParameterLink (const juce::String& targetParamId,
                                        const juce::String& sourceParamId, float amount) noexcept
{
    for (size_t target = 0; target < dspParams.size(); ++target)
    {
        if (targetParamId != dspParams[target].id)
            continue;

        int resolvedSource = -1;

        if (sourceParamId.isNotEmpty() && sourceParamId != targetParamId)
            for (size_t source = 0; source < dspParams.size(); ++source)
                if (sourceParamId == dspParams[source].id)
                {
                    resolvedSource = static_cast<int> (source);
                    break;
                }

        linkAmount[target].store (juce::jlimit (-1.0f, 1.0f, amount), std::memory_order_relaxed);
        linkSource[target].store (resolvedSource, std::memory_order_relaxed);
        return;
    }
}

void ProcessorModule::setParameterLinkCurve (const juce::String& targetParamId,
                                             std::optional<ChassisSchema::BezierCurve> curve) noexcept
{
    for (size_t i = 0; i < dspParams.size(); ++i)
    {
        if (targetParamId != dspParams[i].id)
            continue;

        if (curve.has_value())
        {
            linkCurveX1[i].store (curve->x1, std::memory_order_relaxed);
            linkCurveY1[i].store (curve->y1, std::memory_order_relaxed);
            linkCurveX2[i].store (curve->x2, std::memory_order_relaxed);
            linkCurveY2[i].store (curve->y2, std::memory_order_relaxed);
        }

        linkCurveOn[i].store (curve.has_value(), std::memory_order_relaxed);
        return;
    }
}

bool ProcessorModule::hasParameterLinkCurve (const juce::String& dspParamId) const noexcept
{
    for (size_t i = 0; i < dspParams.size(); ++i)
        if (dspParamId == dspParams[i].id)
            return linkCurveOn[i].load (std::memory_order_relaxed);

    return false;
}

int ProcessorModule::getParameterLinkSourceIndex (const juce::String& dspParamId) const noexcept
{
    for (size_t i = 0; i < dspParams.size(); ++i)
        if (dspParamId == dspParams[i].id)
            return linkSource[i].load (std::memory_order_relaxed);

    return -1;
}

juce::Range<float> ProcessorModule::getParameterUserRange (const juce::String& dspParamId) const noexcept
{
    for (size_t i = 0; i < dspParams.size(); ++i)
        if (dspParamId == dspParams[i].id)
            return { userRangeMin[i].load (std::memory_order_relaxed),
                     userRangeMax[i].load (std::memory_order_relaxed) };

    return {};
}

std::atomic<float>* ProcessorModule::getParameterTarget (const juce::String& parameterId) noexcept
{
    if (parameterId == ChassisSchema::inputGainId)
        return &inputGainDb;

    if (parameterId == ChassisSchema::outputGainId)
        return &outputGainDb;

    for (size_t i = 0; i < dspParams.size(); ++i)
    {
        if (parameterId == dspParams[i].id)
            return &dspBase[i];

        if (parameterId.endsWith (ChassisSchema::cvAmountSuffix)
            && parameterId == ChassisSchema::cvAmountIdFor (dspParams[i].id))
            return &cvAmount[i];
    }

    return nullptr;
}

//==============================================================================
bool ProcessorModule::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Layout ist fix seit dem Konstruktor — keine Verhandlung.
    if (layouts.getMainInputChannels() != numAudioIns
        || layouts.getMainOutputChannels() != numAudioOuts)
        return false;

    if (getNumDspParameters() > 0
        && layouts.getNumChannels (true, 1) != getNumDspParameters())
        return false;

    return true;
}

void ProcessorModule::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int block = juce::jmax (1, samplesPerBlock);
    preparedBlockSize = block;

    // Erst Kapazitäten (wächst-nur bei Re-Prepare), dann ggf. Tap anlegen.
    taps.prepare (block);
    createTapIfWanted();

    smoothedInputGain.reset (sampleRate, 0.005);
    smoothedInputGain.setCurrentAndTargetValue (gainFromDb (inputGainDb.load (std::memory_order_relaxed)));
    smoothedOutputGain.reset (sampleRate, 0.005);
    smoothedOutputGain.setCurrentAndTargetValue (gainFromDb (outputGainDb.load (std::memory_order_relaxed)));

    inputMeter.prepare (sampleRate, numAudioIns);
    outputMeter.prepare (sampleRate, numAudioOuts);

    prepareCore (sampleRate, block);
}

void ProcessorModule::releaseResources()
{
    // Sink bleibt announced — das Modul ist weiterhin Teil des Patches.
    releaseCore();
}

//==============================================================================
void ProcessorModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Epoch-Handshake: Inkrement VOR dem Tap-Commit (seq_cst)
    taps.noteBlockBegin();

    const int numFrames   = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int coreChans   = juce::jmax (numAudioIns, numAudioOuts);
    auto* tap             = rtTap.load (std::memory_order_acquire);

    if (numFrames <= 0 || numChannels < coreChans)
    {
        // Degenerierter Buffer (Tests/Sonderfälle): kein Core-Lauf, Status halten.
        if (tap != nullptr)
            tap->noteIdle();

        return;
    }

    // 1) CV-Blockmittel des BETRAGS → blockkonstante Effektivwerte im
    //    Richtungs-Modell (4.6): die Gleichrichtung VOR der Mittelung macht
    //    bipolare Quellen (LFO-Sinus) zur Modulations-Hüllkurve, die
    //    Richtung bestimmt allein der Attenuverter. Unverbundene CV-Kanäle
    //    sind vom Graph genullt → keine Modulation.
    for (int i = 0; i < getNumDspParameters(); ++i)
    {
        const auto index    = static_cast<size_t> (i);
        const int cvChannel = getCvChannelFor (i);
        float cvMagnitude = 0.0f;

        if (cvChannel < numChannels)
        {
            const float* data = buffer.getReadPointer (cvChannel);
            float sum = 0.0f;

            for (int frame = 0; frame < numFrames; ++frame)
                sum += std::abs (data[frame]);

            cvMagnitude = sum / static_cast<float> (numFrames);
        }

        stage1[index] = ChassisSchema::computeEffective (
            dspBase[index].load (std::memory_order_relaxed), cvMagnitude,
            cvAmount[index].load (std::memory_order_relaxed),
            userRangeMin[index].load (std::memory_order_relaxed),
            userRangeMax[index].load (std::memory_order_relaxed));
    }

    // 1b) Control-Links (4.6, modulintern): Stufe 2 liest AUSSCHLIESSLICH
    //     Stufe-1-Werte — Zyklen (A↔B) sind dadurch harmlos, die
    //     Reihenfolge egal. Quelle normalisiert auf ihren User-Bereich.
    for (int i = 0; i < getNumDspParameters(); ++i)
    {
        const auto index  = static_cast<size_t> (i);
        const auto source = linkSource[index].load (std::memory_order_relaxed);

        if (source < 0 || source >= getNumDspParameters())
        {
            effective[index] = stage1[index];
            continue;
        }

        const auto sourceIndex = static_cast<size_t> (source);
        const auto srcMin      = userRangeMin[sourceIndex].load (std::memory_order_relaxed);
        const auto srcMax      = userRangeMax[sourceIndex].load (std::memory_order_relaxed);
        const auto srcSpan     = srcMax - srcMin;
        auto srcNorm           = srcSpan > 0.0f
                               ? juce::jlimit (0.0f, 1.0f, (stage1[sourceIndex] - srcMin) / srcSpan)
                               : 0.0f;

        // Optionale Link-Response-Kurve (z.B. Gain-Matching): formt die
        // normalisierte Quelle — pure Bezier-Auswertung, alloc-/lock-frei
        if (linkCurveOn[index].load (std::memory_order_relaxed))
        {
            const ChassisSchema::BezierCurve curve {
                linkCurveX1[index].load (std::memory_order_relaxed),
                linkCurveY1[index].load (std::memory_order_relaxed),
                linkCurveX2[index].load (std::memory_order_relaxed),
                linkCurveY2[index].load (std::memory_order_relaxed)
            };
            srcNorm = ChassisSchema::evaluateCurve (curve, srcNorm);
        }

        effective[index] = ChassisSchema::computeEffective (
            stage1[index], srcNorm,
            linkAmount[index].load (std::memory_order_relaxed),
            userRangeMin[index].load (std::memory_order_relaxed),
            userRangeMax[index].load (std::memory_order_relaxed));
    }

    // 2) Input-Gain → Input-Meter (post-Gain, Ableton-Semantik)
    smoothedInputGain.setTargetValue (gainFromDb (inputGainDb.load (std::memory_order_relaxed)));
    applyGainRamp (buffer, numAudioIns, numFrames, smoothedInputGain);
    inputMeter.process (buffer, numAudioIns);

    // 3) Modul-DSP auf der reinen Audio-Sicht (Kanäle 0..coreChans-1) —
    //    setDataToReferTo ist alloc-frei (preallocated channel space)
    for (int channel = 0; channel < coreChans; ++channel)
        audioViewPointers[static_cast<size_t> (channel)] = buffer.getWritePointer (channel);

    audioView.setDataToReferTo (audioViewPointers.data(), coreChans, numFrames);
    processCore (audioView, midiMessages);

    // 4) Output-Gain → Output-Meter
    smoothedOutputGain.setTargetValue (gainFromDb (outputGainDb.load (std::memory_order_relaxed)));
    applyGainRamp (buffer, numAudioOuts, numFrames, smoothedOutputGain);
    outputMeter.process (buffer, numAudioOuts);

    // 5) Link-Tap (post-Output-Gain): Stereo-Commit mit dem ClockState des
    //    Blocks; ohne Takt-Bus / bei zu großem Block announced bleiben.
    if (tap != nullptr)
    {
        if (clockBus != nullptr && numFrames <= preparedBlockSize && numAudioOuts >= 1)
        {
            const float* channelData[2] = {
                buffer.getReadPointer (0),
                buffer.getReadPointer (numAudioOuts > 1 ? 1 : 0)
            };
            tap->commit (channelData, numFrames, clockBus->current);
        }
        else
        {
            tap->noteIdle();
        }
    }
}

//==============================================================================
void ProcessorModule::setLinkAudioContext (LinkClock* clock, const juce::String& initialModuleId)
{
    taps.setLinkClock (clock);
    currentModuleId = initialModuleId;
}

void ProcessorModule::moduleIdRenamed (const juce::String& newModuleId)
{
    currentModuleId = newModuleId;

    if (auto* tap = rtTap.load (std::memory_order_relaxed))
        tap->setName (currentModuleId);
}

void ProcessorModule::releaseSessionResources()
{
    // Phase 1 (5.3): Audio-Thread sofort trennen, dann Sink retiren —
    // sonst Zombie-Kanäle bei den Peers (7.2).
    rtTap.store (nullptr, std::memory_order_release);
    taps.retireAll();
}

void ProcessorModule::setClockBus (const ClockBus* bus) noexcept
{
    clockBus = bus;
}

//==============================================================================
void ProcessorModule::setSendEnabled (bool shouldSend)
{
    if (shouldSend == sendWanted)
        return;

    sendWanted = shouldSend;

    if (shouldSend)
    {
        createTapIfWanted();
        return;
    }

    if (auto* tap = rtTap.exchange (nullptr, std::memory_order_acq_rel))
        taps.retireTap (tap);
}

void ProcessorModule::createTapIfWanted()
{
    if (! sendWanted || rtTap.load (std::memory_order_relaxed) != nullptr)
        return;

    const auto channelName = currentModuleId.isNotEmpty() ? currentModuleId : getModuleId();

    // nullptr ohne Link-Kontext (Tests) — prepareToPlay versucht es erneut.
    if (auto* tap = taps.createTap (channelName, 2))
        rtTap.store (tap, std::memory_order_release);
}

LinkSendTaps::Status ProcessorModule::getLinkSendStatus() const noexcept
{
    if (auto* tap = rtTap.load (std::memory_order_relaxed))
        return tap->getStatus();

    return LinkSendTaps::Status::offline;
}

juce::String ProcessorModule::getSendSinkName() const
{
    if (auto* tap = rtTap.load (std::memory_order_relaxed))
        return tap->getSinkName();

    return {};
}

bool ProcessorModule::isSinkRetirePending() const noexcept
{
    return taps.isRetirePending();
}

void ProcessorModule::flushPendingSinkRetirement()
{
    taps.flushPendingRetirement();
}

} // namespace conduit

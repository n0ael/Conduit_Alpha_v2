#include "StepSequencerModule.h"

#include <cmath>

#include "Util/ScaleQuantizer.h"

namespace conduit
{

StepSequencerModule::StepSequencerModule()
    : GeneratorModule (BusesProperties()
          .withOutput ("Out", juce::AudioChannelSet::discreteChannels (numRows * 2), true))
{
}

//==============================================================================
juce::String StepSequencerModule::getModuleId() const          { return staticModuleId; }
juce::String StepSequencerModule::getModuleDisplayName() const { return "Step-Sequencer"; }
int StepSequencerModule::getStateVersion() const               { return 1; }

juce::String StepSequencerModule::stepParameterId (int row, int column)
{
    jassert (juce::isPositiveAndBelow (row, numRows)
             && juce::isPositiveAndBelow (column, stepsPerRow));

    return juce::String::charToString (static_cast<juce::juce_wchar> ('a' + row))
           + juce::String (column + 1);
}

void StepSequencerModule::appendParametersTo (juce::ValueTree& parameters)
{
    // rate zuerst — der generische Node-Slider zeigt firstParameter
    parameters.appendChild (makeParameter ("rate",      2.0,  0.25, 8.0,  2.0),  nullptr);
    parameters.appendChild (makeParameter ("gate",      0.5,  0.05, 1.0,  0.5),  nullptr);
    parameters.appendChild (makeParameter ("swing",     0.0,  0.0,  0.75, 0.0),  nullptr);
    parameters.appendChild (makeParameter ("direction", 0.0,  0.0,  3.0,  0.0),  nullptr);
    parameters.appendChild (makeParameter ("mode",      0.0,  0.0,  2.0,  0.0),  nullptr);
    parameters.appendChild (makeParameter ("length",    16.0, 1.0,  16.0, 16.0), nullptr);
    parameters.appendChild (makeParameter ("prob",      1.0,  0.0,  1.0,  1.0),  nullptr);
    parameters.appendChild (makeParameter ("quantize",  0.0,  0.0,  1.0,  0.0),  nullptr);

    for (int row = 0; row < numRows; ++row)
        for (int column = 0; column < stepsPerRow; ++column)
            parameters.appendChild (makeParameter (stepParameterId (row, column),
                                                   0.0, 0.0, 1.0, 0.0), nullptr);
}

std::atomic<float>* StepSequencerModule::getParameterTarget (const juce::String& parameterId) noexcept
{
    if (parameterId == "rate")      return &rateTarget;
    if (parameterId == "gate")      return &gateTarget;
    if (parameterId == "swing")     return &swingTarget;
    if (parameterId == "direction") return &directionTarget;
    if (parameterId == "mode")      return &modeTarget;
    if (parameterId == "length")    return &lengthTarget;
    if (parameterId == "prob")      return &probTarget;
    if (parameterId == "quantize")  return &quantizeTarget;

    // Steps: 'a'–'d' + 1–16 (strikt, "a01"/"a17" → nullptr)
    if (parameterId.length() >= 2)
    {
        const auto rowChar = parameterId[0];

        if (rowChar >= 'a' && rowChar < 'a' + numRows)
        {
            const auto columnText = parameterId.substring (1);
            const auto column = columnText.getIntValue();

            if (column >= 1 && column <= stepsPerRow && columnText == juce::String (column))
                return &stepTargets[static_cast<size_t> ((rowChar - 'a') * stepsPerRow + (column - 1))];
        }
    }

    return nullptr;
}

//==============================================================================
void StepSequencerModule::setClockBus (const ClockBus* bus) noexcept
{
    clockBus = bus;
}

void StepSequencerModule::setRandomSeed (std::uint64_t seed) noexcept
{
    seedValue = seed != 0 ? seed : 1;
    random.setSeed (static_cast<juce::int64> (seedValue));
}

//==============================================================================
void StepSequencerModule::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    freeRunPosition = 0.0;
    chainStates.fill ({});
    random.setSeed (static_cast<juce::int64> (seedValue));
}

void StepSequencerModule::releaseResources()
{
}

void StepSequencerModule::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();  // ungenutzte Kanäle bleiben still, aktive überschreiben

    const auto rate        = static_cast<double> (rateTarget.load (std::memory_order_relaxed));
    const auto gateLength  = gateTarget.load (std::memory_order_relaxed);
    auto swing             = juce::jlimit (0.0, 0.75,
                                 static_cast<double> (swingTarget.load (std::memory_order_relaxed)));
    const auto direction   = juce::jlimit (0, 3, juce::roundToInt (directionTarget.load (std::memory_order_relaxed)));
    const auto mode        = juce::jlimit (0, 2, juce::roundToInt (modeTarget.load (std::memory_order_relaxed)));
    const auto length      = juce::jlimit (1, stepsPerRow, juce::roundToInt (lengthTarget.load (std::memory_order_relaxed)));
    const auto probability = probTarget.load (std::memory_order_relaxed);
    const auto quantizeOn  = quantizeTarget.load (std::memory_order_relaxed) > 0.5f;

    double blockStartPosition;     // Position in Steps (vor Swing-Verzerrung)
    double positionPerSample;
    int scaleRootNote = 0, scaleTypeIndex = 0;

    if (clockBus != nullptr)
    {
        // Beat-gelockt: reine Funktion des Session-Beats (phasenstarr)
        const auto& session = clockBus->current;
        blockStartPosition = session.beatAtBlockStart * rate;
        positionPerSample  = session.beatsPerSample() * rate;
        scaleRootNote      = session.scaleRootNote;
        scaleTypeIndex     = session.scaleTypeIndex;

        // Globaler Session-Swing (4.5): lokal 0 folgt dem Header-Regler,
        // lokal > 0 überschreibt für dieses Modul
        if (swing <= 0.0)
            swing = juce::jlimit (0.0, 0.75, session.globalSwing);
    }
    else
    {
        // Freilauf (Tests): rate als Steps/Sekunde
        blockStartPosition = freeRunPosition;
        positionPerSample  = currentSampleRate > 0.0 ? rate / currentSampleRate : 0.0;
        freeRunPosition   += positionPerSample * buffer.getNumSamples();
    }

    const auto rowsPerChain = mode == 0 ? 1 : (mode == 1 ? 2 : numRows);
    const auto numChains    = numRows / rowsPerChain;
    const auto chainLength  = length * rowsPerChain;

    for (int chain = 0; chain < numChains; ++chain)
        renderChain (buffer, chain, numChains, blockStartPosition, positionPerSample,
                     chainLength, direction, gateLength, probability,
                     quantizeOn, scaleRootNote, scaleTypeIndex, swing);
}

void StepSequencerModule::renderChain (juce::AudioBuffer<float>& buffer,
                                       int chainIndex, int numChains,
                                       double blockStartPosition, double positionPerSample,
                                       int chainLength, int direction, float gateLength,
                                       float probability, bool quantizeToScale,
                                       int scaleRootNote, int scaleTypeIndex, double swing)
{
    const auto rowsPerChain = numRows / numChains;
    const auto startRow     = chainIndex * rowsPerChain;
    const auto lengthPerRow = chainLength / rowsPerChain;
    const auto baseChannel  = startRow * 2;

    if (baseChannel + 1 >= buffer.getNumChannels())
        return;  // defensiv — Bus ist 8-kanalig konfiguriert

    auto* cvOut   = buffer.getWritePointer (baseChannel);
    auto* gateOut = buffer.getWritePointer (baseChannel + 1);

    auto& state = chainStates[static_cast<size_t> (chainIndex)];
    const auto firstHalf  = 1.0 + swing;   // Swing: Paar-Verzerrung der Zeitachse —
    const auto secondHalf = 1.0 - swing;   // ungerade Steps starten später, Summe bleibt 2

    int lastRow = startRow, lastColumn = 0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto rawPosition = blockStartPosition + i * positionPerSample;
        const auto pairIndex = std::floor (rawPosition / 2.0);
        const auto positionInPair = rawPosition - pairIndex * 2.0;

        std::int64_t globalStep;
        double fractionInStep;

        if (positionInPair < firstHalf)
        {
            globalStep = static_cast<std::int64_t> (pairIndex) * 2;
            fractionInStep = positionInPair / firstHalf;
        }
        else
        {
            globalStep = static_cast<std::int64_t> (pairIndex) * 2 + 1;
            fractionInStep = (positionInPair - firstHalf) / secondHalf;
        }

        if (globalStep != state.lastGlobalStep)
        {
            // Step-Eintritt: Trigger-Würfel (prob) + ggf. neuer Random-Index.
            // Eigene RNG-Instanz, deterministisch je Seed (IStochastic).
            state.lastGlobalStep = globalStep;
            state.triggerArmed = probability >= 1.0f || random.nextFloat() < probability;

            if (direction == 3)
                state.randomIndex = random.nextInt (chainLength);
        }

        const auto stepInChain = static_cast<int> (((globalStep % chainLength) + chainLength)
                                                   % chainLength);
        int cellIndex;

        switch (direction)
        {
            case 1:  cellIndex = chainLength - 1 - stepInChain; break;

            case 2:
            {
                // Pendulum: Endpunkte nicht doppelt (Periode 2·len−2)
                const auto period = juce::jmax (1, 2 * chainLength - 2);
                const auto k = static_cast<int> (((globalStep % period) + period) % period);
                cellIndex = k < chainLength ? k : period - k;
                break;
            }

            case 3:  cellIndex = state.randomIndex; break;
            default: cellIndex = stepInChain; break;
        }

        const auto row    = startRow + cellIndex / lengthPerRow;
        const auto column = cellIndex % lengthPerRow;

        auto cv = stepTargets[static_cast<size_t> (row * stepsPerRow + column)]
                      .load (std::memory_order_relaxed);

        if (quantizeToScale)
            cv = scale::quantize (cv, scaleRootNote,
                                  static_cast<ScaleType> (scale::clampedIndex (scaleTypeIndex)));

        cvOut[i]   = cv;
        gateOut[i] = (state.triggerArmed && fractionInStep < gateLength) ? 1.0f : 0.0f;

        lastRow = row;
        lastColumn = column;
    }

    if (chainIndex == 0)
        currentCellForUi.store (lastRow * stepsPerRow + lastColumn, std::memory_order_relaxed);
}

} // namespace conduit

#include "dsp/SignalChain.h"

namespace pedal
{

void SignalChain::prepare (double sampleRate, int maxBlockSize)
{
    const juce::dsp::ProcessSpec monoSpec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    monoBuffer.setSize (1, maxBlockSize);
    cleanBuffer.setSize (1, maxBlockSize);

    gate.prepare (monoSpec);
    gate.setRatio (10.0f);
    gate.setAttack (1.0f);
    gate.setRelease (80.0f);

    cabinet.prepare (monoSpec);

    eqSampleRate = sampleRate;
    using EqCoeffs = juce::dsp::IIR::Coefficients<float>;
    bass.coefficients = new EqCoeffs (juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, 120.0f, 0.7f, 1.0f));
    middle.coefficients = new EqCoeffs (juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sampleRate, 700.0f, 0.8f, 1.0f));
    treble.coefficients = new EqCoeffs (juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, 3500.0f, 0.7f, 1.0f));
    for (auto* f : { &bass, &middle, &treble })
    {
        f->prepare (monoSpec);
        f->reset();
    }
    std::fill (std::begin (eqState), std::end (eqState), 0.0f);
    dropTuner.prepare (sampleRate, maxBlockSize);
    amp.prepare (sampleRate, maxBlockSize);

    for (auto* s : { &inputGain, &outputGain, &blend })
        s->reset (sampleRate, 0.02);
}

void SignalChain::reset()
{
    gate.reset();
    cabinet.reset();
    for (auto* f : { &bass, &middle, &treble })
        f->reset();
    dropTuner.reset();
}

void SignalChain::loadCabinet (const juce::File& wavFile)
{
    cabinet.loadImpulseResponse (wavFile, juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::yes, 0,
                                 juce::dsp::Convolution::Normalise::yes);
    cabLoaded = true;
}

void SignalChain::clearCabinet()
{
    cabLoaded = false;
}

void SignalChain::updateEq (const ChainSettings& s) noexcept
{
    using Array = juce::dsp::IIR::ArrayCoefficients<float>;
    const float wanted[3] = { s.bassDb, s.middleDb, s.trebleDb };

    // Assigning a same-size array reuses the coefficient storage, so this never allocates.
    if (! juce::approximatelyEqual (wanted[0], eqState[0]))
        *bass.coefficients = Array::makeLowShelf (eqSampleRate, 120.0f, 0.7f, juce::Decibels::decibelsToGain (wanted[0]));
    if (! juce::approximatelyEqual (wanted[1], eqState[1]))
        *middle.coefficients = Array::makePeakFilter (eqSampleRate, 700.0f, 0.8f, juce::Decibels::decibelsToGain (wanted[1]));
    if (! juce::approximatelyEqual (wanted[2], eqState[2]))
        *treble.coefficients = Array::makeHighShelf (eqSampleRate, 3500.0f, 0.7f, juce::Decibels::decibelsToGain (wanted[2]));

    std::copy (wanted, wanted + 3, eqState);
}

void SignalChain::process (juce::AudioBuffer<float>& buffer, const ChainSettings& s) noexcept
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || buffer.getNumChannels() == 0 || numSamples > monoBuffer.getNumSamples())
        return;

    auto* mono = monoBuffer.getWritePointer (0);
    std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + numSamples, mono);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (s.inputGainDb));
    inputGain.applyGain (mono, numSamples);

    juce::dsp::AudioBlock<float> block (monoBuffer.getArrayOfWritePointers(), 1, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);

    if (s.gateThresholdDb > -99.0f)
    {
        gate.setThreshold (s.gateThresholdDb);
        gate.process (context);
    }

    dropTuner.setSemitones (s.dropSemitones);
    dropTuner.process (mono, numSamples);

    auto* clean = cleanBuffer.getWritePointer (0);
    std::copy (mono, mono + numSamples, clean);

    amp.process (mono, numSamples);

    if (s.cabEnabled && cabLoaded.load())
        cabinet.process (context);

    if (s.eqEnabled)
    {
        updateEq (s);
        bass.process (context);
        middle.process (context);
        treble.process (context);
    }

    blend.setTargetValue (juce::jlimit (0.0f, 1.0f, s.cleanBlend));
    for (int i = 0; i < numSamples; ++i)
    {
        const float b = blend.getNextValue();
        mono[i] = mono[i] * (1.0f - b) + clean[i] * b;
    }

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (s.outputGainDb));
    outputGain.applyGain (mono, numSamples);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, mono, numSamples);
}

} // namespace pedal

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

    limiter.prepare (monoSpec);
    limiter.setThreshold (outputCeilingDb);
    limiter.setRelease (100.0f);

    cabinet.prepare (monoSpec);
    dropTuner.prepare (sampleRate, maxBlockSize);
    amp.prepare (sampleRate, maxBlockSize);

    for (auto* s : { &inputGain, &outputGain, &blend })
        s->reset (sampleRate, 0.02);
}

void SignalChain::reset()
{
    gate.reset();
    limiter.reset();
    cabinet.reset();
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

    dropTuner.setEnabled (s.pitchOn);
    dropTuner.setSemitones (s.pitchSemitones);
    dropTuner.setBuffer (s.pitchBuffer);
    dropTuner.setTonality (s.pitchTonalityHz);
    dropTuner.process (mono, numSamples);

    auto* clean = cleanBuffer.getWritePointer (0);
    std::copy (mono, mono + numSamples, clean);

    amp.process (mono, numSamples);

    if (s.cabEnabled && cabLoaded.load())
        cabinet.process (context);

    blend.setTargetValue (juce::jlimit (0.0f, 1.0f, s.cleanBlend));
    for (int i = 0; i < numSamples; ++i)
    {
        const float b = blend.getNextValue();
        mono[i] = mono[i] * (1.0f - b) + clean[i] * b;
    }

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (s.outputGainDb));
    outputGain.applyGain (mono, numSamples);

    // juce's Limiter raises its threshold to 0 dBFS and clips there; scale
    // back so the level below the ceiling stays as it was.
    limiter.process (context);
    juce::FloatVectorOperations::multiply (mono, juce::Decibels::decibelsToGain (outputCeilingDb), numSamples);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, mono, numSamples);
}

} // namespace pedal

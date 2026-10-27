#pragma once

#include <juce_dsp/juce_dsp.h>

#include "dsp/AmpModel.h"
#include "dsp/DropTuner.h"

namespace pedal
{

// Everything the chain needs for one block. The processor fills this from its
// parameters; tests fill it by hand.
struct ChainSettings
{
    float inputGainDb = 0.0f;
    float gateThresholdDb = -100.0f; // -100 = gate off
    int dropSemitones = 0;
    float cleanBlend = 0.0f;         // 0..1, mostly for bass: keeps the low end clean
    float outputGainDb = 0.0f;
    bool cabEnabled = true;
    bool eqEnabled = true;
    float bassDb = 0.0f, middleDb = 0.0f, trebleDb = 0.0f; // -12..+12
};

// Signal flow (mono inside, copied to every output channel):
//   input gain -> gate -> drop tuner -> NAM amp -> cab IR -> EQ -> clean blend -> output gain
class SignalChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const ChainSettings& settings) noexcept;

    AmpModel& getAmp() noexcept { return amp; }
    const AmpModel& getAmp() const noexcept { return amp; }
    DropTuner& getDropTuner() noexcept { return dropTuner; }

    // Loads a cabinet impulse response. Safe from the message thread; the
    // convolution swaps it in on its own background thread.
    void loadCabinet (const juce::File& wavFile);
    void clearCabinet();
    bool hasCabinet() const noexcept { return cabLoaded.load(); }

private:
    AmpModel amp;
    DropTuner dropTuner;
    juce::dsp::NoiseGate<float> gate;
    juce::dsp::Convolution cabinet { juce::dsp::Convolution::NonUniform { 256 } };

    // Bass / middle / treble. Coefficients are recomputed in place (no
    // allocation) only when a knob moves.
    void updateEq (const ChainSettings&) noexcept;
    juce::dsp::IIR::Filter<float> bass, middle, treble;
    float eqState[3] { 1000.0f, 1000.0f, 1000.0f }; // last applied dB values
    double eqSampleRate = 48000.0;
    juce::AudioBuffer<float> monoBuffer, cleanBuffer;
    juce::SmoothedValue<float> inputGain, outputGain, blend;
    std::atomic<bool> cabLoaded { false };
};

} // namespace pedal

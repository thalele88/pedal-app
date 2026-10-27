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
    bool pitchOn = false;
    float pitchSemitones = 0.0f;     // already rounded when STEP is on
    DropTuner::Buffer pitchBuffer = DropTuner::Buffer::fast;
    float pitchTonalityHz = 0.0f;    // 0 = shift everything
    float cleanBlend = 0.0f;         // 0..1, mostly for bass: keeps the low end clean
    float outputGainDb = 0.0f;
    bool cabEnabled = true;
};

// Signal flow (mono inside, copied to every output channel):
//   input gain -> gate -> drop tuner -> NAM amp -> cab IR -> clean blend -> output gain -> limiter
//
// The limiter is a safety net for ears and speakers: whatever a capture or a
// gain setting does, the output never goes above outputCeilingDb.
class SignalChain
{
public:
    static constexpr float outputCeilingDb = -1.0f;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const ChainSettings& settings) noexcept;

    AmpModel& getAmp() noexcept { return amp; }
    const AmpModel& getAmp() const noexcept { return amp; }
    DropTuner& getDropTuner() noexcept { return dropTuner; }
    const DropTuner& getDropTuner() const noexcept { return dropTuner; }

    // Loads a cabinet impulse response. Safe from the message thread; the
    // convolution swaps it in on its own background thread.
    void loadCabinet (const juce::File& wavFile);
    void clearCabinet();
    bool hasCabinet() const noexcept { return cabLoaded.load(); }

private:
    AmpModel amp;
    DropTuner dropTuner;
    juce::dsp::NoiseGate<float> gate;
    juce::dsp::Limiter<float> limiter;
    juce::dsp::Convolution cabinet { juce::dsp::Convolution::NonUniform { 256 } };
    juce::AudioBuffer<float> monoBuffer, cleanBuffer;
    juce::SmoothedValue<float> inputGain, outputGain, blend;
    std::atomic<bool> cabLoaded { false };
};

} // namespace pedal

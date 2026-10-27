#include "dsp/DropTuner.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace pedal
{

struct DropTuner::Impl
{
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    juce::HeapBlock<float> wet;
    int maxBlock = 0;
    int currentSemitones = 0;
    // 0 = dry, 1 = shifted. Ramped per sample to avoid clicks.
    juce::SmoothedValue<float> mix;
};

DropTuner::DropTuner() : impl (std::make_unique<Impl>()) {}
DropTuner::~DropTuner() = default;

void DropTuner::prepare (double sampleRate, int maxBlockSize)
{
    // A much shorter window than the library default (120 ms) so the latency
    // stays playable live (about 30 ms here). Shorter windows smear low bass
    // notes, so these two numbers need tuning with a real guitar and bass.
    const int blockSamples = (int) (sampleRate * 0.03);
    const int intervalSamples = (int) (sampleRate * 0.0075);
    impl->stretch.configure (1, blockSamples, intervalSamples, false);

    impl->wet.allocate ((size_t) maxBlockSize, true);
    impl->maxBlock = maxBlockSize;
    impl->mix.reset (sampleRate, 0.03);
    reset();
}

void DropTuner::reset()
{
    impl->stretch.reset();
    impl->currentSemitones = targetSemitones.load();
    impl->stretch.setTransposeSemitones ((float) impl->currentSemitones);
    impl->mix.setCurrentAndTargetValue (impl->currentSemitones != 0 ? 1.0f : 0.0f);
}

void DropTuner::process (float* samples, int numSamples) noexcept
{
    if (numSamples > impl->maxBlock)
        return;

    const int target = targetSemitones.load();
    const bool wantOn = target != 0;

    if (wantOn && target != impl->currentSemitones)
    {
        impl->currentSemitones = target;
        impl->stretch.setTransposeSemitones ((float) target);
    }
    impl->mix.setTargetValue (wantOn ? 1.0f : 0.0f);

    const bool fullyOff = ! wantOn && ! impl->mix.isSmoothing() && impl->mix.getCurrentValue() <= 0.0f;
    if (fullyOff)
        return;

    float* in[1] = { samples };
    float* out[1] = { impl->wet.get() };
    impl->stretch.process (in, numSamples, out, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = impl->mix.getNextValue();
        samples[i] = samples[i] * (1.0f - m) + impl->wet[i] * m;
    }
}

int DropTuner::getLatencySamples() const noexcept
{
    return impl->stretch.inputLatency() + impl->stretch.outputLatency();
}

} // namespace pedal

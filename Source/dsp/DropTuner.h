#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>

namespace pedal
{

// Polyphonic pitch shifter used to drop the tuning (for example -2 semitones
// for a whole step down) without retuning the instrument.
//
// When the shift is 0 the signal bypasses the shifter entirely, so there is no
// extra latency in standard tuning. Turning it on or off crossfades.
class DropTuner
{
public:
    DropTuner();
    ~DropTuner();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Safe to call from any thread.
    void setSemitones (int semitones) noexcept { targetSemitones.store (semitones); }

    // Mono, in place.
    void process (float* samples, int numSamples) noexcept;

    // Delay added by the shifter while it is on, in samples.
    int getLatencySamples() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<int> targetSemitones { 0 };
};

} // namespace pedal

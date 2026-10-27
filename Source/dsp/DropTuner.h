#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>

namespace pedal
{

// Polyphonic pitch shifter used to drop the tuning (for example -2 semitones
// for a whole step down) without retuning the instrument. The controls follow
// the Pitch block of the TONE3000 plugin: power, a shift of up to two octaves
// either way (whole semitones or continuous), Tonality and Buffer.
//
// Buffer picks the engine:
//   - fast:   SpliceShifter, time domain. About 7 to 10 ms of delay on single
//             notes, 15 ms on chords and 19 ms on bass; keeps attacks sharp
//             and low notes clean. The delay moves a little all the time,
//             which can be heard as a slight flutter on some big chords.
//   - ms20 .. ms60: Signalsmith Stretch, frequency domain, with that much
//             delay. The windows are asymmetric, so each one is about 3.3x
//             longer than its delay, which keeps low notes much cleaner than
//             a plain window of the same delay. Longer = cleaner.
//
// Tonality (Signalsmith buffers only) leaves everything above a frequency
// unshifted, which keeps the pick attack bright.
//
// When it is off the signal bypasses the shifter entirely, so there is no
// extra latency. Turning it on or off, and switching buffers while it is on,
// crossfades once the new engine has filled up.
class DropTuner
{
public:
    enum class Buffer { fast = 0, ms20, ms30, ms40, ms60 };
    static constexpr int numBuffers = 5;

    static constexpr double fastLatencyMinMs = 7.0, fastLatencyMaxMs = 20.0;

    DropTuner();
    ~DropTuner();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Safe to call from any thread.
    void setEnabled (bool shouldBeOn) noexcept { targetEnabled.store (shouldBeOn); }
    void setSemitones (float semitones) noexcept { targetSemitones.store (semitones); }
    void setBuffer (Buffer buffer) noexcept { targetBuffer.store (buffer); }
    // Highest frequency that gets shifted, in Hz; 0 shifts everything.
    void setTonality (float hz) noexcept { targetTonality.store (hz); }

    // Mono, in place.
    void process (float* samples, int numSamples) noexcept;

    // Delay added while the shifter is on, in samples. For the fast buffer
    // this is the typical value; the real delay moves between about
    // fastLatencyMinMs and fastLatencyMaxMs.
    int getLatencySamples() const noexcept;
    int getLatencySamples (Buffer buffer) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<bool> targetEnabled { false };
    std::atomic<float> targetSemitones { 0.0f };
    std::atomic<Buffer> targetBuffer { Buffer::fast };
    std::atomic<float> targetTonality { 0.0f };
};

} // namespace pedal

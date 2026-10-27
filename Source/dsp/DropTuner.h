#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>

namespace pedal
{

// Polyphonic pitch shifter used to drop the tuning (for example -2 semitones
// for a whole step down) without retuning the instrument.
//
// Two engines, chosen with setMode():
//   - fast:   SpliceShifter, time domain. About 7 to 10 ms of delay on single
//             notes, 15 ms on chords and 19 ms on bass; keeps attacks sharp
//             and low notes clean. The delay moves a little all the time,
//             which can be heard as a slight flutter on some big chords.
//   - smooth: Signalsmith Stretch, frequency domain, with asymmetric windows
//             so a 60 ms window costs only about 18 ms of delay. No flutter,
//             but attacks and low notes get a bit blurred.
//
// When the shift is 0 the signal bypasses the shifter entirely, so there is no
// extra latency in standard tuning. Turning it on or off, and switching
// engines while it is on, crossfades once the new engine has filled up.
class DropTuner
{
public:
    enum class Mode { fast = 0, smooth = 1 };

    static constexpr double fastLatencyMinMs = 7.0, fastLatencyMaxMs = 20.0;

    DropTuner();
    ~DropTuner();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Safe to call from any thread.
    void setSemitones (int semitones) noexcept { targetSemitones.store (semitones); }
    void setMode (Mode mode) noexcept { targetMode.store (mode); }

    // Mono, in place.
    void process (float* samples, int numSamples) noexcept;

    // Delay added while the shifter is on, in samples, for the mode and shift
    // that were last set. For the fast mode this is the typical value; the
    // real delay moves between about fastLatencyMinMs and fastLatencyMaxMs.
    int getLatencySamples() const noexcept;
    int getLatencySamples (Mode mode, int semitones) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<int> targetSemitones { 0 };
    std::atomic<Mode> targetMode { Mode::fast };
};

} // namespace pedal

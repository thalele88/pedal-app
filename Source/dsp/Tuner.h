#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace pedal
{

// Carries the input signal from the audio thread to the tuner screen.
//
// Threads:
//   - prepare() runs before playback starts (it allocates).
//   - push() runs on the audio thread: it low-passes, decimates to about
//     12 kHz and writes into a lock-free FIFO. Nothing is allocated or locked.
//   - pull() runs on the UI thread.
//
// Pushing only happens while the tuner is open (setActive), so the FIFO never
// fills up with stale audio while nobody is looking.
class TunerInput
{
public:
    void prepare (double sampleRate);

    void setActive (bool shouldBeActive) noexcept { active.store (shouldBeActive); }
    bool isActive() const noexcept { return active.load(); }

    void push (const float* samples, int numSamples) noexcept;

    // Copies up to maxSamples decimated samples into dest; returns how many.
    int pull (float* dest, int maxSamples) noexcept;

    // Sample rate of the decimated signal handed out by pull().
    double getOutputSampleRate() const noexcept { return outputRate.load(); }

private:
    static constexpr int fifoSize = 8192;
    juce::AbstractFifo fifo { fifoSize };
    juce::HeapBlock<float> ring { (size_t) fifoSize, true };

    std::atomic<bool> active { false };
    std::atomic<double> outputRate { 12000.0 };
    int factor = 4, phase = 0;
    float lp1 = 0.0f, lp2 = 0.0f, lpCoeff = 0.2f;
};

// Finds the fundamental of a mono signal with the YIN method. Works from low
// B on a 5-string bass (31 Hz) up to the high frets of a guitar; open strings
// read within 1 cent, notes around 1 kHz within about 2 cents.
class PitchDetector
{
public:
    // Returns the frequency in Hz, or 0 when no clear pitch is found.
    // `work` must hold at least numSamples floats (no allocation inside).
    static float detect (const float* samples, int numSamples, double sampleRate, float* work) noexcept;
};

struct NoteReading
{
    int midiNote = -1;     // -1 = no note
    float cents = 0.0f;    // -50..+50 from midiNote
    float frequency = 0.0f;

    bool isValid() const noexcept { return midiNote >= 0; }
    juce::String getName() const;   // "E", "F#"...
    int getOctave() const noexcept { return midiNote / 12 - 1; }

    static NoteReading fromFrequency (float hz, float referenceA4 = 440.0f) noexcept;
};

} // namespace pedal

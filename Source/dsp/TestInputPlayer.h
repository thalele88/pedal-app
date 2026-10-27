#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace pedal
{

// Plays a recorded clean guitar/bass track (DI) in a loop in place of the
// live input, so the chain can be heard without an audio interface.
//
// Same threading rules as AmpModel: load() and collectGarbage() off the audio
// thread, process() on it without allocating or locking.
class TestInputPlayer
{
public:
    TestInputPlayer();
    ~TestInputPlayer();

    void prepare (double sampleRate);

    // Reads a WAV/AIFF/FLAC file (mixed to mono). Empty string on success.
    juce::String load (const juce::File& file);
    void collectGarbage();

    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay); }
    bool isPlaying() const noexcept { return playing.load(); }
    bool hasClip() const noexcept { return clipLoaded.load(); }
    juce::File getFile() const;

    // When playing, overwrites every channel of the buffer with the clip.
    void process (juce::AudioBuffer<float>& buffer) noexcept;

private:
    struct Clip
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 48000.0;
    };

    std::unique_ptr<Clip> active;            // audio thread only
    std::atomic<Clip*> incoming { nullptr }; // loader -> audio thread
    std::atomic<Clip*> retired { nullptr };  // audio thread -> collector

    double hostSampleRate = 48000.0;
    double position = 0.0; // audio thread only, in clip samples
    std::atomic<bool> playing { false };
    std::atomic<bool> clipLoaded { false };

    mutable std::mutex fileMutex;
    juce::File file;
};

} // namespace pedal

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace nam { class DSP; }

namespace pedal
{

// A NAM model that has been read, built and prewarmed off the audio thread,
// ready for AmpModel::install(). Building is the slow part of a load, so the
// preset system builds these ahead of time and a preset switch is only a
// pointer swap.
class LoadedModel
{
public:
    ~LoadedModel();
    const juce::String& getName() const noexcept { return name; }

private:
    friend class AmpModel;
    LoadedModel();

    std::unique_ptr<nam::DSP> dsp;
    juce::String name;
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
};

// Holds one NAM model and swaps it in without blocking the audio thread.
//
// Threads:
//   - load() runs on any non-audio thread (it reads the file and allocates).
//   - process() runs on the audio thread and never allocates or locks.
//   - collectGarbage() runs on a non-audio thread and frees replaced models.
//
// A new model is handed to the audio thread through an atomic pointer and
// crossfaded with the old one over a single block, so switching presets
// on stage does not click.
class AmpModel
{
public:
    AmpModel();
    ~AmpModel();

    void prepare (double sampleRate, int maxBlockSize);

    // Loads a .nam file. Returns an empty string on success, or the error.
    juce::String load (const juce::File& namFile);
    // Same as load() but from the JSON text of a .nam file (used for downloads).
    juce::String loadFromJson (const juce::String& namJson, const juce::String& displayName);

    // The two halves of a load, for callers that build models ahead of time.
    // build*() run on any non-audio thread and return nullptr with `error` set
    // on failure. install() is cheap when the model was built at the current
    // sample rate and block size; otherwise it prewarms the model again.
    std::unique_ptr<LoadedModel> build (const juce::File& namFile, juce::String& error) const;
    std::unique_ptr<LoadedModel> buildFromJson (const juce::String& namJson, const juce::String& displayName,
                                                juce::String& error) const;
    juce::String install (std::unique_ptr<LoadedModel> model);

    // Fades out the current model; the signal then passes through untouched.
    void unload();

    // Mono, in place. With no model loaded the signal passes through untouched.
    void process (float* samples, int numSamples) noexcept;

    void collectGarbage();

    juce::String getModelName() const;
    bool hasModel() const noexcept { return modelLoaded.load(); }
    // True when the model was trained at a sample rate different from the
    // current one and cannot adapt to it (it will sound slightly off).
    bool hasSampleRateMismatch() const noexcept { return sampleRateMismatch.load(); }

private:
    std::unique_ptr<LoadedModel> finishBuild (std::unique_ptr<nam::DSP> model, const juce::String& name,
                                              juce::String& error) const;

    std::unique_ptr<nam::DSP> active;           // audio thread only
    std::atomic<nam::DSP*> incoming { nullptr }; // loader -> audio thread
    std::atomic<nam::DSP*> retired { nullptr };  // audio thread -> collector
    std::atomic<bool> unloadRequested { false };  // loader -> audio thread

    mutable std::mutex loadMutex; // serialises loaders and prepare(), never taken by audio
    double sampleRate = 48000.0;
    int maxBlockSize = 512;

    // NAM reads from one buffer and writes to another, so we keep two scratch
    // buffers: one for the current model and one for the model fading out.
    juce::HeapBlock<float> outBuffer, fadeBuffer;
    int scratchSize = 0;

    mutable std::mutex nameMutex;
    juce::String modelName;
    std::atomic<bool> modelLoaded { false };
    std::atomic<bool> sampleRateMismatch { false };
};

} // namespace pedal

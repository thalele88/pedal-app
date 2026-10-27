#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <mutex>

namespace pedal::tone3000
{

// Disk copies of TONE3000 models so presets still load with no internet
// (the venue Wi-Fi will fail at some point).
//
// TONE3000 does not allow caching or preloading the catalog, so only models
// that a preset (or the tone currently on the amp) uses are kept. Each user of
// the cache is an "owner" ("presets", "current", ...) that declares the models
// it still needs; a file nobody needs is deleted right away.
//
// Thread-safe. Reads and writes files, so never call it on the audio thread.
class ModelCache
{
public:
    explicit ModelCache (juce::File directory = defaultDirectory());

    // <user app data>/Pedal App/TONE3000/models
    static juce::File defaultDirectory();

    // The .nam JSON of a cached model, or an empty string.
    juce::String read (const juce::String& modelId) const;

    // Stores a model and records that `owner` needs it.
    bool write (const juce::String& owner, const juce::String& modelId, const juce::String& namJson);

    // Records that `owner` needs a model that is already on disk. False if
    // the model is not cached.
    bool claim (const juce::String& owner, const juce::String& modelId);

    // Replaces the list of models `owner` needs and deletes every file that no
    // owner needs any more.
    void retain (const juce::String& owner, const juce::StringArray& modelIds);

    juce::StringArray getCachedModelIds() const;

private:
    juce::File fileFor (const juce::String& modelId) const;
    void loadIndex();
    void saveIndex() const;
    void deleteUnowned();

    const juce::File directory;
    mutable std::mutex mutex;
    std::map<juce::String, juce::StringArray> owners;
};

} // namespace pedal::tone3000

#pragma once

#include <juce_events/juce_events.h>

#include "dsp/AmpModel.h"
#include "presets/ToneRef.h"

#include <functional>
#include <map>

namespace pedal
{

// Turns a ToneRef into the text of a .nam file. Runs on a background thread
// and may block on the disk or the network. Returns an empty string on
// success or the error. The TONE3000 module provides this for tone3000 refs.
using ToneFetcher = std::function<juce::String (const ToneRef&, juce::String& namJsonOut)>;

// Keeps amp models for the current setlist built and ready in memory, so a
// preset switch on stage installs a model instantly instead of waiting for
// the disk, the network or the model build.
//
// For every preloaded tone the bank holds the model text plus one ready
// LoadedModel. Selecting a tone hands that model to the AmpModel and builds a
// fresh spare in the background from the text, so the same tone can be
// selected again right away. Nothing here runs on the audio thread.
class ModelBank
{
public:
    explicit ModelBank (AmpModel& amp);
    ~ModelBank();

    void setToneFetcher (ToneFetcher);

    // Message thread. Keeps these tones ready and forgets the others.
    // Returns at once; the work happens in the background.
    void preload (const std::vector<ToneRef>& tones);

    // Message thread. Puts `tone` in the amp (an empty tone unloads it).
    // Preloaded tones are installed before this returns; others load in the
    // background. When several selections overlap, the last one wins.
    // `onDone` runs on the message thread with an empty string or the error.
    void select (const ToneRef& tone, std::function<void (juce::String)> onDone = {});

    struct Status
    {
        int ready = 0;   // built and waiting in memory
        int total = 0;   // asked for in preload()
        int failed = 0;
        bool busy = false;
    };
    Status getStatus() const;
    bool isReady (const ToneRef&) const;
    // Why a preloaded tone failed, or empty.
    juce::String getError (const ToneRef&) const;

private:
    struct Entry
    {
        ToneRef ref;
        juce::String json;                  // .nam text, kept so spares rebuild offline
        std::unique_ptr<LoadedModel> spare; // ready to install
        juce::String error;
        bool wanted = false;                // in the last preload() list
        bool building = false;
    };

    // Background thread: makes sure `key` has text and a spare.
    void prepareEntry (const juce::String& key);
    // Background thread: reads the model text for `ref`.
    juce::String fetch (const ToneRef& ref, juce::String& json);
    void schedulePrepare (const juce::String& key);

    AmpModel& amp;
    mutable std::mutex mutex; // guards everything below; never held while building or fetching
    ToneFetcher fetcher;
    std::map<juce::String, Entry> entries;
    std::mutex installMutex; // makes "is this still the latest selection?" + install atomic
    std::atomic<int> selectGeneration { 0 };

    // Separate threads so a show-time selection never queues behind a long preload.
    juce::ThreadPool pool { 1 }, selectPool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE (ModelBank)
};

} // namespace pedal

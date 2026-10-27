#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/SignalChain.h"
#include "presets/ModelBank.h"
#include "presets/PresetLibrary.h"
#include "ui/PresetSource.h"

namespace pedal
{

// Glue between the preset files, the plugin parameters and the signal chain.
//
// It shows one list of presets: the active setlist in order, or the whole
// library when no setlist is active. Selecting a preset sets the knobs, puts
// its amp capture in the AmpModel through the ModelBank (instant when the
// capture is preloaded) and loads its cabinet IR.
//
// The models of every preset in the active setlist (or the favourites when no
// setlist is active) are kept ready in memory, so a show does not depend on
// the disk or the network between songs.
//
// Everything runs on the message thread except handleMidi(), which the audio
// thread calls and which only stores the request for the timer to act on.
class PresetManager : public ui::PresetSource,
                      private juce::Timer
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& parameters, SignalChain& chain,
                   const juce::File& libraryFolder = PresetLibrary::getDefaultFolder());
    ~PresetManager() override;

    // ui::PresetSource
    int getNumPresets() const override;
    ui::PresetInfo getPresetInfo (int index) const override;
    int getCurrentIndex() const override { return currentIndex; }
    void selectPreset (int index) override;
    juce::String getListName() const override;
    bool canSave() const override { return getCurrentPreset() != nullptr; }
    void saveCurrent() override;
    void saveCurrentAs (const juce::String& name) override;
    void setFavourite (int index, bool favourite) override;
    bool isModified() const override;

    // Footswitch / stage buttons. They stop at the ends of the list.
    void next();
    void previous();
    const Preset* getPreset (int index) const;
    const Preset* getCurrentPreset() const;

    PresetLibrary& getLibrary() noexcept { return library; }
    // Empty id = whole library.
    void setActiveSetlist (const juce::String& setlistId);
    const Setlist* getActiveSetlist() const;
    // Call after changing the library or a setlist through getLibrary(), so the
    // list, the preloaded models and the offline cache follow.
    void libraryChanged();
    ModelBank::Status getPreloadStatus() const { return bank.getStatus(); }

    // Amp and cabinet loaded by hand (file chooser, TONE3000 picker). Saving a
    // preset captures whatever is loaded now.
    void setToneFetcher (ToneFetcher);
    // Called with the TONE3000 modelIds still used by a preset, so the TONE3000
    // module can drop every other preset model from its offline cache.
    void setOfflineCacheHandler (std::function<void (const juce::StringArray& modelIds)>);
    void loadTone (const ToneRef&, std::function<void (juce::String)> onDone = {});
    void loadIr (const juce::File&);
    void clearIr();
    const ToneRef& getCurrentTone() const noexcept { return currentTone; }
    juce::File getCurrentIr() const { return currentIr; }
    // Last load or save problem, in Portuguese, for the status line. Empty if none.
    juce::String getLastError() const { return lastError; }

    // Audio thread. Program Change n selects entry n of the list (0-based).
    void handleMidi (const juce::MidiBuffer&) noexcept;
    // 1..16, or 0 to listen on every channel.
    void setMidiChannel (int channel) { midiChannel = juce::jlimit (0, 16, channel); }
    int getMidiChannel() const noexcept { return midiChannel.load(); }

    // Saved inside the plugin state. setState() expects the parameters to be
    // restored already; it reloads the amp and IR without touching the knobs.
    juce::ValueTree getState() const;
    void setState (const juce::ValueTree&);

    static constexpr int maxPreloadedTones = 64;

private:
    void timerCallback() override;
    void rebuildList();
    void updatePreload();
    void applyPreset (const Preset&);
    void rememberLoaded (const Preset&);
    Preset captureCurrent() const;
    void setError (const juce::String&);

    juce::AudioProcessorValueTreeState& parameters;
    SignalChain& chain;
    PresetLibrary library;
    ModelBank bank;

    std::vector<juce::String> listIds; // preset id per visible row
    juce::String activeSetlistId, currentPresetId;
    int currentIndex = -1;
    std::optional<Preset> loadedSnapshot; // the preset as last loaded or saved

    ToneRef currentTone;
    juce::File currentIr;
    juce::String lastError;
    std::function<void (const juce::StringArray&)> offlineCacheHandler;

    std::atomic<int> pendingProgram { -1 };
    std::atomic<int> midiChannel { 0 };
    bool lastModified = false;
    ModelBank::Status lastStatus;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};

} // namespace pedal

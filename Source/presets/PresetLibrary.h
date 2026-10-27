#pragma once

#include "presets/Preset.h"

namespace pedal
{

// The presets and setlists on disk, kept in memory.
//
// Folder layout (inside getDefaultFolder()):
//   Presets/<name>.json   one preset per file
//   Setlists/<name>.json  one setlist per file
//   IRs/                  cabinet IRs; used to find an IR whose saved path is
//                         gone (e.g. a preset copied from the Mac to the iPad)
//
// Message thread only. Files are written to a temporary file first and then
// moved into place, so a crash while saving never leaves a broken preset.
class PresetLibrary
{
public:
    explicit PresetLibrary (const juce::File& rootFolder = getDefaultFolder());

    static juce::File getDefaultFolder();
    juce::File getRootFolder() const { return root; }
    juce::File getPresetFolder() const { return root.getChildFile ("Presets"); }
    juce::File getSetlistFolder() const { return root.getChildFile ("Setlists"); }
    juce::File getIrFolder() const { return root.getChildFile ("IRs"); }

    // Reads every file again. Unreadable files are skipped and listed in getScanErrors().
    void rescan();
    const juce::StringArray& getScanErrors() const { return scanErrors; }

    // Sorted by name.
    const std::vector<Preset>& getPresets() const { return presets; }
    const Preset* findPreset (const juce::String& id) const;
    std::vector<const Preset*> getFavourites() const;

    // Each returns an empty string on success or the error. savePreset gives the
    // preset an id if it has none and renames the file when the name changed.
    juce::String savePreset (Preset& preset);
    juce::String removePreset (const juce::String& id);
    juce::String setFavourite (const juce::String& id, bool favourite);

    const std::vector<Setlist>& getSetlists() const { return setlists; }
    const Setlist* findSetlist (const juce::String& id) const;
    // The setlist's presets in order; entries whose preset was deleted are skipped.
    std::vector<const Preset*> resolve (const Setlist&) const;
    juce::String saveSetlist (Setlist& setlist);
    juce::String removeSetlist (const juce::String& id);

    // The IR at `path`, or a file with the same name in getIrFolder(), or {}.
    juce::File findIr (const juce::String& path) const;

    // modelIds of every TONE3000 capture used by a preset, for the offline cache.
    juce::StringArray getTone3000ModelIds() const;

private:
    template <typename Item>
    juce::String writeItem (std::vector<Item>& items, std::map<juce::String, juce::File>& files,
                            Item& item, const juce::File& folder);
    template <typename Item>
    juce::String removeItem (std::vector<Item>& items, std::map<juce::String, juce::File>& files,
                             const juce::String& id);

    juce::File root;
    std::vector<Preset> presets;
    std::vector<Setlist> setlists;
    std::map<juce::String, juce::File> presetFiles, setlistFiles; // id -> file
    juce::StringArray scanErrors;
};

} // namespace pedal

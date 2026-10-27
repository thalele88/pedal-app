#pragma once

#include <juce_events/juce_events.h>

namespace pedal::ui
{

struct PresetInfo
{
    juce::String name;
    juce::String ampName;   // capture shown under the preset name
    juce::String category;  // free text, e.g. "Guitarra", "Baixo"
    bool favourite = false;
};

// What the preset browser and the stage mode need from the preset system.
// The UI only talks to presets through this, so the preset manager can change
// without touching the screens.
//
// Everything is called on the message thread. The list returned here is the
// order the stage shows: when a setlist is active, it is that setlist.
// Implementations call sendChangeMessage() whenever the list, the current
// preset or the modified flag changes (including changes from MIDI).
class PresetSource : public juce::ChangeBroadcaster
{
public:
    ~PresetSource() override = default;

    virtual int getNumPresets() const = 0;
    virtual PresetInfo getPresetInfo (int index) const = 0;
    virtual int getCurrentIndex() const = 0; // -1 = none
    // May load captures in the background; must return quickly.
    virtual void selectPreset (int index) = 0;
    virtual juce::String getListName() const = 0; // active setlist, or "Todos"

    virtual bool canSave() const = 0;
    virtual void saveCurrent() = 0;
    virtual void saveCurrentAs (const juce::String& name) = 0;
    virtual void setFavourite (int index, bool favourite) = 0;
    // True when the knobs differ from the saved preset.
    virtual bool isModified() const = 0;
};

} // namespace pedal::ui

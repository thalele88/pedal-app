#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/PresetSource.h"

namespace pedal::ui
{

// Stand-in preset list kept in memory until the real preset manager
// (Source/presets/) lands. Each preset is a snapshot of the plugin state, so
// the browser and the stage mode can be tried today. Nothing is written to disk.
class SessionPresets : public PresetSource
{
public:
    SessionPresets (juce::AudioProcessor& p, std::function<juce::String()> ampNameFn)
        : processor (p), ampName (std::move (ampNameFn)) {}

    int getNumPresets() const override { return (int) entries.size(); }
    PresetInfo getPresetInfo (int index) const override
    {
        return juce::isPositiveAndBelow (index, getNumPresets()) ? entries[(size_t) index].info : PresetInfo {};
    }
    int getCurrentIndex() const override { return current; }
    juce::String getListName() const override { return "Todos"; }

    void selectPreset (int index) override
    {
        if (! juce::isPositiveAndBelow (index, getNumPresets()))
            return;
        current = index;
        const auto& state = entries[(size_t) index].state;
        processor.setStateInformation (state.getData(), (int) state.getSize());
        sendChangeMessage();
    }

    bool canSave() const override { return true; }

    void saveCurrent() override
    {
        if (! juce::isPositiveAndBelow (current, getNumPresets()))
            return;
        auto& e = entries[(size_t) current];
        e.state = capture();
        e.info.ampName = ampName();
        sendChangeMessage();
    }

    void saveCurrentAs (const juce::String& name) override
    {
        Entry e;
        e.info.name = name.isNotEmpty() ? name : "Preset " + juce::String (getNumPresets() + 1);
        e.info.ampName = ampName();
        e.state = capture();
        entries.push_back (std::move (e));
        current = getNumPresets() - 1;
        sendChangeMessage();
    }

    void setFavourite (int index, bool favourite) override
    {
        if (! juce::isPositiveAndBelow (index, getNumPresets()))
            return;
        entries[(size_t) index].info.favourite = favourite;
        sendChangeMessage();
    }

    bool isModified() const override
    {
        return juce::isPositiveAndBelow (current, getNumPresets()) && capture() != entries[(size_t) current].state;
    }

private:
    struct Entry
    {
        PresetInfo info;
        juce::MemoryBlock state;
    };

    juce::MemoryBlock capture() const
    {
        juce::MemoryBlock block;
        processor.getStateInformation (block);
        return block;
    }

    juce::AudioProcessor& processor;
    std::function<juce::String()> ampName;
    std::vector<Entry> entries;
    int current = -1;
};

} // namespace pedal::ui

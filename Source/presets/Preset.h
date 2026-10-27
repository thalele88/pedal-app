#pragma once

#include "presets/ToneRef.h"

#include <map>
#include <optional>
#include <vector>

namespace pedal
{

// One sound: knob values, the amp capture and the cabinet IR.
//
// Saved as JSON, one file per preset:
//   { "format": "pedal-app-preset", "version": 1, "id": "...", "name": "Drop C",
//     "category": "Guitarra", "favourite": true,
//     "params": { "inputGain": 0.0, "drop": -2, ... },
//     "amp": { "source": "tone3000", "toneId": "...", "modelId": "...", "name": "...", "creator": "..." },
//     "ir": "/path/cab.wav" }
//
// Parameters are stored in their real units (dB, semitones), not 0..1, so a
// preset keeps sounding the same if a range changes later. Parameters missing
// from the file take their default value; unknown ones are ignored.
struct Preset
{
    juce::String id;       // stable across renames; setlists point to it
    juce::String name;
    juce::String category; // free text, e.g. "Guitarra", "Baixo"
    bool favourite = false;
    std::map<juce::String, float> params;
    ToneRef amp;
    juce::String irPath;   // empty = no cabinet IR

    juce::var toVar() const;
    juce::String toJson() const;
    // Returns nothing and fills `error` when the JSON is not a preset.
    static std::optional<Preset> fromVar (const juce::var&, juce::String& error);
    static std::optional<Preset> fromJson (const juce::String& json, juce::String& error);
};

// Presets in show order. The same preset can appear more than once.
struct Setlist
{
    struct Entry
    {
        juce::String presetId;
        juce::String note; // e.g. song name
    };

    juce::String id;
    juce::String name;
    std::vector<Entry> entries;

    juce::var toVar() const;
    juce::String toJson() const;
    static std::optional<Setlist> fromVar (const juce::var&, juce::String& error);
    static std::optional<Setlist> fromJson (const juce::String& json, juce::String& error);
};

juce::String createPresetId();

} // namespace pedal

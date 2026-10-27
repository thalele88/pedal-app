#pragma once

#include <juce_core/juce_core.h>

namespace pedal
{

// Which amp capture a preset uses. This is the contract between presets and
// the TONE3000 module (see docs/ARCHITECTURE.md): presets only store the
// reference, and the TONE3000 side turns a tone3000 ref into model JSON.
struct ToneRef
{
    enum class Source { none, localFile, tone3000 };

    Source source = Source::none;
    juce::String localPath;          // when localFile
    juce::String toneId, modelId;    // when tone3000
    juce::String displayName, creator;

    static ToneRef fromFile (const juce::File& namFile);
    static ToneRef fromTone3000 (const juce::String& toneId, const juce::String& modelId,
                                 const juce::String& displayName, const juce::String& creator);

    bool isEmpty() const noexcept { return source == Source::none; }

    // Same key = same capture. Used to share preloaded models between presets.
    juce::String getKey() const;
    // Name for the screen; falls back to the file name.
    juce::String getName() const;

    juce::var toVar() const;
    static ToneRef fromVar (const juce::var&);

    bool operator== (const ToneRef& other) const { return getKey() == other.getKey(); }
    bool operator!= (const ToneRef& other) const { return ! operator== (other); }
};

} // namespace pedal

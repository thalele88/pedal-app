#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pedal::ui
{

// The pitch (drop tuning) controls, found by parameter ID.
//
// The drop tuner work brings TONE3000-style parameters (power, STEP, Tonality,
// Buffer). Until it lands the processor only has "drop", where 0 means off.
// The UI goes through this helper so it works with either set.
class PitchParams
{
public:
    explicit PitchParams (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        auto find = [&] (std::initializer_list<const char*> ids) -> juce::RangedAudioParameter*
        {
            for (auto* id : ids)
                if (auto* p = state.getParameter (id))
                    return p;
            return nullptr;
        };

        amount = find ({ "pitch", "drop" });
        power = find ({ "pitchOn", "dropEnabled" });
        step = find ({ "pitchStep", "dropStep" });
        tonalityOn = find ({ "pitchTonalityOn", "dropTonalityOn" });
        tonality = find ({ "pitchTonality", "dropTonality" });
        buffer = find ({ "pitchBuffer", "dropMode" });
    }

    juce::RangedAudioParameter* amount = nullptr;     // semitones
    juce::RangedAudioParameter* power = nullptr;      // null with the old "drop" parameter
    juce::RangedAudioParameter* step = nullptr;       // snap to whole semitones
    juce::RangedAudioParameter* tonalityOn = nullptr; // keep highs unshifted
    juce::RangedAudioParameter* tonality = nullptr;   // Hz above which highs stay
    juce::RangedAudioParameter* buffer = nullptr;     // engine / latency choice

    bool isOn() const
    {
        if (power != nullptr)
            return power->getValue() > 0.5f;
        return juce::roundToInt (getRaw()) != 0;
    }

    float getSemitones() const
    {
        const float v = getRaw();
        return (step == nullptr || step->getValue() > 0.5f) ? (float) juce::roundToInt (v) : v;
    }

    // What actually reaches the sound: 0 when off.
    float getEffectiveSemitones() const { return isOn() ? getSemitones() : 0.0f; }

    void setOn (bool shouldBeOn)
    {
        if (power != nullptr)
        {
            set (power, shouldBeOn ? 1.0f : 0.0f);
            return;
        }

        // Old "drop" parameter: off means 0, so remember the value to bring it back.
        if (! shouldBeOn && isOn())
            lastOnValue = getRaw();
        set (amount, shouldBeOn ? lastOnValue : 0.0f);
    }

    // Shortcut buttons: 0 turns the shift off, anything else sets it and turns it on.
    void selectShortcut (int semitones)
    {
        if (semitones == 0)
        {
            setOn (false);
            return;
        }
        set (amount, (float) semitones);
        if (power != nullptr)
            set (power, 1.0f);
    }

private:
    float getRaw() const { return amount != nullptr ? amount->convertFrom0to1 (amount->getValue()) : 0.0f; }

    static void set (juce::RangedAudioParameter* p, float value)
    {
        if (p == nullptr)
            return;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }

    juce::AudioProcessorValueTreeState& state;
    float lastOnValue = -2.0f;
};

} // namespace pedal::ui

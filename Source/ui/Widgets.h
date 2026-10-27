#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/PedalLookAndFeel.h"

namespace pedal::ui
{

// Shorthand for juce::String::fromUTF8, needed for every accented UI string.
inline juce::String tr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

// Sets a parameter from the UI as one undoable host gesture.
inline void setParameter (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
{
    if (auto* param = state.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

inline float getParameter (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    return state.getRawParameterValue (id)->load();
}

// A rotary knob with its name above, bound to one parameter.
class ParamKnob : public juce::Component
{
public:
    ParamKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& name)
    {
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Colours::textDim);
        label.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 24);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (label);
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds (area.removeFromTop (22));
        slider.setBounds (area);
    }

private:
    juce::Label label;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Button that only fires after being held, so a stray touch on stage
// cannot leave the stage screen in the middle of a song.
class HoldButton : public juce::Component,
                   private juce::Timer
{
public:
    explicit HoldButton (juce::String textToShow, int holdMs = 700) : text (std::move (textToShow)), holdTime (holdMs) {}

    std::function<void()> onHold;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (Colours::panelRaised);
        g.fillRoundedRectangle (r, 8.0f);
        if (progress > 0.0f)
        {
            g.setColour (Colours::accent.withAlpha (0.6f));
            g.fillRoundedRectangle (r.withWidth (r.getWidth() * progress), 8.0f);
        }
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (juce::jmin (18.0f, r.getHeight() * 0.4f), juce::Font::bold));
        g.drawFittedText (progress > 0.0f ? tr ("Segure...") : text, getLocalBounds().reduced (6), juce::Justification::centred, 2);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        pressedAt = juce::Time::getMillisecondCounter();
        startTimerHz (60);
    }

    void mouseUp (const juce::MouseEvent&) override { cancel(); }

private:
    void timerCallback() override
    {
        progress = juce::jlimit (0.0f, 1.0f, (float) (juce::Time::getMillisecondCounter() - pressedAt) / (float) holdTime);
        repaint();
        if (progress >= 1.0f)
        {
            cancel();
            if (onHold)
                onHold();
        }
    }

    void cancel()
    {
        stopTimer();
        progress = 0.0f;
        repaint();
    }

    juce::String text;
    int holdTime;
    juce::uint32 pressedAt = 0;
    float progress = 0.0f;
};

} // namespace pedal::ui

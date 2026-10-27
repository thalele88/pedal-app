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

// The one-line tip the hint bar shows while the mouse is over (or a finger is
// on) a component or any of its children.
inline void setHint (juce::Component& c, const juce::String& text) { c.getProperties().set ("hint", text); }

inline juce::String findHint (juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent())
        if (c->getProperties().contains ("hint"))
            return c->getProperties()["hint"].toString();
    return {};
}

// Simple line icons drawn with paths, so no image assets are needed.
namespace Icons
{
    inline juce::Path power (juce::Rectangle<float> r)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.38f;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
        p.startNewSubPath (c.x, c.y - rad * 1.15f);
        p.lineTo (c.x, c.y - rad * 0.2f);
        return p;
    }

    inline juce::Path chevron (juce::Rectangle<float> r, bool right)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.22f;
        const float d = right ? 1.0f : -1.0f;
        p.startNewSubPath (c.x - d * s * 0.5f, c.y - s);
        p.lineTo (c.x + d * s * 0.5f, c.y);
        p.lineTo (c.x - d * s * 0.5f, c.y + s);
        return p;
    }

    inline juce::Path plus (juce::Rectangle<float> r)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.25f;
        p.startNewSubPath (c.x - s, c.y);
        p.lineTo (c.x + s, c.y);
        p.startNewSubPath (c.x, c.y - s);
        p.lineTo (c.x, c.y + s);
        return p;
    }

    inline juce::Path save (juce::Rectangle<float> r)
    {
        juce::Path p;
        auto b = r.withSizeKeepingCentre (r.getHeight() * 0.5f, r.getHeight() * 0.5f);
        p.addRoundedRectangle (b, 2.0f);
        p.addRectangle (b.getX() + b.getWidth() * 0.25f, b.getY(), b.getWidth() * 0.5f, b.getHeight() * 0.3f);
        p.addRectangle (b.getX() + b.getWidth() * 0.2f, b.getY() + b.getHeight() * 0.55f, b.getWidth() * 0.6f, b.getHeight() * 0.45f);
        return p;
    }

    inline juce::Path tuningFork (juce::Rectangle<float> r)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float h = r.getHeight() * 0.3f, w = h * 0.45f;
        p.startNewSubPath (c.x - w, c.y - h);
        p.lineTo (c.x - w, c.y);
        p.quadraticTo (c.x - w, c.y + w, c.x, c.y + w);
        p.quadraticTo (c.x + w, c.y + w, c.x + w, c.y);
        p.lineTo (c.x + w, c.y - h);
        p.startNewSubPath (c.x, c.y + w);
        p.lineTo (c.x, c.y + h * 1.1f);
        return p;
    }

    inline juce::Path back (juce::Rectangle<float> r)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.28f;
        p.startNewSubPath (c.x + s, c.y);
        p.lineTo (c.x - s, c.y);
        p.startNewSubPath (c.x - s * 0.3f, c.y - s * 0.7f);
        p.lineTo (c.x - s, c.y);
        p.lineTo (c.x - s * 0.3f, c.y + s * 0.7f);
        return p;
    }

    inline juce::Path dots (juce::Rectangle<float> r)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float d = juce::jmin (r.getWidth(), r.getHeight()) * 0.09f;
        for (int i = -1; i <= 1; ++i)
            p.addEllipse (c.x + (float) i * d * 3.0f - d, c.y - d, d * 2.0f, d * 2.0f);
        return p;
    }
}

// A borderless button that draws one icon (and optional text after it).
class IconButton : public juce::Button
{
public:
    using IconFn = std::function<juce::Path (juce::Rectangle<float>)>;

    IconButton (const juce::String& name, IconFn iconFn, bool filledIcon = false)
        : juce::Button (name), icon (std::move (iconFn)), filled (filledIcon) {}

    juce::String label;
    bool drawBackground = false;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        if (drawBackground || over || getToggleState())
        {
            g.setColour (getToggleState() ? Colours::accent.withAlpha (0.25f)
                                          : Colours::panelRaised.withAlpha (drawBackground ? 1.0f : (down ? 0.9f : 0.6f)));
            g.fillRoundedRectangle (r.reduced (1.0f), juce::jmin (10.0f, r.getHeight() * 0.5f));
        }

        const auto colour = ! isEnabled() ? Colours::textDim.withAlpha (0.4f) : (getToggleState() ? Colours::accent : Colours::text);
        auto iconArea = label.isEmpty() ? r : r.removeFromLeft (r.getHeight()).translated (6.0f, 0.0f);
        const auto path = icon (iconArea);
        g.setColour (colour);
        if (filled)
            g.fillPath (path);
        else
            g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (label.isNotEmpty())
        {
            g.setFont (juce::FontOptions (juce::jmin (16.0f, r.getHeight() * 0.42f), juce::Font::bold));
            g.drawText (label, r.withTrimmedLeft (6.0f).withTrimmedRight (10.0f), juce::Justification::centredLeft);
        }
    }

private:
    IconFn icon;
    bool filled;
};

// Round power switch like on the TONE3000 bar: lit when on.
class PowerButton : public juce::Button
{
public:
    PowerButton() : juce::Button ("power") { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (22.0f, 22.0f);
        const bool on = getToggleState();
        g.setColour (on ? Colours::accent.withAlpha (0.18f) : (over ? Colours::panelRaised : juce::Colours::transparentBlack));
        g.fillEllipse (r);
        g.setColour (on ? Colours::accent : Colours::textDim);
        g.strokePath (Icons::power (r), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

// Knob with its name underneath (bottom bar style). The value shows in a
// bubble while dragging; double-click resets to the default.
class StripKnob : public juce::Component
{
public:
    StripKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& name)
    {
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Colours::textDim);
        label.setFont (juce::FontOptions (14.0f));
        label.setInterceptsMouseClicks (false, false);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setPopupDisplayEnabled (true, true, nullptr, 1500);
        addAndMakeVisible (label);
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
        if (auto* p = state.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void setDimmed (bool dimmed)
    {
        slider.setAlpha (dimmed ? 0.35f : 1.0f);
        label.setAlpha (dimmed ? 0.5f : 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds (area.removeFromBottom (20));
        slider.setBounds (area);
    }

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label label;

private:
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

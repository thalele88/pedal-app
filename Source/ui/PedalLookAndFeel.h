#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pedal::ui
{

// Colours live here so the stage mode and the editor share one palette.
namespace Colours
{
    inline const juce::Colour background { 0xff121316 };
    inline const juce::Colour panel { 0xff1c1e23 };
    inline const juce::Colour panelRaised { 0xff262930 };
    inline const juce::Colour text { 0xffe8e8ea };
    inline const juce::Colour textDim { 0xff8b8f99 };
    inline const juce::Colour accent { 0xffff8a1f }; // amp-light orange
    inline const juce::Colour ok { 0xff3ddc84 };
    inline const juce::Colour warn { 0xffffc23d };
}

class PedalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
        setColour (juce::Label::textColourId, Colours::text);
        setColour (juce::TextButton::buttonColourId, Colours::panelRaised);
        setColour (juce::TextButton::buttonOnColourId, Colours::accent);
        setColour (juce::TextButton::textColourOffId, Colours::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxTextColourId, Colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::thumbColourId, Colours::accent);
        setColour (juce::Slider::trackColourId, Colours::accent.withAlpha (0.6f));
        setColour (juce::Slider::backgroundColourId, Colours::panelRaised);
    }

    // Bigger, bolder button text so labels stay readable on stage and on touch screens.
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::FontOptions (juce::jlimit (14.0f, 24.0f, (float) buttonHeight * 0.36f), juce::Font::bold);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float track = juce::jmax (3.0f, radius * 0.12f);

        juce::Path background;
        background.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
        g.setColour (Colours::panelRaised);
        g.strokePath (background, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
        g.setColour (Colours::accent);
        g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float knobRadius = radius - track * 2.5f;
        g.setColour (Colours::panel.brighter (0.15f));
        g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-track * 0.5f, -knobRadius, track, knobRadius * 0.5f, track * 0.5f);
        g.setColour (Colours::text);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    }
};

} // namespace pedal::ui

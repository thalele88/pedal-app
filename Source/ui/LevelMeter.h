#pragma once

#include "ui/Widgets.h"

namespace pedal::ui
{

// Vertical dot meter with a dB scale (0 to -60), green / yellow / red, and a
// clip light that stays lit until clicked. Fed by the editor's timer.
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (bool scaleOnLeft) : scaleLeft (scaleOnLeft) {}

    // peak: linear peak since the last call. Call ~30 times a second.
    void push (float peak)
    {
        const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
        level = db > level ? db : juce::jmax (-100.0f, level - 1.5f); // ~45 dB/s fall
        if (peak >= 1.0f)
            clipped = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        clipped = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (0.0f, 6.0f);
        auto scale = scaleLeft ? area.removeFromLeft (24.0f) : area.removeFromRight (24.0f);
        auto dots = area.withSizeKeepingCentre (8.0f, area.getHeight());

        auto yFor = [&] (float db) { return dots.getY() + dots.getHeight() * (1.0f - (db + 60.0f) / 60.0f); };

        g.setFont (juce::FontOptions (10.0f));
        for (float db : { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            g.setColour (Colours::textDim.withAlpha (0.7f));
            g.drawText (juce::String ((int) db), scale.withY (yFor (db) - 6.0f).withHeight (12.0f),
                        scaleLeft ? juce::Justification::centredRight : juce::Justification::centredLeft);
        }

        constexpr int numDots = 30;
        const float step = dots.getHeight() / (float) numDots;
        for (int i = 0; i < numDots; ++i)
        {
            const float db = -60.0f + 60.0f * ((float) i + 0.5f) / (float) numDots;
            const auto colour = db > -3.0f ? juce::Colour (0xffff4d4d) : (db > -12.0f ? Colours::warn : Colours::ok);
            const bool lit = db <= level;
            const float d = juce::jmin (step * 0.7f, 6.0f);
            g.setColour (lit ? colour : colour.withAlpha (0.12f));
            g.fillEllipse (dots.getCentreX() - d * 0.5f, dots.getBottom() - step * ((float) i + 0.5f) - d * 0.5f, d, d);
        }

        g.setColour (clipped ? juce::Colour (0xffff4d4d) : Colours::panelRaised);
        g.fillEllipse (dots.getCentreX() - 4.0f, getLocalBounds().toFloat().getY(), 8.0f, 8.0f);
    }

private:
    const bool scaleLeft;
    float level = -100.0f;
    bool clipped = false;
};

} // namespace pedal::ui

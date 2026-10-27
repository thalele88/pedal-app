#pragma once

#include "ui/Widgets.h"

namespace pedal::ui
{

// Drawn artwork for the block cards, until captures bring their own pictures.
// Each capture gets its own tint (from its name), so cards are easy to tell apart.
namespace CardArt
{
    enum class Kind { amp, cab };

    inline juce::Colour tintFor (const juce::String& name)
    {
        const auto hash = (juce::uint32) name.hashCode();
        return juce::Colour::fromHSV ((float) (hash % 360) / 360.0f, 0.55f, 0.55f, 1.0f);
    }

    inline void drawBackground (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, float corner)
    {
        juce::ColourGradient grad (tint.withMultipliedBrightness (0.9f), r.getX(), r.getY(),
                                   juce::Colour (0xff0d0e11), r.getRight(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, corner);

        // Soft spotlight behind the gear.
        juce::ColourGradient spot (juce::Colours::white.withAlpha (0.12f), r.getCentreX(), r.getY() + r.getHeight() * 0.35f,
                                   juce::Colours::transparentWhite, r.getCentreX(), r.getBottom(), true);
        g.setGradientFill (spot);
        g.fillRoundedRectangle (r, corner);
    }

    inline void drawSpeaker (juce::Graphics& g, juce::Point<float> c, float radius)
    {
        g.setColour (juce::Colour (0xff16171b));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c));
        g.setColour (juce::Colour (0xff2c2e35));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), radius * 0.08f);
        g.fillEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.7f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (juce::Rectangle<float> (radius * 1.3f, radius * 1.3f).withCentre (c), 1.0f);
    }

    inline void drawCabinet (juce::Graphics& g, juce::Rectangle<float> box, int speakers)
    {
        g.setColour (juce::Colour (0xff1d1e22));
        g.fillRoundedRectangle (box, box.getWidth() * 0.03f);
        auto grille = box.reduced (box.getWidth() * 0.06f);
        g.setColour (juce::Colour (0xff2a2b31));
        g.fillRoundedRectangle (grille, 3.0f);

        // Woven grille texture
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        for (float x = grille.getX(); x < grille.getRight(); x += 4.0f)
            g.drawVerticalLine ((int) x, grille.getY(), grille.getBottom());

        if (speakers == 1)
        {
            drawSpeaker (g, grille.getCentre(), juce::jmin (grille.getWidth(), grille.getHeight()) * 0.38f);
        }
        else
        {
            const float rad = juce::jmin (grille.getWidth(), grille.getHeight()) * 0.22f;
            for (int i = 0; i < 4; ++i)
                drawSpeaker (g, { grille.getX() + grille.getWidth() * (i % 2 == 0 ? 0.28f : 0.72f),
                                  grille.getY() + grille.getHeight() * (i < 2 ? 0.28f : 0.72f) }, rad);
        }
    }

    inline void drawAmpHead (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour tint)
    {
        g.setColour (juce::Colour (0xff1a1b1f));
        g.fillRoundedRectangle (box, box.getHeight() * 0.08f);
        auto panel = box.reduced (box.getWidth() * 0.05f, box.getHeight() * 0.18f);
        g.setColour (tint.withSaturation (0.3f).withBrightness (0.75f));
        g.fillRoundedRectangle (panel, 3.0f);

        // Knob row
        const int knobs = 6;
        const float kr = panel.getHeight() * 0.22f;
        for (int i = 0; i < knobs; ++i)
        {
            const float x = panel.getX() + panel.getWidth() * (0.22f + 0.13f * (float) i);
            const float y = panel.getCentreY() + panel.getHeight() * 0.12f;
            g.setColour (juce::Colour (0xff111215));
            g.fillEllipse (x - kr, y - kr, kr * 2.0f, kr * 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawLine (x, y, x + kr * 0.6f, y - kr * 0.6f, 1.2f);
        }

        // Pilot light
        g.setColour (Colours::accent);
        g.fillEllipse (panel.getX() + panel.getWidth() * 0.06f, panel.getCentreY() - kr * 0.4f, kr * 0.9f, kr * 0.9f);
    }

    // Draws an amp stack or a speaker cab filling `r`.
    inline void draw (juce::Graphics& g, juce::Rectangle<float> r, Kind kind, const juce::String& name, float corner)
    {
        const auto tint = tintFor (name);
        drawBackground (g, r, tint, corner);

        auto gear = r.reduced (r.getWidth() * 0.16f, r.getHeight() * 0.12f);
        if (kind == Kind::amp)
        {
            auto head = gear.removeFromTop (gear.getHeight() * 0.3f);
            gear.removeFromTop (gear.getHeight() * 0.03f);
            drawAmpHead (g, head, tint);
            drawCabinet (g, gear, 4);
        }
        else
        {
            const float side = juce::jmin (gear.getWidth(), gear.getHeight());
            drawCabinet (g, gear.withSizeKeepingCentre (side, side), 1);
        }

        // Floor shadow
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (r.getCentreX() - r.getWidth() * 0.35f, r.getBottom() - r.getHeight() * 0.1f, r.getWidth() * 0.7f, r.getHeight() * 0.05f);
    }
}

} // namespace pedal::ui

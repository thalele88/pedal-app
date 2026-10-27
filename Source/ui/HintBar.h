#pragma once

#include "ui/Widgets.h"

namespace pedal::ui
{

// One-line tip under the controls explaining what is under the mouse (or the
// finger), like the TONE3000 plugin's info bar. Also shows status messages
// for a few seconds, and the CPU load on the right.
class HintBar : public juce::Component
{
public:
    HintBar() { setInterceptsMouseClicks (false, false); }

    void setHint (const juce::String& text)
    {
        if (text != hint)
        {
            hint = text;
            repaint();
        }
    }

    // Status messages win over hints for a few seconds (errors, "loading...").
    void flash (const juce::String& text)
    {
        status = text;
        statusUntil = juce::Time::getMillisecondCounter() + 5000;
        repaint();
    }

    void setCpu (double load)
    {
        const int percent = juce::roundToInt (load * 100.0);
        if (percent != cpu)
        {
            cpu = percent;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        auto area = getLocalBounds().reduced (16, 0);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (cpu > 70 ? Colours::warn : Colours::textDim);
        g.drawText ("CPU " + juce::String (cpu) + "%", area.removeFromRight (80), juce::Justification::centredRight);

        const bool showStatus = status.isNotEmpty() && juce::Time::getMillisecondCounter() < statusUntil;
        g.setColour (showStatus ? Colours::warn : Colours::textDim);
        g.drawText (showStatus ? status : (hint.isNotEmpty() ? hint : fallback), area, juce::Justification::centredLeft, true);
    }

private:
    juce::String hint, status;
    juce::String fallback { juce::String::fromUTF8 ("Passe o mouse (ou o dedo) sobre qualquer controle para ver aqui o que ele faz.") };
    juce::uint32 statusUntil = 0;
    int cpu = 0;
};

} // namespace pedal::ui

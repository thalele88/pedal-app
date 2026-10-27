#pragma once

#include "PluginProcessor.h"
#include "ui/Widgets.h"

namespace pedal::ui
{

// Full-screen chromatic tuner. Big note name, a needle in cents, green when
// in tune. Reads the raw input (before the drop), so the instrument is tuned
// to standard pitch and the drop still works on top of it.
//
// While it is visible the tuner input is switched on; pitch detection runs on
// the message thread at 30 Hz on a decimated signal, never on the audio thread.
class TunerView : public juce::Component,
                  private juce::Timer
{
public:
    explicit TunerView (PedalAudioProcessor&);
    ~TunerView() override;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void analyse();

    PedalAudioProcessor& processor;

    static constexpr int historySize = 2048;
    std::array<float, historySize> history {};
    std::array<float, historySize> work {};
    std::array<float, 1024> incoming {};

    NoteReading reading;
    float smoothedCents = 0.0f;
    int framesWithoutPitch = 0;
    float recentFreqs[5] {};
    int recentCount = 0;

    juce::TextButton muteButton { "Mutar enquanto afina" };
    juce::TextButton closeButton { "Fechar" };
};

} // namespace pedal::ui

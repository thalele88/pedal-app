#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/PedalLookAndFeel.h"

// First version of the main screen. Layout idea, top to bottom:
//   header (name, loaded model, CPU)  ->  signal chain blocks  ->  big knobs  ->  drop tuning buttons
// The chain strip follows the "pedalboard row" idea from Neural DSP plugins;
// the preset browser (ToneX style) and the stage mode come in later steps.
class PedalAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit PedalAudioProcessorEditor (PedalAudioProcessor&);
    ~PedalAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void chooseAmpModel();
    void chooseCabinet();
    void setDrop (int semitones);
    void setDropMode (pedal::DropTuner::Mode mode);

    PedalAudioProcessor& processor;
    pedal::ui::PedalLookAndFeel lookAndFeel;

    juce::Label title, modelLabel, cpuLabel, statusLabel;
    juce::TextButton ampButton { "AMP" }, cabButton { "CAIXA" };
    juce::ToggleButton cabToggle { "Caixa ligada" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabAttachment;

    Knob inputKnob, gateKnob, blendKnob, outputKnob;

    static constexpr int dropSteps[] = { 0, -1, -2, -3, -5, -7, -12 };
    juce::OwnedArray<juce::TextButton> dropButtons;
    juce::Label dropLabel, dropLatencyLabel;
    // Drop engine: "Rápido" (lowest delay) or "Suave" (no flutter on big chords).
    juce::TextButton fastButton, smoothButton;

    juce::Rectangle<int> chainArea;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalAudioProcessorEditor)
};

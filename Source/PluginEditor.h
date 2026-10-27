#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/BlockPanels.h"
#include "ui/PedalLookAndFeel.h"
#include "ui/PresetBrowser.h"
#include "ui/SignalChainView.h"
#include "ui/StageView.h"
#include "ui/TunerView.h"

// Main screen. Layout:
//   header (preset name, CPU, tuner and stage buttons)
//   preset browser on the left (ToneX idea)  |  signal chain blocks (Neural DSP idea)
//                                            |  controls of the selected block
// The stage mode and the tuner open full screen on top of everything.
class PedalAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit PedalAudioProcessorEditor (PedalAudioProcessor&);
    ~PedalAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void selectBlock (pedal::ui::BlockId);
    void toggleBlock (pedal::ui::BlockId);
    void updateChain();
    void showStage (bool);
    void showTuner (bool);
    void showStatus (const juce::String&);

    PedalAudioProcessor& processor;
    pedal::ui::PedalLookAndFeel lookAndFeel;

    juce::Label title, presetLabel, cpuLabel, statusLabel;
    juce::TextButton tunerButton { "AFINADOR" }, stageButton { "PALCO" };

    pedal::ui::PresetBrowser browser;
    pedal::ui::SignalChainView chain;
    std::unique_ptr<pedal::ui::BlockPanel> panel;
    pedal::ui::StageView stage;
    pedal::ui::TunerView tuner;
    juce::TooltipWindow tooltips { this };

    // Values restored when a block is switched back on.
    float gateWhenOn = -60.0f;
    int dropWhenOn = -2;
    int tick = 0;
    bool presetModified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalAudioProcessorEditor)
};

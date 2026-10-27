#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/BlockDetailView.h"
#include "ui/ChainView.h"
#include "ui/ControlStrip.h"
#include "ui/HintBar.h"
#include "ui/LevelMeter.h"
#include "ui/PedalLookAndFeel.h"
#include "ui/PitchParams.h"
#include "ui/PresetBrowser.h"
#include "ui/StageView.h"
#include "ui/TunerView.h"

// Main screen, laid out like the TONE3000 plugin:
//
//   header: logo | < preset > save + | tuner, stage
//   sidebar (Cadeia / TONE3000 + preset list) | in meter | chain or block view | out meter
//   master controls: Input, Gate, Pitch, Bass/Middle/Treble, Output
//   hint bar (what the control under the mouse does) + CPU
//
// The stage mode and the tuner open full screen on top of everything.
class PedalAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer,
                                  private juce::ChangeListener
{
public:
    explicit PedalAudioProcessorEditor (PedalAudioProcessor&);
    ~PedalAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class MainView { chain, block, tone3000 };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showMain (MainView);
    void openBlock (pedal::ui::CardArt::Kind);
    void addToSlot (pedal::ui::CardArt::Kind);
    void updateCards();
    void updatePresetName();
    void stepPreset (int delta);
    void showStage (bool);
    void showTuner (bool);
    void showStatus (const juce::String&);
    std::unique_ptr<juce::Component> createTone3000View();

    PedalAudioProcessor& processor;
    pedal::ui::PedalLookAndFeel lookAndFeel;
    pedal::ui::PitchParams pitch;

    // Header
    juce::Label logo;
    pedal::ui::IconButton prevPreset { "preset anterior", [] (auto r) { return pedal::ui::Icons::chevron (r, false); } };
    pedal::ui::IconButton nextPreset { "proximo preset", [] (auto r) { return pedal::ui::Icons::chevron (r, true); } };
    pedal::ui::IconButton savePreset { "salvar", pedal::ui::Icons::save };
    pedal::ui::IconButton newPreset { "novo", pedal::ui::Icons::plus };
    pedal::ui::IconButton tunerButton { "afinador", pedal::ui::Icons::tuningFork };
    juce::TextButton stageButton { "PALCO" };
    juce::Rectangle<int> presetBox;
    juce::String presetName;

    // Sidebar
    juce::TextButton chainTab { "Cadeia de sinal" }, tone3000Tab { "TONE3000" };
    pedal::ui::PresetBrowser browser;

    // Main area
    pedal::ui::LevelMeter inMeter { true }, outMeter { false };
    pedal::ui::ChainView chain;
    pedal::ui::BlockDetailView block;
    std::unique_ptr<juce::Component> tone3000View;
    MainView mainView = MainView::chain;

    pedal::ui::ControlStrip controls;
    pedal::ui::HintBar hintBar;

    pedal::ui::StageView stage;
    pedal::ui::TunerView tuner;
    juce::TooltipWindow tooltips { this, 600 };

    struct HintTracker;
    std::unique_ptr<HintTracker> hintTracker;

    bool presetModified = false;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalAudioProcessorEditor)
};

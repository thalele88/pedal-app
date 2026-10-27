#pragma once

#include "PluginProcessor.h"
#include "ui/PitchParams.h"
#include "ui/Widgets.h"

namespace pedal::ui
{

// Master controls along the bottom, laid out like the TONE3000 plugin:
// Input (+ DI test player), Gate, Pitch (the drop), Bass / Middle / Treble, Output.
// Gate, Pitch and EQ have power buttons that bypass without losing the setting.
// Right-click (or the "..." button, for touch) on Pitch opens its options.
class ControlStrip : public juce::Component
{
public:
    ControlStrip (PedalAudioProcessor&, PitchParams&, std::function<void (juce::String)> showStatus);
    ~ControlStrip() override;

    void refresh(); // called by the editor's timer
    void paint (juce::Graphics&) override;
    void resized() override;

    void showPitchOptions();
    std::unique_ptr<juce::Component> createPitchOptions();

private:
    void showDiMenu();
    void chooseDiFile();

    PedalAudioProcessor& processor;
    PitchParams& pitch;
    std::function<void (juce::String)> showStatus;

    StripKnob input, gate, pitchKnob, bass, middle, treble, output;
    PowerButton gatePower, eqPower, pitchPower;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateAttachment, eqAttachment;
    IconButton pitchMore { "opcoes", Icons::dots, true };
    juce::TextButton diButton { "DI" };
    std::unique_ptr<juce::FileChooser> chooser;

    struct RightClickOpener;
    std::unique_ptr<RightClickOpener> rightClick;
};

} // namespace pedal::ui

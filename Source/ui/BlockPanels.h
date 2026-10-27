#pragma once

#include "PluginProcessor.h"
#include "ui/SignalChainView.h"
#include "ui/Widgets.h"

namespace pedal::ui
{

// The drop tuning shortcuts used by the drop block and by the stage mode.
inline constexpr int dropShortcuts[] = { 0, -1, -2, -3, -4, -5, -7, -12 };

inline juce::String dropName (int semitones)
{
    return semitones == 0 ? tr ("Padrão") : (semitones > 0 ? "+" : "") + juce::String (semitones);
}

// Controls of the selected block, shown under the signal chain.
// One panel per block; the editor shows the one that is selected.
class BlockPanel : public juce::Component
{
public:
    // Called by the editor's timer so panels can show live values.
    virtual void refresh() {}
    void paint (juce::Graphics&) override;

protected:
    explicit BlockPanel (juce::String panelTitle) : title (std::move (panelTitle)) {}
    juce::Rectangle<int> getContentArea() const { return getLocalBounds().reduced (20).withTrimmedTop (34); }

private:
    juce::String title;
};

// Panel made of knobs only (input, gate, output).
class KnobPanel : public BlockPanel
{
public:
    KnobPanel (juce::String title, juce::AudioProcessorValueTreeState&,
               std::initializer_list<std::pair<const char*, const char*>> paramIdsAndNames,
               juce::String hint = {});
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::OwnedArray<ParamKnob> knobs;
    juce::String hint;
};

// Input gain plus the DI test player (a recorded clean track played instead of
// the live input, to try sounds without an audio interface).
class InputPanel : public BlockPanel
{
public:
    InputPanel (PedalAudioProcessor&, std::function<void (juce::String)> showStatus);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;

private:
    void chooseFile();

    PedalAudioProcessor& processor;
    std::function<void (juce::String)> showStatus;
    ParamKnob gain;
    juce::TextButton fileButton { "Arquivo DI..." }, playButton { "Tocar DI" };
    std::unique_ptr<juce::FileChooser> chooser;
};

class DropPanel : public BlockPanel
{
public:
    explicit DropPanel (PedalAudioProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;

private:
    PedalAudioProcessor& processor;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::Slider fine { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fineAttachment;
    juce::String latencyText;
};

class AmpPanel : public BlockPanel
{
public:
    AmpPanel (PedalAudioProcessor&, std::function<void (juce::String)> showStatus);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;

private:
    void chooseFile();

    PedalAudioProcessor& processor;
    std::function<void (juce::String)> showStatus;
    juce::TextButton openButton { "Abrir captura (.nam)" };
    juce::TextButton tone3000Button { "Buscar no TONE3000" };
    ParamKnob blend;
    juce::String modelName;
    std::unique_ptr<juce::FileChooser> chooser;
};

class CabPanel : public BlockPanel
{
public:
    explicit CabPanel (PedalAudioProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;

private:
    PedalAudioProcessor& processor;
    juce::TextButton openButton { "Abrir IR (.wav)" };
    juce::ToggleButton enabled { "Caixa ligada" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment;
    juce::String cabName;
    std::unique_ptr<juce::FileChooser> chooser;
};

// Builds the panel for a block.
std::unique_ptr<BlockPanel> createBlockPanel (BlockId, PedalAudioProcessor&, std::function<void (juce::String)> showStatus);

} // namespace pedal::ui

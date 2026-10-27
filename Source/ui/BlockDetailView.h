#pragma once

#include "PluginProcessor.h"
#include "ui/CardArt.h"

namespace pedal::ui
{

// Opened by tapping a card: big artwork, the capture name and where it came
// from, arrows to try the previous / next file of the same folder, and the
// block's own controls.
class BlockDetailView : public juce::Component
{
public:
    using Kind = CardArt::Kind;

    BlockDetailView (PedalAudioProcessor&, std::function<void (juce::String)> showStatus);

    std::function<void()> onBack, onBrowseTone3000;

    void show (Kind);
    Kind getKind() const noexcept { return kind; }
    void refresh(); // called by the editor's timer

    void paint (juce::Graphics&) override;
    void resized() override;

    // Opens a file chooser for this kind of block and loads the result.
    void chooseFile (Kind);
    void loadFile (Kind, const juce::File&);

private:
    juce::File getCurrentFile() const;
    void step (int delta);
    void updateSiblings();

    PedalAudioProcessor& processor;
    std::function<void (juce::String)> showStatus;
    Kind kind = Kind::amp;

    IconButton backButton { "voltar", Icons::back };
    PowerButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    juce::TextButton changeButton { "Trocar arquivo..." }, tone3000Button { "Buscar no TONE3000" };
    IconButton prevButton { "anterior", [] (auto r) { return Icons::chevron (r, false); } };
    IconButton nextButton { "proxima", [] (auto r) { return Icons::chevron (r, true); } };
    StripKnob mix;

    juce::Rectangle<int> panelArea, artArea, infoArea, navArea;
    juce::String name, source;
    juce::Array<juce::File> siblings;
    juce::File siblingsFor;
    int siblingIndex = -1;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace pedal::ui

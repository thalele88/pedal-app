#pragma once

#include "ui/CardArt.h"

namespace pedal::ui
{

// The signal chain as big picture cards on a cable (TONE3000 plugin style).
// An empty slot is a dashed card with a "+": tapping it adds a capture.
// A filled card opens the block view. Files dropped anywhere on it go to the
// right slot: .nam to the amp, audio files (IR) to the cabinet.
class ChainView : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    using Kind = CardArt::Kind;

    struct CardState
    {
        juce::String name;      // empty = slot is empty
        juce::String subtitle;  // e.g. "NAM · A2" or "IR"
        bool canBypass = false;
        bool enabled = true;
    };

    ChainView();
    ~ChainView() override;

    std::function<void (Kind)> onOpen, onAdd, onTogglePower;
    std::function<void (const juce::File&)> onAmpFileDropped, onCabFileDropped;

    void setCard (Kind, const CardState&);

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    static bool isCaptureFile (const juce::File& f) { return f.hasFileExtension ("nam"); }
    static bool isImpulseFile (const juce::File& f) { return f.hasFileExtension ("wav;aif;aiff;flac"); }

private:
    class Card;
    juce::OwnedArray<Card> cards;
    juce::Rectangle<int> lane;
    bool dragOver = false;
};

} // namespace pedal::ui

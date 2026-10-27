#pragma once

#include "ui/PitchParams.h"
#include "ui/PresetSource.h"
#include "ui/Widgets.h"

namespace pedal::ui
{

// Stage screen: huge preset name, 4 or 8 giant preset buttons per bank,
// drop tuning one tap away and the tuner in the corner. Built for touch on
// an iPad and for being read from a few metres away.
//
// Buttons react on touch-down (not release) so a preset change lands on the
// beat. Leaving the screen needs a press-and-hold, so it can't happen by accident.
//
// Keyboard / Bluetooth page-turner pedals: Left/Right or PageUp/PageDown step
// through presets, Up/Down change bank, 1-8 pick a slot, T opens the tuner.
class StageView : public juce::Component,
                  private juce::ChangeListener
{
public:
    StageView (PresetSource&, PitchParams&);
    ~StageView() override;

    std::function<void()> onExit;
    std::function<void()> onTuner;

    // Called by the editor's timer.
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    class Slot;
    class NavButton;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    int getNumBanks() const;
    void setBank (int);
    void followCurrentPreset();
    void stepPreset (int delta);
    void selectSlot (int slot);
    void setSlotsPerBank (int);

    PresetSource& presets;
    PitchParams& pitch;

    int slotsPerBank = 4;
    int bank = 0;

    juce::OwnedArray<Slot> slots;
    std::unique_ptr<NavButton> prevBank, nextBank;
    juce::OwnedArray<juce::TextButton> dropButtons;
    juce::TextButton tunerButton { "AFINADOR" }, layoutButton { "4 / 8" };
    HoldButton exitButton { "Segure para sair" };

    juce::Rectangle<int> nameArea, bankLabelArea;
    int lastDrop = 0;
};

} // namespace pedal::ui

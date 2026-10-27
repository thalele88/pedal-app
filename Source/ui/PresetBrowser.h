#pragma once

#include "ui/PresetSource.h"
#include "ui/Widgets.h"

namespace pedal::ui
{

// Preset list on the left of the main screen, in the spirit of ToneX:
// search, favourites filter, one tap loads a preset.
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::ChangeListener
{
public:
    explicit PresetBrowser (PresetSource&);
    ~PresetBrowser() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Asks for a name and saves the current sound as a new preset.
    void askNameAndSave();

private:
    int getNumRows() override { return visible.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuild();

    PresetSource& source;
    juce::Array<int> visible; // indices into the source after filtering

    juce::TextEditor search;
    juce::TextButton favouritesOnly { "Favoritos" };
    juce::ListBox list { "presets", this };
    juce::TextButton saveButton { "Salvar" }, saveAsButton { "Salvar como..." };
    std::unique_ptr<juce::AlertWindow> nameDialog;
};

} // namespace pedal::ui

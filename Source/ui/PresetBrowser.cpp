#include "ui/PresetBrowser.h"

namespace pedal::ui
{

namespace
{
constexpr int headerHeight = 40;
constexpr int starWidth = 36;

void drawStar (juce::Graphics& g, juce::Rectangle<float> area, bool filled)
{
    juce::Path star;
    star.addStar (area.getCentre(), 5, area.getWidth() * 0.2f, area.getWidth() * 0.45f);
    if (filled)
    {
        g.setColour (Colours::warn);
        g.fillPath (star);
    }
    else
    {
        g.setColour (Colours::textDim.withAlpha (0.5f));
        g.strokePath (star, juce::PathStrokeType (1.2f));
    }
}
} // namespace

PresetBrowser::PresetBrowser (PresetSource& s) : source (s)
{
    search.setTextToShowWhenEmpty ("Buscar preset", Colours::textDim);
    search.setColour (juce::TextEditor::backgroundColourId, Colours::panelRaised);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::textColourId, Colours::text);
    search.setFont (juce::FontOptions (16.0f));
    search.setIndents (10, 8);
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);

    favouritesOnly.setClickingTogglesState (true);
    favouritesOnly.onClick = [this] { rebuild(); };
    addAndMakeVisible (favouritesOnly);

    list.setRowHeight (56);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    saveButton.onClick = [this] { source.saveCurrent(); };
    saveAsButton.onClick = [this] { askNameAndSave(); };
    addAndMakeVisible (saveButton);
    addAndMakeVisible (saveAsButton);

    source.addChangeListener (this);
    rebuild();
}

PresetBrowser::~PresetBrowser()
{
    source.removeChangeListener (this);
}

void PresetBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuild();
}

void PresetBrowser::rebuild()
{
    visible.clearQuick();
    const auto query = search.getText().trim();
    for (int i = 0; i < source.getNumPresets(); ++i)
    {
        const auto info = source.getPresetInfo (i);
        if (favouritesOnly.getToggleState() && ! info.favourite)
            continue;
        if (query.isNotEmpty() && ! info.name.containsIgnoreCase (query) && ! info.ampName.containsIgnoreCase (query)
            && ! info.category.containsIgnoreCase (query))
            continue;
        visible.add (i);
    }

    const int current = source.getCurrentIndex();
    saveButton.setEnabled (source.canSave() && current >= 0);
    saveAsButton.setEnabled (source.canSave());

    list.updateContent();
    list.repaint();
    if (const int row = visible.indexOf (current); row >= 0)
        list.scrollToEnsureRowIsOnscreen (row);
    repaint();
}

void PresetBrowser::askNameAndSave()
{
    nameDialog = std::make_unique<juce::AlertWindow> ("Salvar preset", "Nome do novo preset:", juce::MessageBoxIconType::NoIcon);
    nameDialog->addTextEditor ("name", "Preset " + juce::String (source.getNumPresets() + 1));
    nameDialog->addButton ("Salvar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    nameDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && nameDialog != nullptr)
            source.saveCurrentAs (nameDialog->getTextEditorContents ("name").trim());
        nameDialog.reset();
    }), false);
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (! juce::isPositiveAndBelow (row, visible.size()))
        return;

    const int index = visible[row];
    const auto info = source.getPresetInfo (index);
    const bool isCurrent = index == source.getCurrentIndex();
    auto r = juce::Rectangle<int> (width, height).reduced (2, 3);

    g.setColour (isCurrent ? Colours::panelRaised.brighter (0.1f) : Colours::panelRaised.withAlpha (0.5f));
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    if (isCurrent)
    {
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (r.removeFromLeft (5).toFloat(), 2.0f);
    }
    else
    {
        r.removeFromLeft (5);
    }

    drawStar (g, r.removeFromRight (starWidth).toFloat().withSizeKeepingCentre (20.0f, 20.0f), info.favourite);

    auto text = r.reduced (10, 6);
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::String (index + 1).paddedLeft ('0', 2), text.removeFromLeft (28), juce::Justification::centredLeft);

    g.setColour (isCurrent ? Colours::accent : Colours::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (info.name, text.removeFromTop (text.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);

    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (info.ampName.isNotEmpty() ? info.ampName : info.category, text, juce::Justification::topLeft, true);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, visible.size()))
        return;

    const int index = visible[row];
    if (e.x >= list.getVisibleRowWidth() - starWidth - 2)
        source.setFavourite (index, ! source.getPresetInfo (index).favourite);
    else
        source.selectPreset (index);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

    auto header = getLocalBounds().reduced (14, 0).removeFromTop (headerHeight);
    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("PRESETS", header, juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (source.getListName(), header, juce::Justification::centredRight);

    if (visible.isEmpty())
    {
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (15.0f));
        const auto message = source.getNumPresets() == 0
                                 ? tr ("Nenhum preset ainda.\nMonte o som e toque em \"Salvar como...\".")
                                 : tr ("Nada encontrado.");
        g.drawFittedText (message, list.getBounds().reduced (12), juce::Justification::centredTop, 4);
    }
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (headerHeight - 10);

    auto buttons = area.removeFromBottom (44);
    saveButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
    buttons.removeFromLeft (8);
    saveAsButton.setBounds (buttons);
    area.removeFromBottom (8);

    auto tools = area.removeFromTop (36);
    favouritesOnly.setBounds (tools.removeFromRight (110));
    tools.removeFromRight (8);
    search.setBounds (tools);
    area.removeFromTop (8);

    list.setBounds (area);
}

} // namespace pedal::ui

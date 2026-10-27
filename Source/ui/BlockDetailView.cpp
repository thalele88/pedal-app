#include "ui/BlockDetailView.h"

namespace pedal::ui
{

BlockDetailView::BlockDetailView (PedalAudioProcessor& p, std::function<void (juce::String)> status)
    : processor (p), showStatus (std::move (status)), mix (p.parameters, ParamIDs::cleanBlend, "Mix limpo")
{
    backButton.label = "BLOCO";
    backButton.onClick = [this] { if (onBack) onBack(); };
    setHint (backButton, tr ("Volta para a cadeia de sinal."));
    addAndMakeVisible (backButton);

    addAndMakeVisible (power);

    changeButton.onClick = [this] { chooseFile (kind); };
    addAndMakeVisible (changeButton);

    tone3000Button.onClick = [this] { if (onBrowseTone3000) onBrowseTone3000(); };
    setHint (tone3000Button, tr ("Abre o catálogo do TONE3000 aqui dentro do app para escolher outra captura."));
    addAndMakeVisible (tone3000Button);

    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    setHint (prevButton, tr ("Carrega o arquivo anterior da mesma pasta. Bom para comparar capturas rapidinho."));
    setHint (nextButton, tr ("Carrega o próximo arquivo da mesma pasta. Bom para comparar capturas rapidinho."));
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    setHint (mix, tr ("Mix limpo: mistura o som sem amp com o amp. Ajuda o baixo a manter o grave firme."));
    addAndMakeVisible (mix);

    show (Kind::amp);
}

void BlockDetailView::show (Kind newKind)
{
    kind = newKind;
    const bool isAmp = kind == Kind::amp;

    powerAttachment.reset();
    power.setVisible (! isAmp);
    if (! isAmp)
    {
        powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, ParamIDs::cabEnabled, power);
        setHint (power, tr ("Liga e desliga a caixa sem perder o IR escolhido."));
    }

    tone3000Button.setVisible (isAmp);
    mix.setVisible (isAmp);
    setHint (changeButton, isAmp ? tr ("Escolhe outra captura .nam do computador.") : tr ("Escolhe outro IR de caixa (.wav) do computador."));

    siblingsFor = juce::File();
    refresh();
    resized();
    repaint();
}

juce::File BlockDetailView::getCurrentFile() const
{
    return kind == Kind::amp ? processor.getAmpModelFile() : processor.getCabinetFile();
}

void BlockDetailView::refresh()
{
    const auto file = getCurrentFile();
    juce::String newName, newSource;

    if (kind == Kind::amp)
    {
        newName = processor.getAmpModelName();
        newSource = file != juce::File() ? file.getParentDirectory().getFullPathName()
                                         : (newName.isNotEmpty() ? juce::String ("TONE3000") : juce::String());
    }
    else if (file != juce::File())
    {
        newName = file.getFileNameWithoutExtension();
        newSource = file.getParentDirectory().getFullPathName();
    }

    if (file != siblingsFor)
        updateSiblings();

    if (newName != name || newSource != source)
    {
        name = newName;
        source = newSource;
        repaint();
    }
}

void BlockDetailView::updateSiblings()
{
    siblingsFor = getCurrentFile();
    siblings.clearQuick();
    siblingIndex = -1;

    if (siblingsFor.existsAsFile())
    {
        const auto pattern = kind == Kind::amp ? juce::String ("*.nam") : juce::String ("*.wav;*.aif;*.aiff;*.flac");
        siblings = siblingsFor.getParentDirectory().findChildFiles (juce::File::findFiles, false, pattern);
        siblings.sort();
        siblingIndex = siblings.indexOf (siblingsFor);
    }

    prevButton.setEnabled (siblingIndex > 0);
    nextButton.setEnabled (siblingIndex >= 0 && siblingIndex < siblings.size() - 1);
    repaint (navArea);
}

void BlockDetailView::step (int delta)
{
    const int next = siblingIndex + delta;
    if (juce::isPositiveAndBelow (next, siblings.size()))
        loadFile (kind, siblings[next]);
}

void BlockDetailView::chooseFile (Kind forKind)
{
    const bool isAmp = forKind == Kind::amp;
    chooser = std::make_unique<juce::FileChooser> (isAmp ? "Escolha uma captura NAM" : "Escolha um IR de caixa",
                                                   getCurrentFile().getParentDirectory(),
                                                   isAmp ? "*.nam" : "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, forKind] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file != juce::File())
            loadFile (forKind, file);
    });
}

void BlockDetailView::loadFile (Kind forKind, const juce::File& file)
{
    if (forKind == Kind::cab)
    {
        processor.loadCabinet (file);
        setParameter (processor.parameters, ParamIDs::cabEnabled, 1.0f);
        refresh();
        return;
    }

    showStatus ("Carregando " + file.getFileNameWithoutExtension() + "...");
    juce::Component::SafePointer<BlockDetailView> safeThis (this);
    processor.loadAmpModelAsync (file, [safeThis] (juce::String error)
    {
        if (safeThis != nullptr)
        {
            safeThis->showStatus (error);
            safeThis->refresh();
        }
    });
}

void BlockDetailView::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel.withAlpha (0.6f));
    g.fillRoundedRectangle (panelArea.toFloat(), 16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (panelArea.toFloat(), 16.0f, 1.0f);

    const bool isAmp = kind == Kind::amp;
    const bool empty = name.isEmpty();

    if (empty)
    {
        g.setColour (Colours::panelRaised);
        g.fillRoundedRectangle (artArea.toFloat(), 12.0f);
        g.setColour (Colours::textDim);
        g.strokePath (Icons::plus (artArea.toFloat()), juce::PathStrokeType (2.0f));
    }
    else
    {
        CardArt::draw (g, artArea.toFloat(), kind, name, 12.0f);
    }

    auto info = infoArea;
    g.setColour (empty ? Colours::textDim : Colours::text);
    g.setFont (juce::FontOptions (juce::jlimit (20.0f, 30.0f, (float) info.getHeight() * 0.14f), juce::Font::bold));
    g.drawFittedText (empty ? (isAmp ? tr ("Nenhuma captura ainda") : tr ("Nenhum IR ainda")) : name,
                      info.removeFromTop (info.getHeight() * 2 / 5), juce::Justification::bottomLeft, 2);
    info.removeFromTop (10);

    // Type chip
    const auto chip = isAmp ? tr ("AMP  ·  NAM") : tr ("CAIXA  ·  IR");
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    auto chipRow = info.removeFromTop (26);
    auto chipBox = chipRow.removeFromLeft (120);
    g.setColour (Colours::panelRaised);
    g.fillRoundedRectangle (chipBox.toFloat(), 6.0f);
    g.setColour (Colours::textDim);
    g.drawText (chip, chipBox, juce::Justification::centred);
    info.removeFromTop (10);

    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (empty ? (isAmp ? tr ("Abra um arquivo .nam do computador ou busque um tom no TONE3000.")
                                     : tr ("Abra um IR de caixa (.wav). Sem IR, use capturas \"full rig\", que já incluem a caixa."))
                            : source,
                      info.removeFromTop (44), juce::Justification::topLeft, 2);

    if (isAmp && processor.ampHasSampleRateMismatch())
    {
        g.setColour (Colours::warn);
        g.drawFittedText (tr ("Esta captura foi treinada em outra taxa de amostragem; o som pode mudar um pouco."),
                          info.removeFromTop (40), juce::Justification::topLeft, 2);
    }

    // Navigator: file name between the arrows, and the position in the folder
    g.setColour (Colours::panelRaised);
    g.fillRoundedRectangle (navArea.toFloat(), 10.0f);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (15.0f));
    auto nav = navArea.reduced (52, 0);
    const auto count = siblingIndex >= 0 ? juce::String (siblingIndex + 1) + "/" + juce::String (siblings.size()) : juce::String();
    g.setColour (Colours::textDim);
    g.drawText (count, nav.removeFromRight (60), juce::Justification::centredRight);
    g.setColour (Colours::text);
    g.drawText (siblingIndex >= 0 ? siblings[siblingIndex].getFileName() : tr ("Sem outros arquivos para comparar"),
                nav, juce::Justification::centred, true);
}

void BlockDetailView::resized()
{
    auto area = getLocalBounds().reduced (8);
    backButton.setBounds (area.removeFromTop (40).removeFromLeft (130));
    area.removeFromTop (10);

    panelArea = area;
    auto inner = area.reduced (20);

    auto top = inner.removeFromTop (34);
    power.setBounds (top.removeFromLeft (34));
    inner.removeFromTop (6);

    auto bottom = inner.removeFromBottom (juce::jmin (120, inner.getHeight() / 3));
    if (mix.isVisible())
        mix.setBounds (bottom.removeFromRight (100));
    navArea = bottom.withSizeKeepingCentre (bottom.getWidth() - 20, 48).withX (bottom.getX());
    prevButton.setBounds (navArea.removeFromLeft (48).reduced (4));
    nextButton.setBounds (navArea.removeFromRight (48).reduced (4));
    navArea = navArea.expanded (48, 0);
    inner.removeFromBottom (12);

    const int side = juce::jmin (inner.getHeight(), inner.getWidth() / 2);
    artArea = inner.removeFromLeft (side).withSizeKeepingCentre (side, side);
    inner.removeFromLeft (24);
    infoArea = inner;

    auto buttons = infoArea.removeFromBottom (44);
    changeButton.setBounds (buttons.removeFromLeft (190));
    buttons.removeFromLeft (10);
    if (tone3000Button.isVisible())
        tone3000Button.setBounds (buttons.removeFromLeft (200));
    infoArea.removeFromBottom (10);
}

} // namespace pedal::ui

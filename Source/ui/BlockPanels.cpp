#include "ui/BlockPanels.h"

namespace pedal::ui
{

namespace
{
void drawBigName (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, const juce::String& placeholder)
{
    g.setColour (text.isEmpty() ? Colours::textDim : Colours::text);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawFittedText (text.isEmpty() ? placeholder : text, area, juce::Justification::centredLeft, 2);
}
} // namespace

//==============================================================================
void BlockPanel::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().reduced (20, 14).removeFromTop (22), juce::Justification::centredLeft);
}

//==============================================================================
KnobPanel::KnobPanel (juce::String panelTitle, juce::AudioProcessorValueTreeState& state,
                      std::initializer_list<std::pair<const char*, const char*>> params, juce::String hintText)
    : BlockPanel (std::move (panelTitle)), hint (std::move (hintText))
{
    for (const auto& [id, name] : params)
        addAndMakeVisible (knobs.add (new ParamKnob (state, id, tr (name))));
}

void KnobPanel::paint (juce::Graphics& g)
{
    BlockPanel::paint (g);
    if (hint.isNotEmpty())
    {
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (hint, getContentArea().removeFromBottom (22), juce::Justification::centred, 1);
    }
}

void KnobPanel::resized()
{
    auto area = getContentArea();
    if (hint.isNotEmpty())
        area.removeFromBottom (26);
    const int size = juce::jmin (area.getHeight(), 170);
    const int total = size * knobs.size() + 30 * (knobs.size() - 1);
    auto row = area.withSizeKeepingCentre (juce::jmin (total, area.getWidth()), size);
    for (auto* k : knobs)
    {
        k->setBounds (row.removeFromLeft (size));
        row.removeFromLeft (30);
    }
}

//==============================================================================
InputPanel::InputPanel (PedalAudioProcessor& p, std::function<void (juce::String)> status)
    : BlockPanel ("ENTRADA"), processor (p), showStatus (std::move (status)), gain (p.parameters, ParamIDs::inputGain, "GANHO")
{
    addAndMakeVisible (gain);

    fileButton.setTooltip (tr ("Escolha uma gravação limpa de guitarra ou baixo para tocar sem interface"));
    fileButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (fileButton);

    playButton.onClick = [this]
    {
        auto& di = processor.getTestInput();
        if (! di.hasClip())
            chooseFile();
        else
            di.setPlaying (! di.isPlaying());
    };
    addAndMakeVisible (playButton);
}

void InputPanel::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Escolha uma gravação DI (guitarra ou baixo limpo)"),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::Component::SafePointer<InputPanel> safeThis (this);
        processor.loadTestInputAsync (file, [safeThis] (juce::String error)
        {
            if (safeThis == nullptr)
                return;
            safeThis->showStatus (error);
            if (error.isEmpty())
                safeThis->processor.getTestInput().setPlaying (true);
        });
    });
}

void InputPanel::refresh()
{
    const auto& di = processor.getTestInput();
    playButton.setButtonText (di.isPlaying() ? juce::String ("Parar DI") : juce::String ("Tocar DI"));
    playButton.setToggleState (di.isPlaying(), juce::dontSendNotification);
    fileButton.setButtonText (di.hasClip() ? di.getFile().getFileNameWithoutExtension() : juce::String ("Arquivo DI..."));
}

void InputPanel::paint (juce::Graphics& g)
{
    BlockPanel::paint (g);
    auto area = getContentArea().withTrimmedLeft (200);
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (tr ("Ajuste o ganho até os acordes mais fortes não estourarem.\n\n"
                          "Sem interface de áudio? Toque uma gravação DI (guitarra ou baixo limpo) "
                          "em loop pela cadeia no lugar da entrada."),
                      area.removeFromTop (90), juce::Justification::topLeft, 5);
}

void InputPanel::resized()
{
    auto area = getContentArea();
    gain.setBounds (area.removeFromLeft (170).withHeight (juce::jmin (area.getHeight(), 170)));
    area.removeFromLeft (30);
    area.removeFromTop (100);
    auto row = area.removeFromTop (52);
    fileButton.setBounds (row.removeFromLeft (220));
    row.removeFromLeft (12);
    playButton.setBounds (row.removeFromLeft (160));
}

//==============================================================================
DropPanel::DropPanel (PedalAudioProcessor& p) : BlockPanel (tr ("AFINAÇÃO (DROP)")), processor (p)
{
    for (int st : dropShortcuts)
    {
        auto* b = buttons.add (new juce::TextButton (dropName (st)));
        b->onClick = [this, st] { setParameter (processor.parameters, ParamIDs::drop, (float) st); };
        addAndMakeVisible (b);
    }

    fine.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 26);
    fine.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (fine);
    fineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, ParamIDs::drop, fine);
}

void DropPanel::refresh()
{
    const int drop = juce::roundToInt (getParameter (processor.parameters, ParamIDs::drop));
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (dropShortcuts[i] == drop, juce::dontSendNotification);

    const double sr = processor.getSampleRate();
    juce::String text;
    if (drop != 0 && sr > 0.0)
        text = tr ("Latência extra com o drop ligado: ") + juce::String (1000.0 * processor.getLatencySamples() / sr, 0) + " ms";
    else
        text = tr ("Desligado: o som passa direto, sem latência extra.");

    if (text != latencyText)
    {
        latencyText = text;
        repaint();
    }
}

void DropPanel::paint (juce::Graphics& g)
{
    BlockPanel::paint (g);
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (latencyText, getContentArea().removeFromBottom (22), juce::Justification::centredLeft, 1);
}

void DropPanel::resized()
{
    auto area = getContentArea();
    area.removeFromBottom (26);
    auto row = area.removeFromTop (juce::jmin (64, area.getHeight() / 2));
    const int w = row.getWidth() / buttons.size();
    for (auto* b : buttons)
        b->setBounds (row.removeFromLeft (w).reduced (4));
    area.removeFromTop (12);
    fine.setBounds (area.removeFromTop (36).reduced (4, 0));
}

//==============================================================================
AmpPanel::AmpPanel (PedalAudioProcessor& p, std::function<void (juce::String)> status)
    : BlockPanel ("AMP (CAPTURA NAM)"), processor (p), showStatus (std::move (status)),
      blend (p.parameters, ParamIDs::cleanBlend, "MIX LIMPO")
{
    openButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (openButton);

    // The online catalogue comes with the TONE3000 integration (feat/tone3000).
    tone3000Button.setEnabled (false);
    tone3000Button.setTooltip ("Em breve");
    addAndMakeVisible (tone3000Button);

    addAndMakeVisible (blend);
}

void AmpPanel::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Escolha uma captura NAM", juce::File(), "*.nam");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        showStatus ("Carregando " + file.getFileNameWithoutExtension() + "...");
        juce::Component::SafePointer<AmpPanel> safeThis (this);
        processor.loadAmpModelAsync (file, [safeThis] (juce::String error)
        {
            if (safeThis != nullptr)
                safeThis->showStatus (error);
        });
    });
}

void AmpPanel::refresh()
{
    const auto name = processor.getAmpModelName();
    if (name != modelName)
    {
        modelName = name;
        repaint();
    }
}

void AmpPanel::paint (juce::Graphics& g)
{
    BlockPanel::paint (g);
    auto area = getContentArea();
    drawBigName (g, area.removeFromTop (60).withTrimmedRight (200), modelName, "Nenhuma captura carregada");
}

void AmpPanel::resized()
{
    auto area = getContentArea();
    blend.setBounds (area.removeFromRight (180).withTrimmedBottom (juce::jmax (0, area.getHeight() - 180)));
    area.removeFromTop (70);
    auto row = area.removeFromTop (52);
    openButton.setBounds (row.removeFromLeft (220));
    row.removeFromLeft (12);
    tone3000Button.setBounds (row.removeFromLeft (220));
}

//==============================================================================
CabPanel::CabPanel (PedalAudioProcessor& p) : BlockPanel ("CAIXA (IR)"), processor (p)
{
    openButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Escolha um IR de caixa", juce::File(), "*.wav;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file != juce::File())
                processor.loadCabinet (file);
        });
    };
    addAndMakeVisible (openButton);
    addAndMakeVisible (enabled);
    enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.parameters, ParamIDs::cabEnabled, enabled);
}

void CabPanel::refresh()
{
    const auto file = processor.getCabinetFile();
    const auto name = file == juce::File() ? juce::String() : file.getFileNameWithoutExtension();
    if (name != cabName)
    {
        cabName = name;
        repaint();
    }
}

void CabPanel::paint (juce::Graphics& g)
{
    BlockPanel::paint (g);
    drawBigName (g, getContentArea().removeFromTop (60), cabName, "Nenhum IR carregado");
}

void CabPanel::resized()
{
    auto area = getContentArea();
    area.removeFromTop (70);
    auto row = area.removeFromTop (52);
    openButton.setBounds (row.removeFromLeft (220));
    row.removeFromLeft (16);
    enabled.setBounds (row.removeFromLeft (200));
}

//==============================================================================
std::unique_ptr<BlockPanel> createBlockPanel (BlockId id, PedalAudioProcessor& p, std::function<void (juce::String)> showStatus)
{
    switch (id)
    {
        case BlockId::input:  return std::make_unique<InputPanel> (p, std::move (showStatus));
        case BlockId::gate:   return std::make_unique<KnobPanel> ("NOISE GATE", p.parameters,
                                                                 std::initializer_list<std::pair<const char*, const char*>> { { ParamIDs::gateThreshold, "LIMIAR" } },
                                                                 tr ("No mínimo o gate fica desligado."));
        case BlockId::drop:   return std::make_unique<DropPanel> (p);
        case BlockId::amp:    return std::make_unique<AmpPanel> (p, std::move (showStatus));
        case BlockId::cab:    return std::make_unique<CabPanel> (p);
        case BlockId::output: return std::make_unique<KnobPanel> (tr ("SAÍDA"), p.parameters,
                                                                 std::initializer_list<std::pair<const char*, const char*>> { { ParamIDs::outputGain, "VOLUME" } });
    }
    return nullptr;
}

} // namespace pedal::ui

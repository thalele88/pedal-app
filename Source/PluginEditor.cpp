#include "PluginEditor.h"

using namespace pedal::ui;

namespace
{
// Shown in the TONE3000 tab until the in-app catalogue (Tone3000Panel) lands.
class Tone3000Placeholder : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (8).toFloat();
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("TONE3000", r.withTrimmedBottom (r.getHeight() / 2).toNearestInt(), juce::Justification::centredBottom);
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (tr ("Aqui vai abrir o catálogo do TONE3000 dentro do app: escolha um tom e ele carrega direto no amp, sem baixar nada.\n"
                              "Esta parte chega com a integração do TONE3000."),
                          r.withTrimmedTop (r.getHeight() / 2 + 10).reduced (40.0f, 0.0f).toNearestInt(), juce::Justification::centredTop, 4);
    }
};
} // namespace

// Feeds the hint bar with the tip of whatever is under the mouse or finger.
struct PedalAudioProcessorEditor::HintTracker : public juce::MouseListener
{
    explicit HintTracker (PedalAudioProcessorEditor& e) : editor (e) { juce::Desktop::getInstance().addGlobalMouseListener (this); }
    ~HintTracker() override { juce::Desktop::getInstance().removeGlobalMouseListener (this); }

    void update (const juce::MouseEvent& e)
    {
        auto* c = e.eventComponent;
        if (c != nullptr && (c == &editor || editor.isParentOf (c)))
            editor.hintBar.setHint (findHint (c));
    }

    void mouseMove (const juce::MouseEvent& e) override { update (e); }
    void mouseDown (const juce::MouseEvent& e) override { update (e); }
    void mouseEnter (const juce::MouseEvent& e) override { update (e); }

    PedalAudioProcessorEditor& editor;
};

PedalAudioProcessorEditor::PedalAudioProcessorEditor (PedalAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), pitch (p.parameters),
      browser (p.getPresetSource()),
      block (p, [this] (juce::String s) { showStatus (s); }),
      controls (p, pitch, [this] (juce::String s) { showStatus (s); }),
      stage (p.getPresetSource(), pitch), tuner (p)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    // Header
    logo.setText ("PEDAL APP", juce::dontSendNotification);
    logo.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    logo.setColour (juce::Label::textColourId, Colours::accent);
    addAndMakeVisible (logo);

    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    savePreset.onClick = [this]
    {
        auto& presets = processor.getPresetSource();
        if (presets.getCurrentIndex() >= 0)
        {
            presets.saveCurrent();
            showStatus ("Preset salvo.");
        }
        else
        {
            browser.askNameAndSave();
        }
    };
    newPreset.onClick = [this] { browser.askNameAndSave(); };
    tunerButton.label = "AFINADOR";
    tunerButton.onClick = [this] { showTuner (true); };
    stageButton.onClick = [this] { showStage (true); };
    stageButton.setColour (juce::TextButton::buttonColourId, Colours::accent.darker (0.3f));
    for (juce::Component* c : { (juce::Component*) &prevPreset, (juce::Component*) &nextPreset, (juce::Component*) &savePreset,
                                (juce::Component*) &newPreset, (juce::Component*) &tunerButton, (juce::Component*) &stageButton })
        addAndMakeVisible (c);

    setHint (prevPreset, tr ("Preset anterior."));
    setHint (nextPreset, tr ("Próximo preset."));
    setHint (savePreset, tr ("Salva as mudanças no preset atual."));
    setHint (newPreset, tr ("Salva o som atual como um preset novo."));
    setHint (tunerButton, tr ("Abre o afinador (tecla T). A saída fica muda enquanto você afina."));
    setHint (stageButton, tr ("Modo palco: tela cheia com botões grandes para trocar de preset durante o show."));

    // Sidebar
    for (auto* tab : { &chainTab, &tone3000Tab })
    {
        tab->setClickingTogglesState (false);
        addAndMakeVisible (tab);
    }
    chainTab.onClick = [this] { showMain (MainView::chain); };
    tone3000Tab.onClick = [this] { showMain (MainView::tone3000); };
    setHint (chainTab, tr ("Mostra a cadeia de sinal: amp e caixa."));
    setHint (tone3000Tab, tr ("Abre o catálogo do TONE3000 dentro do app para escolher capturas online."));
    setHint (browser, tr ("Seus presets. Toque para carregar, na estrela para favoritar, e busque pelo nome ou pela captura."));
    addAndMakeVisible (browser);

    // Main area
    setHint (inMeter, tr ("Nível de entrada. O ideal é ficar no amarelo nos ataques mais fortes. Clique na luz de cima para apagar o aviso de clip."));
    setHint (outMeter, tr ("Nível de saída. Se acender o vermelho, baixe o Output."));
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    chain.onOpen = [this] (CardArt::Kind k) { openBlock (k); };
    chain.onAdd = [this] (CardArt::Kind k) { addToSlot (k); };
    chain.onTogglePower = [this] (CardArt::Kind)
    {
        setParameter (processor.parameters, ParamIDs::cabEnabled, getParameter (processor.parameters, ParamIDs::cabEnabled) > 0.5f ? 0.0f : 1.0f);
    };
    chain.onAmpFileDropped = [this] (const juce::File& f) { block.loadFile (CardArt::Kind::amp, f); };
    chain.onCabFileDropped = [this] (const juce::File& f) { block.loadFile (CardArt::Kind::cab, f); };
    addAndMakeVisible (chain);

    block.onBack = [this] { showMain (MainView::chain); };
    block.onBrowseTone3000 = [this] { showMain (MainView::tone3000); };
    addChildComponent (block);

    tone3000View = createTone3000View();
    addChildComponent (*tone3000View);

    addAndMakeVisible (controls);
    addAndMakeVisible (hintBar);

    // Full-screen overlays
    stage.onExit = [this] { showStage (false); };
    stage.onTuner = [this] { showTuner (true); };
    addChildComponent (stage);
    tuner.onClose = [this] { showTuner (false); };
    addChildComponent (tuner);

    hintTracker = std::make_unique<HintTracker> (*this);
    processor.getPresetSource().addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (900, 600, 3000, 2000);
    setSize (1180, 760);

    showMain (MainView::chain);
    timerCallback();
    startTimerHz (30);
}

PedalAudioProcessorEditor::~PedalAudioProcessorEditor()
{
    processor.getPresetSource().removeChangeListener (this);
    hintTracker.reset();
    showStage (false);
    setLookAndFeel (nullptr);
}

std::unique_ptr<juce::Component> PedalAudioProcessorEditor::createTone3000View()
{
    // When the TONE3000 integration is in, this becomes:
    //   auto panel = std::make_unique<pedal::tone3000::Tone3000Panel> (processor.getTone3000());
    //   panel->onModelChosen = [this] (auto&, auto&) { showMain (MainView::chain); };
    //   panel->onClose = [this] { showMain (MainView::chain); };
    // and showMain() calls panel->open() each time the tab is shown.
    return std::make_unique<Tone3000Placeholder>();
}

void PedalAudioProcessorEditor::showStatus (const juce::String& text)
{
    if (text.isNotEmpty())
        hintBar.flash (text);
}

void PedalAudioProcessorEditor::showMain (MainView view)
{
    mainView = view;
    chain.setVisible (view == MainView::chain);
    block.setVisible (view == MainView::block);
    tone3000View->setVisible (view == MainView::tone3000);

    chainTab.setToggleState (view != MainView::tone3000, juce::dontSendNotification);
    tone3000Tab.setToggleState (view == MainView::tone3000, juce::dontSendNotification);
    resized();
}

void PedalAudioProcessorEditor::openBlock (CardArt::Kind kind)
{
    block.show (kind);
    showMain (MainView::block);
}

void PedalAudioProcessorEditor::addToSlot (CardArt::Kind kind)
{
    if (kind == CardArt::Kind::cab)
    {
        block.chooseFile (kind);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("Adicionar amp");
    menu.addItem (1, tr ("Abrir captura .nam do computador..."));
    menu.addItem (2, tr ("Buscar no TONE3000"));
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this] (int result)
    {
        if (result == 1)
            block.chooseFile (CardArt::Kind::amp);
        else if (result == 2)
            showMain (MainView::tone3000);
    });
}

void PedalAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    presetModified = processor.getPresetSource().isModified();
    updatePresetName();
}

void PedalAudioProcessorEditor::stepPreset (int delta)
{
    auto& presets = processor.getPresetSource();
    const int count = presets.getNumPresets();
    if (count == 0)
    {
        showStatus (tr ("Ainda não há presets: monte o som e toque no + para salvar o primeiro."));
        return;
    }
    const int current = presets.getCurrentIndex();
    presets.selectPreset (current < 0 ? 0 : (current + delta + count) % count);
}

void PedalAudioProcessorEditor::showStage (bool shouldShow)
{
    stage.setVisible (shouldShow);
    if (shouldShow)
        stage.toFront (true);

    // The standalone app goes truly full screen; inside a DAW the stage fills the plugin window.
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        auto& desktop = juce::Desktop::getInstance();
        auto* window = getTopLevelComponent();
        if (shouldShow && desktop.getKioskModeComponent() == nullptr && window != this)
            desktop.setKioskModeComponent (window, false);
        else if (! shouldShow && desktop.getKioskModeComponent() == window)
            desktop.setKioskModeComponent (nullptr);
    }
}

void PedalAudioProcessorEditor::showTuner (bool shouldShow)
{
    tuner.setVisible (shouldShow);
    if (shouldShow)
        tuner.toFront (true);
    else if (stage.isVisible())
        stage.grabKeyboardFocus();
}

bool PedalAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')
    {
        showTuner (true);
        return true;
    }
    return false;
}

void PedalAudioProcessorEditor::updateCards()
{
    const auto amp = processor.getAmpModelName();
    const auto ampFile = processor.getAmpModelFile();
    ChainView::CardState ampCard;
    ampCard.name = amp;
    ampCard.subtitle = ampFile != juce::File() ? tr ("Amp  ·  NAM") : tr ("Amp  ·  NAM  ·  TONE3000");
    chain.setCard (CardArt::Kind::amp, ampCard);

    const auto cab = processor.getCabinetFile();
    ChainView::CardState cabCard;
    cabCard.name = cab == juce::File() ? juce::String() : cab.getFileNameWithoutExtension();
    cabCard.subtitle = tr ("Caixa  ·  IR");
    cabCard.canBypass = true;
    cabCard.enabled = getParameter (processor.parameters, ParamIDs::cabEnabled) > 0.5f;
    chain.setCard (CardArt::Kind::cab, cabCard);
}

void PedalAudioProcessorEditor::updatePresetName()
{
    auto& presets = processor.getPresetSource();
    const int current = presets.getCurrentIndex();
    const auto name = current >= 0 ? juce::String (current + 1).paddedLeft ('0', 2) + "  " + presets.getPresetInfo (current).name
                                         + (presetModified ? " *" : "")
                                   : juce::String ("Presets");
    if (name != presetName)
    {
        presetName = name;
        repaint (presetBox);
    }
}

void PedalAudioProcessorEditor::timerCallback()
{
    inMeter.push (processor.takeInputPeak());
    outMeter.push (processor.takeOutputPeak());

    // The rest does not need 30 updates a second.
    if (++tick % 3 != 0)
        return;

    // Comparing the state is not free, so check for edits a few times a second only.
    if (tick % 12 == 0)
        presetModified = processor.getPresetSource().isModified();
    updatePresetName();

    hintBar.setCpu (processor.getCpuLoad());
    hintBar.repaint(); // lets an expired status message go away

    updateCards();
    controls.refresh();
    if (block.isVisible())
        block.refresh();
    if (stage.isVisible())
        stage.refresh();
}

void PedalAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Header line and the preset selector box
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (60, 0.0f, (float) getWidth());

    g.setColour (Colours::panelRaised);
    g.fillRoundedRectangle (presetBox.toFloat(), 10.0f);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (presetName, presetBox.reduced (44, 0), juce::Justification::centred, true);
}

void PedalAudioProcessorEditor::resized()
{
    stage.setBounds (getLocalBounds());
    tuner.setBounds (getLocalBounds());

    auto area = getLocalBounds();

    // Header
    auto header = area.removeFromTop (60).reduced (16, 10);
    logo.setBounds (header.removeFromLeft (170));
    stageButton.setBounds (header.removeFromRight (110));
    header.removeFromRight (10);
    tunerButton.setBounds (header.removeFromRight (150));
    auto centre = header.withSizeKeepingCentre (juce::jmin (header.getWidth() - 20, 460), header.getHeight());
    auto icons = centre.removeFromRight (84);
    savePreset.setBounds (icons.removeFromLeft (40));
    newPreset.setBounds (icons.removeFromLeft (40));
    centre.removeFromRight (8);
    presetBox = centre;
    prevPreset.setBounds (centre.removeFromLeft (40));
    nextPreset.setBounds (centre.removeFromRight (40));

    // Bottom: hint bar and master controls
    hintBar.setBounds (area.removeFromBottom (28));
    controls.setBounds (area.removeFromBottom (110));

    // Sidebar
    auto side = area.removeFromLeft (juce::jlimit (260, 320, getWidth() / 4)).reduced (12, 12);
    auto tabs = side.removeFromTop (40);
    chainTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).reduced (2, 0));
    tone3000Tab.setBounds (tabs.reduced (2, 0));
    side.removeFromTop (10);
    browser.setBounds (side);

    // Main area with a meter on each side
    auto main = area.reduced (0, 12);
    inMeter.setBounds (main.removeFromLeft (48));
    outMeter.setBounds (main.removeFromRight (48));
    main.reduce (8, 0);
    chain.setBounds (main);
    block.setBounds (main);
    tone3000View->setBounds (main);
}

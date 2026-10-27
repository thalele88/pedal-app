#include "PluginEditor.h"

using namespace pedal::ui;

PedalAudioProcessorEditor::PedalAudioProcessorEditor (PedalAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      browser (p.getPresetSource()), stage (p, p.getPresetSource()), tuner (p)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    title.setText ("PEDAL APP", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, Colours::accent);
    addAndMakeVisible (title);

    presetLabel.setJustificationType (juce::Justification::centred);
    presetLabel.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    addAndMakeVisible (presetLabel);

    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (cpuLabel);

    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, Colours::warn);
    addAndMakeVisible (statusLabel);

    tunerButton.onClick = [this] { showTuner (true); };
    stageButton.onClick = [this] { showStage (true); };
    stageButton.setColour (juce::TextButton::buttonColourId, Colours::accent.darker (0.3f));
    addAndMakeVisible (tunerButton);
    addAndMakeVisible (stageButton);

    addAndMakeVisible (browser);

    chain.onSelect = [this] (BlockId id) { selectBlock (id); };
    chain.onTogglePower = [this] (BlockId id) { toggleBlock (id); };
    addAndMakeVisible (chain);

    stage.onExit = [this] { showStage (false); };
    stage.onTuner = [this] { showTuner (true); };
    addChildComponent (stage);

    tuner.onClose = [this] { showTuner (false); };
    addChildComponent (tuner);

    setResizable (true, true);
    setResizeLimits (820, 540, 3000, 2000);
    setSize (1100, 700);

    selectBlock (BlockId::amp);
    timerCallback();
    startTimerHz (15);
}

PedalAudioProcessorEditor::~PedalAudioProcessorEditor()
{
    showStage (false);
    setLookAndFeel (nullptr);
}

void PedalAudioProcessorEditor::showStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void PedalAudioProcessorEditor::selectBlock (BlockId id)
{
    chain.setSelected (id);
    panel = createBlockPanel (id, processor, [this] (juce::String s) { showStatus (s); });
    addAndMakeVisible (*panel);
    panel->refresh();
    resized();
}

void PedalAudioProcessorEditor::toggleBlock (BlockId id)
{
    auto& state = processor.parameters;
    switch (id)
    {
        case BlockId::gate:
        {
            const float threshold = getParameter (state, ParamIDs::gateThreshold);
            if (threshold > -99.0f)
                gateWhenOn = threshold;
            setParameter (state, ParamIDs::gateThreshold, threshold > -99.0f ? -100.0f : gateWhenOn);
            break;
        }
        case BlockId::drop:
        {
            const int drop = juce::roundToInt (getParameter (state, ParamIDs::drop));
            if (drop != 0)
                dropWhenOn = drop;
            setParameter (state, ParamIDs::drop, drop != 0 ? 0.0f : (float) dropWhenOn);
            break;
        }
        case BlockId::cab:
            setParameter (state, ParamIDs::cabEnabled, getParameter (state, ParamIDs::cabEnabled) > 0.5f ? 0.0f : 1.0f);
            break;
        case BlockId::input:
        case BlockId::amp:
        case BlockId::output:
            break;
    }
    updateChain();
}

void PedalAudioProcessorEditor::updateChain()
{
    auto& state = processor.parameters;
    auto valueText = [&state] (const char* id) { return state.getParameter (id)->getCurrentValueAsText(); };

    const float threshold = getParameter (state, ParamIDs::gateThreshold);
    const int drop = juce::roundToInt (getParameter (state, ParamIDs::drop));
    const auto amp = processor.getAmpModelName();
    const auto cab = processor.getCabinetFile();
    const float blend = getParameter (state, ParamIDs::cleanBlend);

    const auto& di = processor.getTestInput();
    chain.setBlockState (BlockId::input, { "ENTRADA", di.isPlaying() ? "DI: " + di.getFile().getFileNameWithoutExtension()
                                                                     : valueText (ParamIDs::inputGain),
                                           false, true });
    chain.setBlockState (BlockId::gate, { "GATE", valueText (ParamIDs::gateThreshold), true, threshold > -99.0f });
    chain.setBlockState (BlockId::drop, { "DROP", drop != 0 ? dropName (drop) + " st" : juce::String ("desligado"), true, drop != 0 });
    chain.setBlockState (BlockId::amp, { "AMP", amp.isNotEmpty() ? amp : juce::String ("vazio"), false, true });
    chain.setBlockState (BlockId::cab, { "CAIXA", cab == juce::File() ? juce::String ("sem IR") : cab.getFileNameWithoutExtension(),
                                         true, getParameter (state, ParamIDs::cabEnabled) > 0.5f });
    chain.setBlockState (BlockId::output, { tr ("SAÍDA"), valueText (ParamIDs::outputGain)
                                                + (blend > 0.0f ? juce::String ("\nlimpo ") + valueText (ParamIDs::cleanBlend) : juce::String()),
                                            false, true });
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

void PedalAudioProcessorEditor::timerCallback()
{
    auto& presets = processor.getPresetSource();

    // Comparing the state is not free, so check for edits a few times a second only.
    if (++tick % 4 == 0)
        presetModified = presets.isModified();

    const int current = presets.getCurrentIndex();
    const auto name = current >= 0 ? presets.getPresetInfo (current).name + (presetModified ? " *" : "")
                                   : juce::String ("Sem preset");
    presetLabel.setText (name, juce::dontSendNotification);

    if (processor.ampHasSampleRateMismatch() && statusLabel.getText().isEmpty())
        showStatus (tr ("Atenção: a captura foi feita em outra taxa de amostragem"));

    const auto cpu = processor.getCpuLoad();
    cpuLabel.setText ("CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%", juce::dontSendNotification);
    cpuLabel.setColour (juce::Label::textColourId, cpu > 0.7 ? Colours::warn : Colours::textDim);

    updateChain();
    if (panel != nullptr)
        panel->refresh();
    if (stage.isVisible())
        stage.refresh();
}

void PedalAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void PedalAudioProcessorEditor::resized()
{
    stage.setBounds (getLocalBounds());
    tuner.setBounds (getLocalBounds());

    auto area = getLocalBounds().reduced (14);

    auto header = area.removeFromTop (48);
    title.setBounds (header.removeFromLeft (150));
    stageButton.setBounds (header.removeFromRight (120).reduced (2));
    header.removeFromRight (8);
    tunerButton.setBounds (header.removeFromRight (140).reduced (2));
    cpuLabel.setBounds (header.removeFromRight (90));
    presetLabel.setBounds (header);

    statusLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);

    browser.setBounds (area.removeFromLeft (juce::jlimit (240, 340, area.getWidth() / 4)));
    area.removeFromLeft (14);

    chain.setBounds (area.removeFromTop (juce::jlimit (100, 150, area.getHeight() / 4)));
    area.removeFromTop (14);
    if (panel != nullptr)
        panel->setBounds (area);
}

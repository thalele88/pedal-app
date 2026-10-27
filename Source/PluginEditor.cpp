#include "PluginEditor.h"

using namespace pedal::ui;

PedalAudioProcessorEditor::PedalAudioProcessorEditor (PedalAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    title.setText ("PEDAL APP", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    addAndMakeVisible (title);

    modelLabel.setJustificationType (juce::Justification::centred);
    modelLabel.setFont (juce::FontOptions (18.0f));
    addAndMakeVisible (modelLabel);

    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (cpuLabel);

    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, Colours::warn);
    addAndMakeVisible (statusLabel);

    ampButton.onClick = [this] { chooseAmpModel(); };
    cabButton.onClick = [this] { chooseCabinet(); };
    addAndMakeVisible (ampButton);
    addAndMakeVisible (cabButton);

    addAndMakeVisible (cabToggle);

    diFileButton.setTooltip (juce::String::fromUTF8 ("Escolha uma gravação limpa de guitarra ou baixo para tocar sem interface"));
    diFileButton.onClick = [this] { chooseTestInput(); };
    diPlayButton.onClick = [this]
    {
        auto& di = processor.getTestInput();
        if (! di.hasClip())
            chooseTestInput();
        else
            di.setPlaying (! di.isPlaying());
    };
    addAndMakeVisible (diFileButton);
    addAndMakeVisible (diPlayButton);
    cabAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.parameters, ParamIDs::cabEnabled, cabToggle);

    setupKnob (inputKnob, ParamIDs::inputGain, "ENTRADA");
    setupKnob (gateKnob, ParamIDs::gateThreshold, "GATE");
    setupKnob (blendKnob, ParamIDs::cleanBlend, "LIMPO");
    setupKnob (outputKnob, ParamIDs::outputGain, juce::String::fromUTF8 ("SAÍDA"));

    dropLabel.setText (juce::String::fromUTF8 ("AFINAÇÃO"), juce::dontSendNotification);
    dropLabel.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (dropLabel);

    dropLatencyLabel.setJustificationType (juce::Justification::centredRight);
    dropLatencyLabel.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (dropLatencyLabel);

    fastButton.setButtonText (juce::String::fromUTF8 ("RÁPIDO"));
    smoothButton.setButtonText ("SUAVE");
    fastButton.onClick = [this] { setDropMode (pedal::DropTuner::Mode::fast); };
    smoothButton.onClick = [this] { setDropMode (pedal::DropTuner::Mode::smooth); };
    addAndMakeVisible (fastButton);
    addAndMakeVisible (smoothButton);

    for (int st : dropSteps)
    {
        auto* b = dropButtons.add (new juce::TextButton (st == 0 ? juce::String::fromUTF8 ("Padrão") : juce::String (st)));
        b->setClickingTogglesState (false);
        b->onClick = [this, st] { setDrop (st); };
        addAndMakeVisible (b);
    }

    setResizable (true, true);
    setResizeLimits (720, 480, 2400, 1600);
    setSize (960, 600);

    timerCallback();
    startTimerHz (15);
}

PedalAudioProcessorEditor::~PedalAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void PedalAudioProcessorEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name)
{
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, Colours::textDim);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 22);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, paramId, k.slider);
}

void PedalAudioProcessorEditor::setDrop (int semitones)
{
    if (auto* param = processor.parameters.getParameter (ParamIDs::drop))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) semitones));
        param->endChangeGesture();
    }
}

void PedalAudioProcessorEditor::setDropMode (pedal::DropTuner::Mode mode)
{
    if (auto* param = processor.parameters.getParameter (ParamIDs::dropMode))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) static_cast<int> (mode)));
        param->endChangeGesture();
    }
}

void PedalAudioProcessorEditor::chooseAmpModel()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Escolha uma captura NAM", juce::File(), "*.nam");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        statusLabel.setText ("Carregando " + file.getFileNameWithoutExtension() + "...", juce::dontSendNotification);
        juce::Component::SafePointer<PedalAudioProcessorEditor> safeThis (this);
        processor.loadAmpModelAsync (file, [safeThis] (juce::String error)
        {
            if (safeThis != nullptr)
                safeThis->statusLabel.setText (error, juce::dontSendNotification);
        });
    });
}

void PedalAudioProcessorEditor::chooseCabinet()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Escolha um IR de caixa", juce::File(), "*.wav;*.aif;*.aiff");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file != juce::File())
            processor.loadCabinet (file);
    });
}

void PedalAudioProcessorEditor::chooseTestInput()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Escolha uma gravação DI (guitarra ou baixo limpo)",
                                                       juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::Component::SafePointer<PedalAudioProcessorEditor> safeThis (this);
        processor.loadTestInputAsync (file, [safeThis] (juce::String error)
        {
            if (safeThis == nullptr)
                return;
            safeThis->statusLabel.setText (error, juce::dontSendNotification);
            if (error.isEmpty())
                safeThis->processor.getTestInput().setPlaying (true);
        });
    });
}

void PedalAudioProcessorEditor::timerCallback()
{
    const auto name = processor.getAmpModelName();
    modelLabel.setText (name.isEmpty() ? "Nenhuma captura carregada" : name, juce::dontSendNotification);

    if (processor.ampHasSampleRateMismatch() && statusLabel.getText().isEmpty())
        statusLabel.setText (juce::String::fromUTF8 ("Atenção: a captura foi feita em outra taxa de amostragem"), juce::dontSendNotification);

    const auto cpu = processor.getCpuLoad();
    cpuLabel.setText ("CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%", juce::dontSendNotification);
    cpuLabel.setColour (juce::Label::textColourId, cpu > 0.7 ? Colours::warn : Colours::textDim);

    const auto cab = processor.getCabinetFile();
    cabButton.setButtonText (cab == juce::File() ? "CAIXA" : "CAIXA\n" + cab.getFileNameWithoutExtension());

    const auto& di = processor.getTestInput();
    diPlayButton.setButtonText (di.isPlaying() ? juce::String ("Parar DI") : juce::String ("Tocar DI"));
    diPlayButton.setToggleState (di.isPlaying(), juce::dontSendNotification);
    if (di.hasClip())
        diFileButton.setButtonText (di.getFile().getFileNameWithoutExtension());

    const int drop = (int) processor.parameters.getRawParameterValue (ParamIDs::drop)->load();
    for (int i = 0; i < dropButtons.size(); ++i)
        dropButtons[i]->setToggleState (dropSteps[i] == drop, juce::dontSendNotification);

    const auto mode = juce::roundToInt (processor.parameters.getRawParameterValue (ParamIDs::dropMode)->load());
    fastButton.setToggleState (mode == static_cast<int> (pedal::DropTuner::Mode::fast), juce::dontSendNotification);
    smoothButton.setToggleState (mode == static_cast<int> (pedal::DropTuner::Mode::smooth), juce::dontSendNotification);

    // The fast mode's delay depends on what is played, so show its range.
    juce::String latencyText;
    if (drop != 0)
        latencyText = mode == static_cast<int> (pedal::DropTuner::Mode::fast)
                        ? juce::String::fromUTF8 ("atraso ") + juce::String (juce::roundToInt (pedal::DropTuner::fastLatencyMinMs))
                              + " a " + juce::String (juce::roundToInt (pedal::DropTuner::fastLatencyMaxMs)) + " ms"
                        : juce::String::fromUTF8 ("atraso ") + juce::String (juce::roundToInt (processor.getDropLatencyMs())) + " ms";
    dropLatencyLabel.setText (latencyText, juce::dontSendNotification);
}

void PedalAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Chain strip: Gate -> Afinação -> Amp -> Caixa -> Saída
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (chainArea.toFloat(), 10.0f);

    const juce::StringArray blocks { "GATE", juce::String::fromUTF8 ("AFINAÇÃO"), "AMP", "CAIXA", juce::String::fromUTF8 ("SAÍDA") };
    auto row = chainArea.reduced (16);
    const int w = row.getWidth() / blocks.size();
    for (int i = 0; i < blocks.size(); ++i)
    {
        auto cell = row.removeFromLeft (w).reduced (10, 0);

        if (i < blocks.size() - 1)
        {
            g.setColour (Colours::textDim.withAlpha (0.5f));
            g.drawArrow ({ (float) cell.getRight(), (float) cell.getCentreY(),
                           (float) cell.getRight() + 20.0f, (float) cell.getCentreY() }, 2.0f, 8.0f, 8.0f);
        }

        if (blocks[i] == "AMP" || blocks[i] == "CAIXA")
            continue; // these are real buttons laid out in resized()

        g.setColour (Colours::panelRaised);
        g.fillRoundedRectangle (cell.toFloat(), 8.0f);
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (blocks[i], cell, juce::Justification::centred);
    }
}

void PedalAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (40);
    title.setBounds (header.removeFromLeft (160));
    cpuLabel.setBounds (header.removeFromRight (90));
    diPlayButton.setBounds (header.removeFromRight (110).reduced (4, 4));
    diFileButton.setBounds (header.removeFromRight (150).reduced (4, 4));
    modelLabel.setBounds (header);

    statusLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    chainArea = area.removeFromTop (juce::jmax (90, area.getHeight() / 5));
    {
        auto row = chainArea.reduced (16);
        const int w = row.getWidth() / 5;
        row.removeFromLeft (w * 2);
        ampButton.setBounds (row.removeFromLeft (w).reduced (10, 0));
        cabButton.setBounds (row.removeFromLeft (w).reduced (10, 0));
    }
    area.removeFromTop (8);
    cabToggle.setBounds (area.removeFromTop (28).withSizeKeepingCentre (160, 28));

    auto dropArea = area.removeFromBottom (juce::jmax (80, area.getHeight() / 4));
    {
        auto top = dropArea.removeFromTop (28);
        smoothButton.setBounds (top.removeFromRight (90).reduced (4, 2));
        fastButton.setBounds (top.removeFromRight (90).reduced (4, 2));
        dropLatencyLabel.setBounds (top.removeFromRight (120));
        dropLabel.setBounds (top);
    }
    {
        const int w = dropArea.getWidth() / dropButtons.size();
        for (auto* b : dropButtons)
            b->setBounds (dropArea.removeFromLeft (w).reduced (4));
    }

    area.removeFromBottom (8);
    Knob* knobs[] = { &inputKnob, &gateKnob, &blendKnob, &outputKnob };
    const int w = area.getWidth() / 4;
    for (auto* k : knobs)
    {
        auto cell = area.removeFromLeft (w).reduced (8);
        k->label.setBounds (cell.removeFromTop (22));
        k->slider.setBounds (cell);
    }
}

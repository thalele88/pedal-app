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

    attributionLabel.setJustificationType (juce::Justification::centred);
    attributionLabel.setFont (juce::FontOptions (13.0f));
    attributionLabel.setColour (juce::Label::textColourId, Colours::textDim);
    addAndMakeVisible (attributionLabel);

    tone3000Button.setTooltip ("Escolher uma captura no TONE3000");
    tone3000Button.onClick = [this] { chooseTone3000(); };
    addAndMakeVisible (tone3000Button);

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

void PedalAudioProcessorEditor::chooseTone3000()
{
    auto& t3k = processor.getTone3000();

    if (t3k.isSelecting())
    {
        t3k.cancelSelect();
        return;
    }

    if (! t3k.hasClientId())
    {
        statusLabel.setText (juce::String::fromUTF8 ("Falta o client_id do TONE3000 nesta versão. Veja a seção TONE3000 do README."),
                             juce::dontSendNotification);
        return;
    }

    if (t3k.isConnected())
    {
        openTone3000Catalog();
        return;
    }

    // Before the first sign-in TONE3000 asks for a short "partnership" screen.
    juce::Component::SafePointer<PedalAudioProcessorEditor> safeThis (this);
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::NoIcon,
        "TONE3000",
        juce::String::fromUTF8 ("O Pedal App se conecta ao TONE3000 para dar acesso a uma enorme biblioteca de capturas "
                                "Neural Amp Modeler e IRs de equipamentos reais, feitas por uma comunidade de músicos "
                                "do mundo todo.\n\nVocê entra com a sua conta do TONE3000 (grátis) no navegador e "
                                "escolhe o tom lá; ele volta direto para o app."),
        "Entrar no TONE3000",
        juce::String::fromUTF8 ("Agora não"),
        this,
        juce::ModalCallbackFunction::create ([safeThis] (int result)
        {
            if (safeThis != nullptr && result == 1)
                safeThis->openTone3000Catalog();
        }));
}

void PedalAudioProcessorEditor::openTone3000Catalog()
{
    statusLabel.setText (juce::String::fromUTF8 ("Escolha um tom na página do TONE3000 que abriu no navegador..."),
                         juce::dontSendNotification);

    juce::Component::SafePointer<PedalAudioProcessorEditor> safeThis (this);
    processor.getTone3000().selectTone ([safeThis] (pedal::tone3000::SelectResult result)
    {
        if (safeThis != nullptr)
            safeThis->tone3000Selected (result);
    });
}

void PedalAudioProcessorEditor::tone3000Selected (const pedal::tone3000::SelectResult& result)
{
    using Status = pedal::tone3000::SelectResult::Status;

    if (result.status == Status::canceled)
    {
        statusLabel.setText ({}, juce::dontSendNotification);
        return;
    }
    if (result.status == Status::failed)
    {
        statusLabel.setText (result.error, juce::dontSendNotification);
        return;
    }

    const auto& tone = result.tone;
    if (tone.models.size() == 1)
    {
        loadTone3000Model (tone, tone.models.getReference (0));
        return;
    }

    // A tone usually has one capture per amp setting/channel; let the user pick.
    statusLabel.setText (tone.title + juce::String::fromUTF8 (": escolha a captura"), juce::dontSendNotification);

    juce::PopupMenu menu;
    menu.addSectionHeader (tone.title);
    const int suggested = pedal::tone3000::Service::defaultModelIndex (tone);
    for (int i = 0; i < tone.models.size(); ++i)
    {
        const auto& m = tone.models.getReference (i);
        juce::StringArray details;
        if (m.size.isNotEmpty())
            details.add (m.size);
        if (m.architecture.isNotEmpty())
            details.add (m.architecture == "custom" ? juce::String ("custom") : "A" + m.architecture);
        menu.addItem (i + 1, m.name + (details.isEmpty() ? juce::String() : "  (" + details.joinIntoString (", ") + ")"),
                      true, i == suggested);
    }

    juce::Component::SafePointer<PedalAudioProcessorEditor> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tone3000Button),
                        [safeThis, tone] (int chosen)
    {
        if (safeThis == nullptr)
            return;
        if (chosen <= 0)
            safeThis->statusLabel.setText ({}, juce::dontSendNotification);
        else
            safeThis->loadTone3000Model (tone, tone.models.getReference (chosen - 1));
    });
}

void PedalAudioProcessorEditor::loadTone3000Model (const pedal::tone3000::Tone& tone, const pedal::tone3000::Model& model)
{
    PedalAudioProcessor::Tone3000Model ref;
    ref.toneId = tone.id;
    ref.modelId = model.id;
    ref.name = model.name.isEmpty() || model.name == tone.title ? tone.title : tone.title + " - " + model.name;
    ref.creator = tone.creator;

    statusLabel.setText ("Baixando " + ref.name + "...", juce::dontSendNotification);
    juce::Component::SafePointer<PedalAudioProcessorEditor> safeThis (this);
    processor.loadTone3000ModelAsync (ref, [safeThis] (juce::String error)
    {
        if (safeThis != nullptr)
            safeThis->statusLabel.setText (error, juce::dontSendNotification);
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

    const auto t3kModel = processor.getTone3000Model();
    attributionLabel.setText (! t3kModel.isEmpty() && name.isNotEmpty()
                                  ? pedal::tone3000::Service::attribution (t3kModel.creator)
                                  : juce::String(),
                              juce::dontSendNotification);
    tone3000Button.setButtonText (processor.getTone3000().isSelecting() ? "CANCELAR" : "TONE3000");

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
    {
        // The amp's source row: TONE3000 button and the credit for the loaded tone.
        auto row = area.removeFromTop (30);
        tone3000Button.setBounds (row.removeFromRight (120).reduced (4, 2));
        row.removeFromLeft (120); // keeps the credit centred under the model name
        attributionLabel.setBounds (row);
    }

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
    dropLabel.setBounds (dropArea.removeFromTop (22));
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

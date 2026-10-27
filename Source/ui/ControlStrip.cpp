#include "ui/ControlStrip.h"

namespace pedal::ui
{

namespace
{
// Popup with the Pitch options. Only the options the processor has are shown,
// so it works before and after the drop tuner gets STEP / Tonality / Buffer.
class PitchOptions : public juce::Component,
                     private juce::Timer
{
public:
    PitchOptions (PedalAudioProcessor& p, PitchParams& params) : processor (p), pitch (params)
    {
        auto& state = processor.parameters;

        if (pitch.step != nullptr)
        {
            step.setButtonText ("STEP");
            step.setClickingTogglesState (true);
            stepAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, pitch.step->getParameterID(), step);
            step.setTooltip (tr ("Ligado: pula de semitom em semitom. Desligado: o knob varre a afinação suave, como um pedal whammy."));
            addAndMakeVisible (step);
        }

        if (pitch.tonality != nullptr)
        {
            tonality = std::make_unique<StripKnob> (state, pitch.tonality->getParameterID(), "Tonality");
            tonality->slider.setTooltip (tr ("Agudos acima desta frequência passam sem mudar de afinação, para a palhetada continuar brilhante."));
            addAndMakeVisible (*tonality);
        }
        if (pitch.tonalityOn != nullptr)
        {
            tonalityOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, pitch.tonalityOn->getParameterID(), tonalityOn);
            addAndMakeVisible (tonalityOn);
        }

        if (pitch.buffer != nullptr)
        {
            buffer.addItemList (pitch.buffer->getAllValueStrings(), 1);
            bufferAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, pitch.buffer->getParameterID(), buffer);
            buffer.setTooltip (tr ("Buffer: menor = menos atraso; maior = som mais limpo. Para baixo, use 30 ms ou mais."));
            addAndMakeVisible (buffer);
        }

        for (int st : { -1, -2, -3, -4, -5, -7, -12 })
        {
            auto* b = shortcuts.add (new juce::TextButton (juce::String (st)));
            b->onClick = [this, st] { pitch.selectShortcut (st); };
            addAndMakeVisible (b);
        }

        setSize (440, pitch.step != nullptr || pitch.buffer != nullptr || pitch.tonality != nullptr ? 230 : 140);
        timerCallback();
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText ("PITCH (DROP)", getLocalBounds().reduced (14, 10).removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (tr ("Atalhos de afinação:"), shortcutsLabel, juce::Justification::centredLeft);
        g.drawFittedText (latencyText, getLocalBounds().reduced (14, 8).removeFromBottom (20), juce::Justification::centredLeft, 1);
        if (buffer.isVisible())
            g.drawText ("Buffer", buffer.getBounds().translated (0, -20).withHeight (18), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 10);
        area.removeFromTop (28);
        area.removeFromBottom (22);

        shortcutsLabel = area.removeFromTop (18);
        auto row = area.removeFromTop (40);
        const int w = row.getWidth() / shortcuts.size();
        for (auto* b : shortcuts)
            b->setBounds (row.removeFromLeft (w).reduced (2));
        area.removeFromTop (10);

        if (area.getHeight() < 60)
            return;
        auto options = area;
        if (step.isVisible())
            step.setBounds (options.removeFromLeft (80).withSizeKeepingCentre (70, 30));
        options.removeFromLeft (10);
        if (tonality != nullptr)
        {
            auto t = options.removeFromLeft (100);
            if (tonalityOn.isVisible())
                tonalityOn.setBounds (t.removeFromRight (26).withSizeKeepingCentre (26, 26));
            tonality->setBounds (t);
        }
        options.removeFromLeft (14);
        if (buffer.isVisible())
            buffer.setBounds (options.withSizeKeepingCentre (options.getWidth(), 30));
    }

private:
    void timerCallback() override
    {
        const double sr = processor.getSampleRate();
        juce::String text;
        if (! pitch.isOn())
            text = tr ("Pitch desligado: o som passa direto, sem atraso.");
        else if (sr > 0.0)
            text = tr ("Atraso com o pitch ligado: ") + juce::String (1000.0 * processor.getLatencySamples() / sr, 0) + " ms";

        // Tonality only works with the buffered engines (not "Rápido").
        if (tonality != nullptr && pitch.buffer != nullptr)
        {
            const bool usable = pitch.buffer->getValue() > 0.0f; // index 0 = "Rápido"
            tonality->setEnabled (usable);
            tonality->setDimmed (! usable);
            tonalityOn.setEnabled (usable);
        }

        if (text != latencyText)
        {
            latencyText = text;
            repaint();
        }
    }

    PedalAudioProcessor& processor;
    PitchParams& pitch;
    juce::TextButton step;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stepAttachment, tonalityOnAttachment;
    std::unique_ptr<StripKnob> tonality;
    PowerButton tonalityOn;
    juce::ComboBox buffer;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bufferAttachment;
    juce::OwnedArray<juce::TextButton> shortcuts;
    juce::Rectangle<int> shortcutsLabel;
    juce::String latencyText;
};
} // namespace

// Opens the Pitch options on right-click, like the TONE3000 plugin.
struct ControlStrip::RightClickOpener : public juce::MouseListener
{
    explicit RightClickOpener (ControlStrip& s) : strip (s) {}
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            strip.showPitchOptions();
    }
    ControlStrip& strip;
};

//==============================================================================
ControlStrip::ControlStrip (PedalAudioProcessor& p, PitchParams& pitchParams, std::function<void (juce::String)> status)
    : processor (p), pitch (pitchParams), showStatus (std::move (status)),
      input (p.parameters, ParamIDs::inputGain, "Input"),
      gate (p.parameters, ParamIDs::gateThreshold, "Gate"),
      pitchKnob (p.parameters, pitchParams.amount != nullptr ? pitchParams.amount->getParameterID() : juce::String (ParamIDs::drop), "Pitch"),
      bass (p.parameters, ParamIDs::bass, "Bass"),
      middle (p.parameters, ParamIDs::middle, "Middle"),
      treble (p.parameters, ParamIDs::treble, "Treble"),
      output (p.parameters, ParamIDs::outputGain, "Output")
{
    for (auto* k : { &input, &gate, &pitchKnob, &bass, &middle, &treble, &output })
        addAndMakeVisible (*k);
    for (auto* k : { &input, &pitchKnob, &bass, &middle, &treble })
        k->slider.getProperties().set ("bipolar", true);

    gateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, ParamIDs::gateEnabled, gatePower);
    eqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters, ParamIDs::eqEnabled, eqPower);
    pitchPower.setClickingTogglesState (false);
    pitchPower.onClick = [this] { pitch.setOn (! pitch.isOn()); refresh(); };
    for (auto* b : { &gatePower, &eqPower, &pitchPower })
        addAndMakeVisible (*b);

    pitchMore.onClick = [this] { showPitchOptions(); };
    addAndMakeVisible (pitchMore);
    rightClick = std::make_unique<RightClickOpener> (*this);
    pitchKnob.addMouseListener (rightClick.get(), true);

    diButton.onClick = [this] { showDiMenu(); };
    addAndMakeVisible (diButton);

    setHint (input, tr ("Input: volume que entra no amp. Toque forte e suba até o medidor da esquerda ficar no amarelo, sem chegar no vermelho."));
    setHint (diButton, tr ("DI: toca uma gravação limpa de guitarra ou baixo no lugar da entrada, para testar sons sem interface de áudio."));
    setHint (gate, tr ("Gate: corta o chiado quando você não está tocando. Gire para a direita até o ruído sumir nas pausas."));
    setHint (gatePower, tr ("Liga e desliga o gate sem perder o ajuste."));
    setHint (pitchKnob, tr ("Pitch: muda a afinação sem mexer nas tarraxas (ex.: -2 = um tom abaixo). Clique com o botão direito para mais opções."));
    setHint (pitchPower, tr ("Liga e desliga o pitch. Desligado, não há atraso nenhum."));
    setHint (pitchMore, tr ("Opções do pitch: atalhos de afinação e qualidade x atraso."));
    setHint (bass, tr ("Graves: corpo e peso do som. 5 = sem mudança."));
    setHint (middle, tr ("Médios: presença na banda. Mais médio aparece mais na mixagem. 5 = sem mudança."));
    setHint (treble, tr ("Agudos: brilho e ataque. 5 = sem mudança."));
    setHint (eqPower, tr ("Liga e desliga o EQ sem perder o ajuste."));
    setHint (output, tr ("Output: volume final. Use para igualar o volume entre presets."));
}

ControlStrip::~ControlStrip()
{
    pitchKnob.removeMouseListener (rightClick.get());
}

std::unique_ptr<juce::Component> ControlStrip::createPitchOptions()
{
    return std::make_unique<PitchOptions> (processor, pitch);
}

void ControlStrip::showPitchOptions()
{
    juce::CallOutBox::launchAsynchronously (createPitchOptions(), pitchKnob.getScreenBounds(), nullptr);
}

void ControlStrip::showDiMenu()
{
    auto& di = processor.getTestInput();
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Gravação DI (teste sem interface)"));
    menu.addItem (1, tr ("Escolher arquivo..."));
    menu.addItem (2, di.isPlaying() ? juce::String ("Parar") : juce::String ("Tocar") + (di.hasClip() ? " " + di.getFile().getFileNameWithoutExtension() : juce::String()),
                  di.hasClip());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (diButton), [this] (int result)
    {
        auto& player = processor.getTestInput();
        if (result == 1)
            chooseDiFile();
        else if (result == 2)
            player.setPlaying (! player.isPlaying());
    });
}

void ControlStrip::chooseDiFile()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Escolha uma gravação DI (guitarra ou baixo limpo)"),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::Component::SafePointer<ControlStrip> safeThis (this);
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

void ControlStrip::refresh()
{
    const bool gateOn = gatePower.getToggleState();
    const bool eqOn = eqPower.getToggleState();
    const bool pitchOn = pitch.isOn();
    gate.setDimmed (! gateOn);
    for (auto* k : { &bass, &middle, &treble })
        k->setDimmed (! eqOn);
    pitchPower.setToggleState (pitchOn, juce::dontSendNotification);
    pitchKnob.setDimmed (! pitchOn);

    const bool diPlaying = processor.getTestInput().isPlaying();
    diButton.setToggleState (diPlaying, juce::dontSendNotification);
}

void ControlStrip::paint (juce::Graphics& g)
{
    g.setColour (Colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void ControlStrip::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    const int knob = juce::jlimit (56, 80, area.getHeight());

    auto place = [&] (StripKnob& k) { k.setBounds (area.removeFromLeft (knob)); };
    auto small = [&] (juce::Component& c, int w) { c.setBounds (area.removeFromLeft (w).withSizeKeepingCentre (w, 28).translated (0, -10)); };

    output.setBounds (area.removeFromRight (knob));

    place (input);
    small (diButton, 40);
    area.removeFromLeft (juce::jmax (8, area.getWidth() / 30));
    place (gate);
    small (gatePower, 28);
    area.removeFromLeft (8);
    place (pitchKnob);
    small (pitchPower, 28);
    small (pitchMore, 28);
    area.removeFromLeft (juce::jmax (8, area.getWidth() / 12));
    place (bass);
    place (middle);
    place (treble);
    small (eqPower, 28);
}

} // namespace pedal::ui

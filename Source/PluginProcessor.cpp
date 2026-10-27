#include "PluginProcessor.h"
#include "PluginEditor.h"

PedalAudioProcessor::PedalAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PedalState", createParameterLayout())
{
    inputGain = parameters.getRawParameterValue (ParamIDs::inputGain);
    gateThreshold = parameters.getRawParameterValue (ParamIDs::gateThreshold);
    drop = parameters.getRawParameterValue (ParamIDs::drop);
    dropMode = parameters.getRawParameterValue (ParamIDs::dropMode);
    cleanBlend = parameters.getRawParameterValue (ParamIDs::cleanBlend);
    outputGain = parameters.getRawParameterValue (ParamIDs::outputGain);
    cabEnabled = parameters.getRawParameterValue (ParamIDs::cabEnabled);

    // Frees replaced models off the audio thread and keeps latency in sync.
    startTimerHz (10);
}

PedalAudioProcessor::~PedalAudioProcessor()
{
    *alive = false;
    stopTimer();
    loaderPool.removeAllJobs (true, 5000);
}

juce::AudioProcessorValueTreeState::ParameterLayout PedalAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto db = [] (float v, int) { return String (v, 1) + " dB"; };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::inputGain, 1 }, "Entrada", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (db)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::gateThreshold, 1 }, "Gate", NormalisableRange<float> (-100.0f, 0.0f, 0.5f), -100.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            return v <= -99.0f ? String ("Desligado") : String (v, 1) + " dB";
        })));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { ParamIDs::drop, 1 }, juce::String::fromUTF8 ("Afinação"), -12, 12, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) {
            return v == 0 ? juce::String::fromUTF8 ("Padrão") : (v > 0 ? "+" : "") + String (v) + " st";
        })));

    // Index order must match pedal::DropTuner::Mode.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::dropMode, 1 }, "Modo do drop",
        StringArray { juce::String::fromUTF8 ("Rápido"), "Suave" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::cleanBlend, 1 }, "Limpo", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            return String (juce::roundToInt (v * 100.0f)) + "%";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::outputGain, 1 }, juce::String::fromUTF8 ("Saída"), NormalisableRange<float> (-40.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (db)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::cabEnabled, 1 }, "Caixa", true));

    return layout;
}

bool PedalAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

void PedalAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    chain.prepare (sampleRate, samplesPerBlock);
    loadMeasurer.reset (sampleRate, samplesPerBlock);
    updateLatency();
}

void PedalAudioProcessor::releaseResources()
{
    chain.reset();
}

void PedalAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());

    pedal::ChainSettings settings;
    settings.inputGainDb = inputGain->load();
    settings.gateThresholdDb = gateThreshold->load();
    settings.dropSemitones = juce::roundToInt (drop->load());
    settings.dropMode = static_cast<pedal::DropTuner::Mode> (juce::roundToInt (dropMode->load()));
    settings.cleanBlend = cleanBlend->load();
    settings.outputGainDb = outputGain->load();
    settings.cabEnabled = cabEnabled->load() > 0.5f;

    chain.process (buffer, settings);
}

void PedalAudioProcessor::loadAmpModelAsync (const juce::File& file, std::function<void (juce::String)> onDone)
{
    loaderPool.addJob ([this, file, onDone, aliveFlag = alive]
    {
        auto error = chain.getAmp().load (file);
        juce::MessageManager::callAsync ([this, file, error, onDone, aliveFlag]
        {
            if (! aliveFlag->load())
                return;
            if (error.isEmpty())
                ampModelFile = file;
            if (onDone)
                onDone (error);
        });
    });
}

void PedalAudioProcessor::loadCabinet (const juce::File& file)
{
    cabinetFile = file;
    chain.loadCabinet (file);
}

void PedalAudioProcessor::timerCallback()
{
    chain.getAmp().collectGarbage();
    updateLatency();
}

int PedalAudioProcessor::currentDropLatencySamples() const
{
    const auto mode = static_cast<pedal::DropTuner::Mode> (juce::roundToInt (dropMode->load()));
    return chain.getDropTuner().getLatencySamples (mode, juce::roundToInt (drop->load()));
}

double PedalAudioProcessor::getDropLatencyMs() const
{
    return 1000.0 * currentDropLatencySamples() / currentSampleRate;
}

void PedalAudioProcessor::updateLatency()
{
    const int latency = currentDropLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

void PedalAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("ampModel", ampModelFile.getFullPathName(), nullptr);
    state.setProperty ("cabinet", cabinetFile.getFullPathName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PedalAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    parameters.replaceState (state);

    const juce::File model (state.getProperty ("ampModel").toString());
    if (model.existsAsFile())
        loadAmpModelAsync (model);

    const juce::File cab (state.getProperty ("cabinet").toString());
    if (cab.existsAsFile())
        loadCabinet (cab);
}

juce::AudioProcessorEditor* PedalAudioProcessor::createEditor()
{
    return new PedalAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PedalAudioProcessor();
}

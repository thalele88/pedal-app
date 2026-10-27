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
    pitchOn = parameters.getRawParameterValue (ParamIDs::pitchOn);
    pitch = parameters.getRawParameterValue (ParamIDs::pitch);
    pitchStep = parameters.getRawParameterValue (ParamIDs::pitchStep);
    pitchTonalityOn = parameters.getRawParameterValue (ParamIDs::pitchTonalityOn);
    pitchTonality = parameters.getRawParameterValue (ParamIDs::pitchTonality);
    pitchBuffer = parameters.getRawParameterValue (ParamIDs::pitchBuffer);
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

    // Pitch (drop tuning), modelled on the TONE3000 plugin's Pitch block.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::pitchOn, 1 }, "Pitch ligado", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::pitch, 1 }, "Pitch", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), -2.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            return (v > 0.0f ? "+" : "") + String (v, 1) + " st";
        })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::pitchStep, 1 }, "Step", true));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::pitchTonalityOn, 1 }, "Tonality ligada", false));

    NormalisableRange<float> tonalityRange (1000.0f, 20000.0f, 1.0f);
    tonalityRange.setSkewForCentre (5000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::pitchTonality, 1 }, "Tonality", tonalityRange, 8000.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            return String (v / 1000.0f, 1) + " kHz";
        })));

    // Index order must match pedal::DropTuner::Buffer.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::pitchBuffer, 1 }, "Buffer",
        StringArray { juce::String::fromUTF8 ("Rápido"), "20 ms", "30 ms", "40 ms", "60 ms" }, 0));

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
    testInput.prepare (sampleRate);
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
    settings.pitchOn = pitchOn->load() > 0.5f;
    settings.pitchSemitones = pitchStep->load() > 0.5f ? std::round (pitch->load()) : pitch->load();
    settings.pitchBuffer = static_cast<pedal::DropTuner::Buffer> (juce::roundToInt (pitchBuffer->load()));
    settings.pitchTonalityHz = pitchTonalityOn->load() > 0.5f ? pitchTonality->load() : 0.0f;
    settings.cleanBlend = cleanBlend->load();
    settings.outputGainDb = outputGain->load();
    settings.cabEnabled = cabEnabled->load() > 0.5f;

    testInput.process (buffer);
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

void PedalAudioProcessor::loadTestInputAsync (const juce::File& file, std::function<void (juce::String)> onDone)
{
    loaderPool.addJob ([this, file, onDone, aliveFlag = alive]
    {
        auto error = testInput.load (file);
        juce::MessageManager::callAsync ([error, onDone, aliveFlag]
        {
            if (aliveFlag->load() && onDone)
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
    testInput.collectGarbage();
    updateLatency();
}

int PedalAudioProcessor::currentPitchLatencySamples() const
{
    if (pitchOn->load() < 0.5f)
        return 0;
    const auto buffer = static_cast<pedal::DropTuner::Buffer> (juce::roundToInt (pitchBuffer->load()));
    return chain.getDropTuner().getLatencySamples (buffer);
}

double PedalAudioProcessor::getPitchLatencyMs() const
{
    return 1000.0 * currentPitchLatencySamples() / currentSampleRate;
}

void PedalAudioProcessor::updateLatency()
{
    const int latency = currentPitchLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

void PedalAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("ampModel", ampModelFile.getFullPathName(), nullptr);
    state.setProperty ("cabinet", cabinetFile.getFullPathName(), nullptr);
    state.setProperty ("testInput", testInput.getFile().getFullPathName(), nullptr);
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

    // Remembers the DI file but never starts playing it on its own.
    const juce::File di (state.getProperty ("testInput").toString());
    if (di.existsAsFile())
        loadTestInputAsync (di);
}

juce::AudioProcessorEditor* PedalAudioProcessor::createEditor()
{
    return new PedalAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PedalAudioProcessor();
}

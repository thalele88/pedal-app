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
    cleanBlend = parameters.getRawParameterValue (ParamIDs::cleanBlend);
    outputGain = parameters.getRawParameterValue (ParamIDs::outputGain);
    cabEnabled = parameters.getRawParameterValue (ParamIDs::cabEnabled);
    gateEnabled = parameters.getRawParameterValue (ParamIDs::gateEnabled);
    bass = parameters.getRawParameterValue (ParamIDs::bass);
    middle = parameters.getRawParameterValue (ParamIDs::middle);
    treble = parameters.getRawParameterValue (ParamIDs::treble);
    eqEnabled = parameters.getRawParameterValue (ParamIDs::eqEnabled);

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
        ParameterID { ParamIDs::gateThreshold, 1 }, "Gate", NormalisableRange<float> (-100.0f, 0.0f, 0.5f), -60.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::gateEnabled, 1 }, "Gate ligado", false));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { ParamIDs::drop, 1 }, juce::String::fromUTF8 ("Afinação"), -12, 12, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) {
            return v == 0 ? juce::String::fromUTF8 ("Padrão") : (v > 0 ? "+" : "") + String (v) + " st";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::cleanBlend, 1 }, "Limpo", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            return String (juce::roundToInt (v * 100.0f)) + "%";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::outputGain, 1 }, juce::String::fromUTF8 ("Saída"), NormalisableRange<float> (-40.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (db)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::cabEnabled, 1 }, "Caixa", true));

    // Tone stack on a 0..10 scale like an amp; 5 is flat, each step is 2.4 dB.
    auto tone = [] (float v, int) { return String (v, 1); };
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::bass, 1 }, "Graves", NormalisableRange<float> (0.0f, 10.0f, 0.1f), 5.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (tone)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::middle, 1 }, juce::String::fromUTF8 ("Médios"), NormalisableRange<float> (0.0f, 10.0f, 0.1f), 5.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (tone)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::treble, 1 }, "Agudos", NormalisableRange<float> (0.0f, 10.0f, 0.1f), 5.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (tone)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::eqEnabled, 1 }, "EQ ligado", true));

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
    chain.prepare (sampleRate, samplesPerBlock);
    testInput.prepare (sampleRate);
    loadMeasurer.reset (sampleRate, samplesPerBlock);
    tuner.prepare (sampleRate);
    muteGain.reset (sampleRate, 0.02);
    muteGain.setCurrentAndTargetValue (outputMuted.load() ? 0.0f : 1.0f);
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
    settings.gateThresholdDb = gateEnabled->load() > 0.5f ? gateThreshold->load() : -100.0f;
    settings.dropSemitones = juce::roundToInt (drop->load());
    settings.cleanBlend = cleanBlend->load();
    settings.outputGainDb = outputGain->load();
    settings.cabEnabled = cabEnabled->load() > 0.5f;
    settings.eqEnabled = eqEnabled->load() > 0.5f;
    settings.bassDb = (bass->load() - 5.0f) * 2.4f;
    settings.middleDb = (middle->load() - 5.0f) * 2.4f;
    settings.trebleDb = (treble->load() - 5.0f) * 2.4f;

    testInput.process (buffer);
    tuner.push (buffer.getReadPointer (0), buffer.getNumSamples());
    auto storePeak = [] (std::atomic<float>& peak, float value)
    {
        if (value > peak.load (std::memory_order_relaxed))
            peak.store (value, std::memory_order_relaxed);
    };
    storePeak (inputPeak, buffer.getMagnitude (0, 0, buffer.getNumSamples()));
    chain.process (buffer, settings);

    const bool muted = outputMuted.load();
    muteGain.setTargetValue (muted ? 0.0f : 1.0f);
    if (muteGain.isSmoothing())
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float gain = muteGain.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer (ch)[i] *= gain;
        }
    }
    else if (muted)
    {
        buffer.clear();
    }

    storePeak (outputPeak, buffer.getMagnitude (0, buffer.getNumSamples()));
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

void PedalAudioProcessor::updateLatency()
{
    const int latency = juce::roundToInt (drop->load()) != 0 ? chain.getDropTuner().getLatencySamples() : 0;
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

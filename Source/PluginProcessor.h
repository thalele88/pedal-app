#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/SignalChain.h"
#include "dsp/TestInputPlayer.h"
#include "presets/PresetManager.h"

namespace ParamIDs
{
    inline constexpr auto inputGain = "inputGain";
    inline constexpr auto gateThreshold = "gateThreshold";
    inline constexpr auto drop = "drop";
    inline constexpr auto cleanBlend = "cleanBlend";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto cabEnabled = "cabEnabled";
}

class PedalAudioProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    PedalAudioProcessor();
    ~PedalAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Loads happen on a background thread; `onDone` is called on the message
    // thread with an empty string on success or an error message.
    void loadAmpModelAsync (const juce::File& file, std::function<void (juce::String)> onDone = {});
    void loadCabinet (const juce::File& file);
    // Test input: a recorded DI track played through the chain instead of the
    // live input, for playing around without an audio interface.
    void loadTestInputAsync (const juce::File& file, std::function<void (juce::String)> onDone = {});
    pedal::TestInputPlayer& getTestInput() { return testInput; }

    juce::String getAmpModelName() const { return chain.getAmp().getModelName(); }
    bool ampHasSampleRateMismatch() const { return chain.getAmp().hasSampleRateMismatch(); }
    juce::File getCabinetFile() const { return presets.getCurrentIr(); }

    pedal::PresetManager& getPresets() noexcept { return presets; }

    // 0..1 share of the audio callback time used by this plugin.
    double getCpuLoad() const { return loadMeasurer.getLoadAsProportion(); }

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;
    void updateLatency();

    pedal::SignalChain chain;
    pedal::PresetManager presets { parameters, chain };
    pedal::TestInputPlayer testInput;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    juce::ThreadPool loaderPool { 1 };

    // Lets callbacks posted by the loader know the processor is gone.
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    std::atomic<float>* inputGain = nullptr;
    std::atomic<float>* gateThreshold = nullptr;
    std::atomic<float>* drop = nullptr;
    std::atomic<float>* cleanBlend = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* cabEnabled = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalAudioProcessor)
};

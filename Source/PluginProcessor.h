#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/SignalChain.h"
#include "dsp/TestInputPlayer.h"
#include "dsp/Tuner.h"
#include "ui/SessionPresets.h"

namespace ParamIDs
{
    inline constexpr auto inputGain = "inputGain";
    inline constexpr auto gateThreshold = "gateThreshold";
    inline constexpr auto drop = "drop";
    inline constexpr auto cleanBlend = "cleanBlend";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto cabEnabled = "cabEnabled";
    inline constexpr auto gateEnabled = "gateEnabled";
    inline constexpr auto bass = "bass";      // 0..10, 5 = flat
    inline constexpr auto middle = "middle";
    inline constexpr auto treble = "treble";
    inline constexpr auto eqEnabled = "eqEnabled";
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
    juce::File getAmpModelFile() const { return ampModelFile; }
    juce::File getCabinetFile() const { return cabinetFile; }

    // Peak levels since the last call (0..1+), for the meters. Message thread only.
    float takeInputPeak() noexcept { return inputPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept { return outputPeak.exchange (0.0f); }

    // 0..1 share of the audio callback time used by this plugin.
    double getCpuLoad() const { return loadMeasurer.getLoadAsProportion(); }

    // Input signal for the tuner screen (filled only while the tuner is open).
    pedal::TunerInput& getTuner() noexcept { return tuner; }
    // Silences the output, e.g. while tuning on stage. Fades in 20 ms.
    void setOutputMuted (bool shouldMute) noexcept { outputMuted.store (shouldMute); }
    bool isOutputMuted() const noexcept { return outputMuted.load(); }

    // The preset list shown by the browser and the stage mode.
    pedal::ui::PresetSource& getPresetSource() noexcept { return sessionPresets; }

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;
    void updateLatency();

    pedal::SignalChain chain;
    pedal::TestInputPlayer testInput;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    juce::ThreadPool loaderPool { 1 };
    pedal::TunerInput tuner;
    std::atomic<bool> outputMuted { false };
    juce::SmoothedValue<float> muteGain { 1.0f };
    pedal::ui::SessionPresets sessionPresets { *this, [this] { return getAmpModelName(); } };

    juce::File ampModelFile, cabinetFile;
    // Lets callbacks posted by the loader know the processor is gone.
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    std::atomic<float>* inputGain = nullptr;
    std::atomic<float>* gateThreshold = nullptr;
    std::atomic<float>* drop = nullptr;
    std::atomic<float>* cleanBlend = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* cabEnabled = nullptr;
    std::atomic<float>* gateEnabled = nullptr;
    std::atomic<float>* bass = nullptr;
    std::atomic<float>* middle = nullptr;
    std::atomic<float>* treble = nullptr;
    std::atomic<float>* eqEnabled = nullptr;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalAudioProcessor)
};

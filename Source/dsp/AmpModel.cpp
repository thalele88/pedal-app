#include "dsp/AmpModel.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

namespace pedal
{

AmpModel::AmpModel() = default;

AmpModel::~AmpModel()
{
    delete incoming.exchange (nullptr);
    delete retired.exchange (nullptr);
}

void AmpModel::prepare (double newSampleRate, int newMaxBlockSize)
{
    std::lock_guard<std::mutex> lock (loadMutex);
    sampleRate = newSampleRate;
    maxBlockSize = newMaxBlockSize;

    outBuffer.allocate ((size_t) maxBlockSize, true);
    fadeBuffer.allocate ((size_t) maxBlockSize, true);
    scratchSize = maxBlockSize;

    // prepare() is called while audio is stopped, so touching `active` is safe.
    if (active != nullptr)
        active->Reset (sampleRate, maxBlockSize);
    if (auto* pending = incoming.load())
        pending->Reset (sampleRate, maxBlockSize);
}

juce::String AmpModel::load (const juce::File& namFile)
{
    if (! namFile.existsAsFile())
        return juce::String::fromUTF8 ("Arquivo não encontrado: ") + namFile.getFullPathName();

    try
    {
        nam::DspLoadOptions options;
        options.prewarm = false; // prewarmed in install() at the right sample rate
        auto model = nam::get_dsp (std::filesystem::path (namFile.getFullPathName().toStdString()), options);
        return install (std::move (model), namFile.getFileNameWithoutExtension());
    }
    catch (const std::exception& e)
    {
        return juce::String (juce::String::fromUTF8 ("Não foi possível carregar o modelo: ")) + e.what();
    }
}

juce::String AmpModel::loadFromJson (const juce::String& namJson, const juce::String& displayName)
{
    try
    {
        nam::DspLoadOptions options;
        options.prewarm = false;
        auto json = nlohmann::json::parse (namJson.toStdString());
        nam::dspData returnedConfig;
        auto model = nam::get_dsp (json, returnedConfig, options);
        return install (std::move (model), displayName);
    }
    catch (const std::exception& e)
    {
        return juce::String (juce::String::fromUTF8 ("Não foi possível carregar o modelo: ")) + e.what();
    }
}

juce::String AmpModel::install (std::unique_ptr<nam::DSP> model, const juce::String& name)
{
    if (model == nullptr)
        return juce::String::fromUTF8 ("Modelo inválido");

    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
        return juce::String::fromUTF8 ("Só modelos mono são suportados por enquanto");

    std::lock_guard<std::mutex> lock (loadMutex);

    const auto expected = model->GetExpectedSampleRate();
    const bool mismatch = expected > 0 && std::abs (expected - sampleRate) > 1.0
                          && ! model->SupportsArbitrarySampleRate();

    model->SetPrewarmOnReset (true);
    model->Reset (sampleRate, maxBlockSize);

    collectGarbage();
    // If the audio thread has not picked up a previous load yet, replace it.
    delete incoming.exchange (model.release());

    {
        std::lock_guard<std::mutex> nameLock (nameMutex);
        modelName = name;
    }
    modelLoaded = true;
    sampleRateMismatch = mismatch;
    return {};
}

void AmpModel::process (float* samples, int numSamples) noexcept
{
    if (numSamples > scratchSize)
        return; // host broke its promise about block size; pass through

    std::unique_ptr<nam::DSP> previous;

    // Only take the new model once the previously retired one has been freed,
    // otherwise we would have to free memory here.
    if (retired.load (std::memory_order_acquire) == nullptr)
    {
        if (auto* next = incoming.exchange (nullptr, std::memory_order_acq_rel))
        {
            previous.reset (active.release());
            active.reset (next);
        }
    }

    if (active == nullptr)
        return;

    float* input = samples;
    float* output = outBuffer.get();
    active->process (&input, &output, numSamples);

    if (previous != nullptr)
    {
        // Crossfade old -> new over this block so the switch does not click.
        float* oldOutput = fadeBuffer.get();
        previous->process (&input, &oldOutput, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = (float) (i + 1) / (float) numSamples;
            output[i] = oldOutput[i] * (1.0f - t) + output[i] * t;
        }
        retired.store (previous.release(), std::memory_order_release);
    }

    std::copy (output, output + numSamples, samples);
}

void AmpModel::collectGarbage()
{
    delete retired.exchange (nullptr, std::memory_order_acq_rel);
}

juce::String AmpModel::getModelName() const
{
    std::lock_guard<std::mutex> lock (nameMutex);
    return modelName;
}

} // namespace pedal

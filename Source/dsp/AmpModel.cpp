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

LoadedModel::LoadedModel() = default;
LoadedModel::~LoadedModel() = default;

juce::String AmpModel::load (const juce::File& namFile)
{
    juce::String error;
    auto model = build (namFile, error);
    return model != nullptr ? install (std::move (model)) : error;
}

juce::String AmpModel::loadFromJson (const juce::String& namJson, const juce::String& displayName)
{
    juce::String error;
    auto model = buildFromJson (namJson, displayName, error);
    return model != nullptr ? install (std::move (model)) : error;
}

std::unique_ptr<LoadedModel> AmpModel::build (const juce::File& namFile, juce::String& error) const
{
    if (! namFile.existsAsFile())
    {
        error = juce::String::fromUTF8 ("Arquivo não encontrado: ") + namFile.getFullPathName();
        return nullptr;
    }

    try
    {
        nam::DspLoadOptions options;
        options.prewarm = false; // prewarmed in finishBuild() at the right sample rate
        auto model = nam::get_dsp (std::filesystem::path (namFile.getFullPathName().toStdString()), options);
        return finishBuild (std::move (model), namFile.getFileNameWithoutExtension(), error);
    }
    catch (const std::exception& e)
    {
        error = juce::String (juce::String::fromUTF8 ("Não foi possível carregar o modelo: ")) + e.what();
        return nullptr;
    }
}

std::unique_ptr<LoadedModel> AmpModel::buildFromJson (const juce::String& namJson, const juce::String& displayName,
                                                      juce::String& error) const
{
    try
    {
        nam::DspLoadOptions options;
        options.prewarm = false;
        auto json = nlohmann::json::parse (namJson.toStdString());
        nam::dspData returnedConfig;
        auto model = nam::get_dsp (json, returnedConfig, options);
        return finishBuild (std::move (model), displayName, error);
    }
    catch (const std::exception& e)
    {
        error = juce::String (juce::String::fromUTF8 ("Não foi possível carregar o modelo: ")) + e.what();
        return nullptr;
    }
}

std::unique_ptr<LoadedModel> AmpModel::finishBuild (std::unique_ptr<nam::DSP> model, const juce::String& name,
                                                    juce::String& error) const
{
    if (model == nullptr)
    {
        error = juce::String::fromUTF8 ("Modelo inválido");
        return nullptr;
    }

    if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
    {
        error = juce::String::fromUTF8 ("Só modelos mono são suportados por enquanto");
        return nullptr;
    }

    std::unique_ptr<LoadedModel> loaded (new LoadedModel());
    {
        std::lock_guard<std::mutex> lock (loadMutex);
        loaded->preparedSampleRate = sampleRate;
        loaded->preparedBlockSize = maxBlockSize;
    }

    // Prewarming is the slow part, so it runs outside the lock; install()
    // prewarms again if prepare() changed the sample rate in the meantime.
    model->SetPrewarmOnReset (true);
    model->Reset (loaded->preparedSampleRate, loaded->preparedBlockSize);

    loaded->dsp = std::move (model);
    loaded->name = name;
    return loaded;
}

juce::String AmpModel::install (std::unique_ptr<LoadedModel> model)
{
    if (model == nullptr || model->dsp == nullptr)
        return juce::String::fromUTF8 ("Modelo inválido");

    std::lock_guard<std::mutex> lock (loadMutex);

    if (! juce::exactlyEqual (model->preparedSampleRate, sampleRate) || model->preparedBlockSize != maxBlockSize)
    {
        model->dsp->Reset (sampleRate, maxBlockSize);
        model->preparedSampleRate = sampleRate;
        model->preparedBlockSize = maxBlockSize;
    }

    const auto expected = model->dsp->GetExpectedSampleRate();
    const bool mismatch = expected > 0 && std::abs (expected - sampleRate) > 1.0
                          && ! model->dsp->SupportsArbitrarySampleRate();

    collectGarbage();
    unloadRequested = false;
    // If the audio thread has not picked up a previous load yet, replace it.
    delete incoming.exchange (model->dsp.release());

    {
        std::lock_guard<std::mutex> nameLock (nameMutex);
        modelName = model->name;
    }
    modelLoaded = true;
    sampleRateMismatch = mismatch;
    return {};
}

void AmpModel::unload()
{
    std::lock_guard<std::mutex> lock (loadMutex);
    delete incoming.exchange (nullptr);
    unloadRequested = true;

    {
        std::lock_guard<std::mutex> nameLock (nameMutex);
        modelName = {};
    }
    modelLoaded = false;
    sampleRateMismatch = false;
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
        else if (unloadRequested.exchange (false, std::memory_order_acq_rel))
        {
            previous.reset (active.release());
        }
    }

    if (active == nullptr && previous == nullptr)
        return;

    float* input = samples;
    float* output = outBuffer.get();
    if (active != nullptr)
        active->process (&input, &output, numSamples);
    else
        std::copy (samples, samples + numSamples, output); // fading out to the dry signal

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

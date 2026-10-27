#include "dsp/TestInputPlayer.h"

namespace pedal
{

TestInputPlayer::TestInputPlayer() = default;

TestInputPlayer::~TestInputPlayer()
{
    delete incoming.exchange (nullptr);
    delete retired.exchange (nullptr);
}

void TestInputPlayer::prepare (double sampleRate)
{
    hostSampleRate = sampleRate;
}

juce::String TestInputPlayer::load (const juce::File& newFile)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (newFile));
    if (reader == nullptr)
        return juce::String::fromUTF8 ("Não foi possível abrir o arquivo: ") + newFile.getFileName();

    // Ten minutes is plenty for a test riff and keeps memory bounded.
    const auto maxSamples = (juce::int64) (reader->sampleRate * 600.0);
    const int numSamples = (int) juce::jmin (reader->lengthInSamples, maxSamples);
    if (numSamples <= 0)
        return juce::String::fromUTF8 ("O arquivo está vazio: ") + newFile.getFileName();

    const int numChannels = (int) juce::jmax (1u, reader->numChannels);
    juce::AudioBuffer<float> fileAudio (numChannels, numSamples);
    reader->read (&fileAudio, 0, numSamples, 0, true, true);

    auto clip = std::make_unique<Clip>();
    clip->sampleRate = reader->sampleRate;
    clip->audio.setSize (1, numSamples);
    clip->audio.clear();
    for (int ch = 0; ch < numChannels; ++ch)
        clip->audio.addFrom (0, 0, fileAudio, ch, 0, numSamples, 1.0f / (float) numChannels);

    collectGarbage();
    delete incoming.exchange (clip.release());

    {
        std::lock_guard<std::mutex> lock (fileMutex);
        file = newFile;
    }
    clipLoaded = true;
    return {};
}

void TestInputPlayer::collectGarbage()
{
    delete retired.exchange (nullptr);
}

juce::File TestInputPlayer::getFile() const
{
    std::lock_guard<std::mutex> lock (fileMutex);
    return file;
}

void TestInputPlayer::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (retired.load() == nullptr)
    {
        if (auto* next = incoming.exchange (nullptr))
        {
            retired.store (active.release());
            active.reset (next);
            position = 0.0;
        }
    }

    if (! playing.load() || active == nullptr || buffer.getNumChannels() == 0)
        return;

    const auto* clip = active->audio.getReadPointer (0);
    const int length = active->audio.getNumSamples();
    const double step = active->sampleRate / hostSampleRate; // plays at the right pitch at any host rate
    auto* out = buffer.getWritePointer (0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int index = (int) position;
        const float frac = (float) (position - index);
        const float a = clip[index];
        const float b = clip[index + 1 < length ? index + 1 : 0];
        out[i] = a + (b - a) * frac;

        position += step;
        if (position >= (double) length)
            position -= (double) length;
    }

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, out, buffer.getNumSamples());
}

} // namespace pedal

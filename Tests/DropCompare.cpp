// Renders a recording through every drop tuner buffer so they can be compared
// by ear. Use a dry DI recording of the guitar or bass (no amp).
//
//   DropCompare <input.wav> [semitones, default -2]
//
// Writes <input>_drop<st>_rapido.wav, _20ms.wav, _30ms.wav, _40ms.wav and
// _60ms.wav next to the input. Each output is lined up with the input (the
// typical delay of that buffer is removed) so you can also A/B them against
// the original.

#include "dsp/DropTuner.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

namespace
{
bool render (const juce::AudioBuffer<float>& input, double sampleRate, int semitones,
             pedal::DropTuner::Buffer bufferChoice, const juce::File& outFile)
{
    const int blockSize = 128;
    pedal::DropTuner tuner;
    tuner.setEnabled (true);
    tuner.setBuffer (bufferChoice);
    tuner.setSemitones ((float) semitones);
    tuner.prepare (sampleRate, blockSize);
    const int latency = tuner.getLatencySamples();

    const int length = input.getNumSamples();
    juce::AudioBuffer<float> work (1, length + latency + blockSize);
    work.clear();
    work.copyFrom (0, 0, input, 0, 0, length);

    for (int start = 0; start + blockSize <= work.getNumSamples(); start += blockSize)
        tuner.process (work.getWritePointer (0, start), blockSize);

    juce::AudioBuffer<float> out (1, length);
    out.copyFrom (0, 0, work, 0, latency, length);

    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (outFile.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return false;

    std::printf ("%s: delay about %.1f ms\n", outFile.getFileName().toRawUTF8(), 1000.0 * latency / sampleRate);
    return writer->writeFromAudioSampleBuffer (out, 0, length);
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::printf ("usage: DropCompare <input.wav> [semitones, default -2]\n");
        return 1;
    }

    const juce::File inFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const int semitones = argc > 2 ? juce::String (argv[2]).getIntValue() : -2;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
    {
        std::printf ("could not read %s\n", inFile.getFullPathName().toRawUTF8());
        return 1;
    }

    // Mono: the first channel, like the plugin does.
    juce::AudioBuffer<float> input (1, (int) reader->lengthInSamples);
    reader->read (&input, 0, input.getNumSamples(), 0, true, false);

    const auto base = inFile.getParentDirectory().getChildFile (inFile.getFileNameWithoutExtension()
                                                                + "_drop" + juce::String (semitones));
    bool ok = true;
    const struct { pedal::DropTuner::Buffer buffer; const char* suffix; } buffers[] = {
        { pedal::DropTuner::Buffer::fast, "_rapido" }, { pedal::DropTuner::Buffer::ms20, "_20ms" },
        { pedal::DropTuner::Buffer::ms30, "_30ms" }, { pedal::DropTuner::Buffer::ms40, "_40ms" },
        { pedal::DropTuner::Buffer::ms60, "_60ms" } };
    for (const auto& b : buffers)
        ok = render (input, reader->sampleRate, semitones, b.buffer,
                     base.getSiblingFile (base.getFileName() + b.suffix + ".wav")) && ok;
    return ok ? 0 : 1;
}

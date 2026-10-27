// Runs the signal chain offline with the example NAM models and checks that
// it produces sane audio fast enough for real time. Exit code 0 = pass.

#include "dsp/SignalChain.h"
#include "dsp/TestInputPlayer.h"

#include <chrono>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;

void check (bool condition, const char* what)
{
    std::printf ("%s %s\n", condition ? "[ ok ]" : "[FAIL]", what);
    if (! condition)
        ++failures;
}

// A decaying chord-ish signal, close enough to a strummed guitar for a smoke test.
void fillTestSignal (juce::AudioBuffer<float>& buffer, double sampleRate, int64_t& position)
{
    const double freqs[] = { 82.41, 123.47, 164.81, 207.65 };
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const double t = (double) (position + i) / sampleRate;
        const double envelope = std::exp (-3.0 * std::fmod (t, 1.0));
        double v = 0.0;
        for (double f : freqs)
            v += std::sin (juce::MathConstants<double>::twoPi * f * t);
        buffer.setSample (0, i, (float) (0.1 * envelope * v));
    }
    position += buffer.getNumSamples();
}

// Runs a sine through the drop tuner alone and returns the output frequency,
// counted from zero crossings over the last second.
double shiftedFrequency (pedal::DropTuner::Buffer buffer, float semitones, double inputHz, double sampleRate, int blockSize,
                         float tonalityHz = 0.0f)
{
    pedal::DropTuner tuner;
    tuner.setEnabled (true);
    tuner.setBuffer (buffer);
    tuner.setSemitones (semitones);
    tuner.setTonality (tonalityHz);
    tuner.prepare (sampleRate, blockSize);

    const int total = (int) (3.0 * sampleRate);
    const int measureFrom = total - (int) sampleRate;
    std::vector<float> block ((size_t) blockSize);
    int crossings = 0;
    float previous = 0.0f;
    for (int start = 0; start + blockSize <= total; start += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
            block[(size_t) i] = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * inputHz * (start + i) / sampleRate);
        tuner.process (block.data(), blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            if (start + i >= measureFrom && previous < 0.0f && block[(size_t) i] >= 0.0f)
                ++crossings;
            previous = block[(size_t) i];
        }
    }
    return (double) crossings;
}

// Plays a sine, switches drop engines halfway and back, and returns the
// largest jump between neighbouring samples relative to the steady sine's.
double switchClickRatio (double sampleRate, int blockSize)
{
    pedal::DropTuner tuner;
    tuner.setEnabled (true);
    tuner.setSemitones (-2.0f);
    tuner.prepare (sampleRate, blockSize);

    std::vector<float> block ((size_t) blockSize);
    const int total = (int) (4.0 * sampleRate);
    double steadyMax = 0.0, switchMax = 0.0;
    float previous = 0.0f;
    for (int start = 0; start + blockSize <= total; start += blockSize)
    {
        const double t = start / sampleRate;
        tuner.setBuffer (t < 1.5 || t >= 2.5 ? pedal::DropTuner::Buffer::fast : pedal::DropTuner::Buffer::ms30);
        for (int i = 0; i < blockSize; ++i)
            block[(size_t) i] = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 196.0 * (start + i) / sampleRate);
        tuner.process (block.data(), blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const double step = std::abs (block[(size_t) i] - previous);
            previous = block[(size_t) i];
            if (t > 0.5 && t < 1.4)
                steadyMax = std::max (steadyMax, step);
            else if (t >= 1.5)
                switchMax = std::max (switchMax, step);
        }
    }
    return switchMax / steadyMax;
}
} // namespace

int main()
{
    const juce::File models (PEDAL_EXAMPLE_MODELS_DIR);
    const double sampleRate = 48000.0;
    const int blockSize = 128;
    const double seconds = 10.0;

    pedal::SignalChain chain;
    chain.prepare (sampleRate, blockSize);

    const auto error = chain.getAmp().load (models.getChildFile ("A2.nam"));
    check (error.isEmpty(), ("load A2.nam " + error).toRawUTF8());

    pedal::ChainSettings settings;
    settings.pitchOn = true;
    settings.pitchSemitones = -2.0f;
    settings.pitchBuffer = pedal::DropTuner::Buffer::fast;
    settings.gateThresholdDb = -60.0f;

    juce::AudioBuffer<float> buffer (1, blockSize);
    int64_t position = 0;
    bool finite = true;
    double peak = 0.0;
    const int numBlocks = (int) (seconds * sampleRate / blockSize);

    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < numBlocks; ++b)
    {
        if (b == numBlocks / 2)
        {
            // Swap models mid-stream, like changing presets during a song.
            chain.getAmp().collectGarbage();
            const auto swapError = chain.getAmp().load (models.getChildFile ("wavenet.nam"));
            check (swapError.isEmpty(), ("load wavenet.nam mid-stream " + swapError).toRawUTF8());
        }

        // Switch drop buffers while playing, sweep the pitch like a whammy,
        // then turn it off and back on.
        if (b == numBlocks / 4)
        {
            settings.pitchBuffer = pedal::DropTuner::Buffer::ms40;
            settings.pitchTonalityHz = 6000.0f;
        }
        if (b > numBlocks * 3 / 8 && b < numBlocks / 2)
            settings.pitchSemitones = -2.0f - 10.0f * (float) (b - numBlocks * 3 / 8) / (float) (numBlocks / 8);
        if (b == numBlocks * 5 / 8)
            settings.pitchOn = false;
        if (b == numBlocks * 6 / 8)
        {
            settings.pitchBuffer = pedal::DropTuner::Buffer::fast;
            settings.pitchSemitones = -5.0f;
            settings.pitchOn = true;
        }

        fillTestSignal (buffer, sampleRate, position);
        chain.process (buffer, settings);

        for (int i = 0; i < blockSize; ++i)
        {
            const float v = buffer.getSample (0, i);
            finite = finite && std::isfinite (v);
            peak = std::max (peak, (double) std::abs (v));
        }
    }
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    check (finite, "output has no NaN/inf");
    check (peak > 1.0e-4, "output is not silent");
    check (chain.getAmp().getModelName() == "wavenet", "second model is active after swap");

    const double realtimeFactor = seconds / elapsed;
    std::printf ("processed %.0f s of audio in %.2f s (%.1fx real time)\n", seconds, elapsed, realtimeFactor);
    check (realtimeFactor > 2.0, "runs at least 2x faster than real time");

    using Buffer = pedal::DropTuner::Buffer;
    const struct { Buffer buffer; const char* name; double expectedMs; } buffers[] = {
        { Buffer::fast, "fast", 0.0 }, { Buffer::ms20, "20 ms", 20.0 }, { Buffer::ms30, "30 ms", 30.0 },
        { Buffer::ms40, "40 ms", 40.0 }, { Buffer::ms60, "60 ms", 60.0 } };
    for (const auto& m : buffers)
    {
        for (float semitones : { -2.0f, -5.0f, -12.0f, 2.0f, 24.0f, -24.0f, -2.5f })
        {
            // Two octaves down needs more than the 20 ms window can resolve.
            if (m.buffer == Buffer::ms20 && semitones < -12.0f)
                continue;
            const double expected = 196.0 * std::pow (2.0, semitones / 12.0);
            const double measured = shiftedFrequency (m.buffer, semitones, 196.0, sampleRate, blockSize);
            const auto what = juce::String::formatted ("%s buffer %+.1f st: 196 Hz -> %.1f Hz (expected %.1f)",
                                                       m.name, semitones, measured, expected);
            check (std::abs (measured - expected) < 0.01 * expected + 1.0, what.toRawUTF8());
        }

        const double latencyMs = 1000.0 * chain.getDropTuner().getLatencySamples (m.buffer) / sampleRate;
        std::printf ("drop tuner latency, %s buffer: %.1f ms\n", m.name, latencyMs);
        if (m.expectedMs > 0.0)
            check (std::abs (latencyMs - m.expectedMs) < 1.0, "buffer delay matches its label");
        else
            check (latencyMs < 20.0, "fast buffer adds less than 20 ms");
    }

    {
        // Tonality leaves the highs alone but the fundamental still moves.
        const double expected = 196.0 * std::pow (2.0, -2.0 / 12.0);
        const double measured = shiftedFrequency (Buffer::ms30, -2.0f, 196.0, sampleRate, blockSize, 4000.0f);
        const auto what = juce::String::formatted ("30 ms buffer with Tonality 4 kHz: 196 Hz -> %.1f Hz", measured);
        check (std::abs (measured - expected) < 0.01 * expected + 1.0, what.toRawUTF8());
    }

    for (double rate : { 44100.0, 96000.0 })
        for (int size : { 32, 512 })
        {
            const double expected = 196.0 * std::pow (2.0, -2.0 / 12.0);
            const double measured = shiftedFrequency (Buffer::fast, -2.0f, 196.0, rate, size);
            const auto what = juce::String::formatted ("fast mode at %.0f Hz, block %d: 196 Hz -> %.1f Hz", rate, size, measured);
            check (std::abs (measured - expected) < 0.01 * expected + 1.0, what.toRawUTF8());
        }

    const double clickRatio = switchClickRatio (sampleRate, blockSize);
    std::printf ("largest step while switching engines: %.2fx the steady sine\n", clickRatio);
    check (clickRatio < 1.5, "switching drop engines does not click");

    // Test input: a 48 kHz WAV played at a 44.1 kHz host rate, looped.
    {
        pedal::TestInputPlayer player;
        player.prepare (44100.0);
        const auto diError = player.load (juce::File (PEDAL_EXAMPLE_AUDIO_DIR).getChildFile ("input.wav"));
        check (diError.isEmpty(), ("load DI test file " + diError).toRawUTF8());
        player.setPlaying (true);

        juce::AudioBuffer<float> stereo (2, blockSize);
        double diPeak = 0.0;
        bool channelsMatch = true;
        // 3 s of blocks: longer than the 2 s file, so it has to loop.
        for (int b = 0; b < (int) (3.0 * 44100.0 / blockSize); ++b)
        {
            stereo.clear();
            player.process (stereo);
            diPeak = std::max (diPeak, (double) stereo.getMagnitude (0, 0, blockSize));
            channelsMatch = channelsMatch && stereo.getSample (1, blockSize - 1) == stereo.getSample (0, blockSize - 1);
        }
        check (diPeak > 1.0e-4, "DI test input produces sound");
        check (channelsMatch, "DI test input fills every channel");
    }

    return failures == 0 ? 0 : 1;
}

// Runs the signal chain offline with the example NAM models and checks that
// it produces sane audio fast enough for real time. Exit code 0 = pass.

#include "dsp/SignalChain.h"

#include <chrono>
#include <cstdio>

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
    settings.dropSemitones = -2;
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

    std::printf ("drop tuner latency: %d samples (%.1f ms)\n", chain.getDropTuner().getLatencySamples(),
                 1000.0 * chain.getDropTuner().getLatencySamples() / sampleRate);

    return failures == 0 ? 0 : 1;
}

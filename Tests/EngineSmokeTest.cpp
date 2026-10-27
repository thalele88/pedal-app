// Runs the signal chain offline with the example NAM models and checks that
// it produces sane audio fast enough for real time. Exit code 0 = pass.

#include "dsp/SignalChain.h"
#include "dsp/TestInputPlayer.h"

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

    // A 48 kHz capture on a 44.1 kHz interface (like the M-VAVE MK-300) must be
    // resampled: no mismatch warning, and a steady sine should come out at
    // about the same level as when the host runs at the capture's own rate.
    {
        auto sineRms = [&] (double hostRate)
        {
            pedal::AmpModel model;
            model.prepare (hostRate, blockSize);
            const auto err = model.load (models.getChildFile ("wavenet.nam"));
            check (err.isEmpty(), ("load wavenet.nam at " + juce::String (hostRate) + " Hz " + err).toRawUTF8());
            if (hostRate != 48000.0)
                check (! model.hasSampleRateMismatch(), "no sample-rate warning at 44.1 kHz");

            std::vector<float> block ((size_t) blockSize);
            double sum = 0.0;
            int64_t n = 0, pos = 0;
            const int blocks = (int) (2.0 * hostRate / blockSize);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i, ++pos)
                    block[(size_t) i] = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) pos / hostRate);
                model.process (block.data(), blockSize);
                if (b > blocks / 2) // skip the start-up
                    for (float v : block) { sum += (double) v * v; ++n; }
            }
            return std::sqrt (sum / (double) n);
        };

        const double at48 = sineRms (48000.0);
        const double at441 = sineRms (44100.0);
        const double diffDb = 20.0 * std::log10 (at441 / at48);
        std::printf ("220 Hz sine level at 44.1 kHz vs 48 kHz: %+.2f dB\n", diffDb);
        check (std::isfinite (diffDb) && std::abs (diffDb) < 0.5, "resampled model matches the 48 kHz level within 0.5 dB");
    }

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

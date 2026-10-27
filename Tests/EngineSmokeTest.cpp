// Runs the signal chain offline with the example NAM models and checks that
// it produces sane audio fast enough for real time. Exit code 0 = pass.

#include "dsp/SignalChain.h"
#include "dsp/TestInputPlayer.h"
#include "dsp/Tuner.h"

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
// Feeds a plucked-string-like tone through the tuner path and returns the
// detected offset in cents from the expected note (or 1000 if it found none).
float tunerErrorCents (double sampleRate, float hz, int expectedMidiNote)
{
    pedal::TunerInput input;
    input.prepare (sampleRate);
    input.setActive (true);

    // Fundamental plus a strong 2nd and 3rd harmonic, like a real string.
    juce::AudioBuffer<float> block (1, 256);
    int64_t position = 0;
    for (int b = 0; b < (int) (0.25 * sampleRate) / 256; ++b)
    {
        for (int i = 0; i < 256; ++i)
        {
            const double t = (double) (position + i) / sampleRate;
            const double w = juce::MathConstants<double>::twoPi * hz * t;
            block.setSample (0, i, (float) (0.2 * std::sin (w) + 0.25 * std::sin (2.0 * w) + 0.1 * std::sin (3.0 * w)));
        }
        position += 256;
        input.push (block.getReadPointer (0), 256);
    }

    std::vector<float> history (2048), scratch (8192), work (2048);
    const int n = input.pull (scratch.data(), (int) scratch.size());
    if (n < (int) history.size())
        return 1000.0f;
    std::copy (scratch.begin() + n - (int) history.size(), scratch.begin() + n, history.begin());

    const float detected = pedal::PitchDetector::detect (history.data(), (int) history.size(), input.getOutputSampleRate(), work.data());
    const auto reading = pedal::NoteReading::fromFrequency (detected);
    if (reading.midiNote != expectedMidiNote)
        return 1000.0f;
    return reading.cents;
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

    // EQ: flat leaves the sound alone, +12 dB of bass makes a low note about 4x louder.
    {
        auto rmsAt = [&] (float bassDb, bool eqOn)
        {
            pedal::SignalChain eqChain; // no amp loaded = the amp passes the signal through
            eqChain.prepare (sampleRate, blockSize);
            pedal::ChainSettings eqSettings;
            eqSettings.eqEnabled = eqOn;
            eqSettings.bassDb = bassDb;
            juce::AudioBuffer<float> b (1, blockSize);
            double sum = 0.0;
            int64_t n = 0;
            for (int blk = 0; blk < 200; ++blk)
            {
                for (int i = 0; i < blockSize; ++i)
                    b.setSample (0, i, 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 60.0 * (double) (n + i) / sampleRate));
                n += blockSize;
                eqChain.process (b, eqSettings);
                if (blk >= 100)
                    for (int i = 0; i < blockSize; ++i)
                        sum += b.getSample (0, i) * b.getSample (0, i);
            }
            return std::sqrt (sum / (100.0 * blockSize));
        };
        const double off = rmsAt (0.0f, false), flat = rmsAt (0.0f, true), boosted = rmsAt (12.0f, true);
        check (std::abs (flat / off - 1.0) < 0.01, "EQ at 5/5/5 does not change the sound");
        check (boosted / off > 3.0 && boosted / off < 4.5, ("EQ bass +12 dB on 60 Hz gives x" + juce::String (boosted / off, 2)).toRawUTF8());
    }

    // Tuner: low B of a 5-string bass up to the 24th fret of the high E string.
    // Open strings must read within 1 cent; high frets have fewer samples per
    // period after decimation, so they get a little more room.
    const struct { float hz; int midi; float tolerance; const char* name; } notes[] = {
        { 30.87f, 23, 1.0f, "B0 (bass low B)" }, { 41.20f, 28, 1.0f, "E1 (bass low E)" }, { 82.41f, 40, 1.0f, "E2 (guitar low E)" },
        { 110.0f, 45, 1.0f, "A2" }, { 146.83f, 50, 1.0f, "D3" }, { 329.63f, 64, 1.0f, "E4 (high E)" },
        { 659.26f, 76, 3.0f, "E5 (12th fret harmonic)" }, { 987.77f, 83, 3.0f, "B5 (19th fret)" }
    };
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const auto& note : notes)
        {
            const float cents = tunerErrorCents (rate, note.hz, note.midi);
            const auto label = juce::String ("tuner ") + note.name + " @ " + juce::String (rate / 1000.0, 1) + " kHz: "
                               + juce::String (cents, 2) + " cents";
            check (std::abs (cents) < note.tolerance, label.toRawUTF8());
        }
    }
    // A string 20 cents flat must read as flat, not as in tune.
    const float flat = tunerErrorCents (48000.0, 82.41f * std::pow (2.0f, -20.0f / 1200.0f), 40);
    check (flat < -17.0f && flat > -23.0f, ("tuner reads 20 cents flat as " + juce::String (flat, 1)).toRawUTF8());

    return failures == 0 ? 0 : 1;
}

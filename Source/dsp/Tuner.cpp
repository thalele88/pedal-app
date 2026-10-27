#include "dsp/Tuner.h"

namespace pedal
{

void TunerInput::prepare (double sampleRate)
{
    // Decimate to roughly 12 kHz: plenty for pitch, and 4x less work for YIN.
    factor = juce::jmax (1, juce::roundToInt (sampleRate / 12000.0));
    outputRate.store (sampleRate / factor);
    // Two one-pole low-passes at ~2.5 kHz keep the strongest aliasing out.
    lpCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 2500.0 / sampleRate));
    phase = 0;
    lp1 = lp2 = 0.0f;
    fifo.reset();
}

void TunerInput::push (const float* samples, int numSamples) noexcept
{
    if (! active.load (std::memory_order_relaxed))
        return;

    // Decimate into a small stack buffer, then write it in one go.
    float decimated[512];
    int count = 0;

    auto flush = [&]
    {
        const auto scope = fifo.write (count); // drops what does not fit
        if (scope.blockSize1 > 0) std::copy (decimated, decimated + scope.blockSize1, ring.get() + scope.startIndex1);
        if (scope.blockSize2 > 0) std::copy (decimated + scope.blockSize1, decimated + scope.blockSize1 + scope.blockSize2, ring.get() + scope.startIndex2);
        count = 0;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        lp1 += lpCoeff * (samples[i] - lp1);
        lp2 += lpCoeff * (lp1 - lp2);

        if (++phase >= factor)
        {
            phase = 0;
            decimated[count++] = lp2;
            if (count == (int) std::size (decimated))
                flush();
        }
    }

    if (count > 0)
        flush();
}

int TunerInput::pull (float* dest, int maxSamples) noexcept
{
    const auto scope = fifo.read (juce::jmin (maxSamples, fifo.getNumReady()));
    if (scope.blockSize1 > 0) std::copy (ring.get() + scope.startIndex1, ring.get() + scope.startIndex1 + scope.blockSize1, dest);
    if (scope.blockSize2 > 0) std::copy (ring.get() + scope.startIndex2, ring.get() + scope.startIndex2 + scope.blockSize2, dest + scope.blockSize1);
    return scope.blockSize1 + scope.blockSize2;
}

float PitchDetector::detect (const float* x, int numSamples, double sampleRate, float* work) noexcept
{
    // work holds the normalised difference (d) and the raw one (raw) side by side.
    float* d = work;
    float* raw = work + numSamples / 2;

    const int window = numSamples / 2;
    const int minLag = juce::jmax (2, (int) (sampleRate / 1500.0));
    const int maxLag = juce::jmin (window - 1, (int) (sampleRate / 30.0));
    if (maxLag <= minLag)
        return 0.0f;

    // Difference function + cumulative mean normalisation (YIN steps 2 and 3).
    d[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < window; ++j)
        {
            const float diff = x[j] - x[j + tau];
            sum += diff * diff;
        }
        raw[tau] = sum;
        runningSum += sum;
        d[tau] = runningSum > 0.0f ? sum * (float) tau / runningSum : 1.0f;
    }

    // First dip under the threshold wins; this avoids octave-down errors.
    constexpr float threshold = 0.15f;
    int best = -1;
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        if (d[tau] < threshold)
        {
            while (tau + 1 <= maxLag && d[tau + 1] < d[tau])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
        return 0.0f;

    // Parabolic interpolation around the minimum for sub-sample precision. The
    // raw difference is closer to a parabola there than the normalised one,
    // which matters for high notes where the period is only a few samples.
    float lag = (float) best;
    if (best > 1 && best < maxLag)
    {
        const float a = raw[best - 1], b = raw[best], c = raw[best + 1];
        const float denom = a - 2.0f * b + c;
        if (std::abs (denom) > 1.0e-9f)
            lag += 0.5f * (a - c) / denom;
    }

    return (float) (sampleRate / lag);
}

juce::String NoteReading::getName() const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return isValid() ? juce::String (names[midiNote % 12]) : juce::String();
}

NoteReading NoteReading::fromFrequency (float hz, float referenceA4) noexcept
{
    NoteReading r;
    if (hz <= 0.0f)
        return r;

    const float exact = 69.0f + 12.0f * std::log2 (hz / referenceA4);
    r.midiNote = juce::jmax (0, juce::roundToInt (exact));
    r.cents = (exact - (float) r.midiNote) * 100.0f;
    r.frequency = hz;
    return r;
}

} // namespace pedal

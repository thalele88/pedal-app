#include "dsp/SpliceShifter.h"

#include <algorithm>
#include <cmath>

namespace pedal
{

void SpliceShifter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    auto samples = [this] (double ms) { return (int) std::lround (sampleRate * ms / 1000.0); };

    minJump = samples (minJumpMs);
    shortJump = samples (shortJumpMs);
    longJump = samples (longJumpMs);
    fadeLength = std::max (1, samples (fadeMs));
    compareLength = samples (compareMs);
    retryInterval = std::max (1, samples (retryMs));
    typicalLatency = samples (typicalLatencyMs);

    // Enough for the longest delay, a jump back from it and the comparison window.
    const int needed = 2 * (longJump + margin) + compareLength + 2 * fadeLength + 64;
    int size = 1;
    while (size < needed)
        size <<= 1;

    buffer.assign ((size_t) size, 0.0f);
    mask = size - 1;
    reset();
}

void SpliceShifter::reset() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    delay = nextDelay = (double) (minJump + margin);
    fadeRemaining = retryCountdown = searchFrom = 0;
}

float SpliceShifter::read (double d) const noexcept
{
    const double position = (double) writeIndex - d;
    const double floorPosition = std::floor (position);
    const int i = (int) floorPosition;
    const float t = (float) (position - floorPosition);

    // 4-point cubic Hermite
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

double SpliceShifter::meanSquare (int end) const noexcept
{
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < compareLength; i += 4, ++count)
    {
        const float v = at (end - compareLength + i);
        sum += v * v;
    }
    return count > 0 ? sum / count : 0.0;
}

// Normalised correlation of the windows that end at `a` and at `b`.
double SpliceShifter::similarity (int a, int b, int step) const noexcept
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (int i = 0; i < compareLength; i += step)
    {
        const float x = at (a - compareLength + i), y = at (b - compareLength + i);
        ab += x * y;
        aa += x * x;
        bb += y * y;
    }
    return ab / std::sqrt ((aa + 1.0e-12) * (bb + 1.0e-12));
}

// Searches jump sizes in [lo, hi]: first every other size with every other
// sample, then refines around the winner. Returns the signed jump in samples.
int SpliceShifter::bestJump (int readIndex, int lo, int hi, int direction, double& score) const noexcept
{
    int best = hi;
    score = -2.0;
    for (int j = lo; j <= hi; j += 2)
    {
        const double s = similarity (readIndex, readIndex + direction * j, 2);
        if (s > score)
        {
            score = s;
            best = j;
        }
    }

    const int coarse = best;
    score = -2.0;
    for (int j = std::max (lo, coarse - 2); j <= std::min (hi, coarse + 2); ++j)
    {
        const double s = similarity (readIndex, readIndex + direction * j, 1);
        if (s > score)
        {
            score = s;
            best = j;
        }
    }
    return direction * best;
}

// A positive jump moves the read point forward in time (less delay).
void SpliceShifter::startJump (int jump) noexcept
{
    nextDelay = delay - jump;
    fadeRemaining = fadeLength;
    searchFrom = 0;
}

float SpliceShifter::processSample (float x) noexcept
{
    buffer[(size_t) writeIndex] = x;

    float out;
    const double drift = 1.0 - ratio;

    if (fadeRemaining > 0)
    {
        const float t = 1.0f - (float) fadeRemaining / (float) fadeLength;
        const float g = 0.5f - 0.5f * std::cos (3.14159265f * t);
        out = read (delay) * (1.0f - g) + read (nextDelay) * g;
        delay += drift;
        nextDelay += drift;
        if (--fadeRemaining == 0)
            delay = nextDelay;
    }
    else
    {
        out = read (delay);
        delay += drift;

        const int readIndex = writeIndex - (int) std::ceil (delay);

        if (ratio < 1.0)
        {
            // Going down: the delay grows, so jump forward.
            if (delay >= shortJump + margin && --retryCountdown <= 0)
            {
                const int hi = std::min (longJump, (int) delay - margin - 1);
                const bool mustJump = delay >= longJump + margin;

                if (meanSquare (readIndex) < silenceMeanSquare)
                {
                    startJump (hi); // nothing to hear, take the lowest delay
                }
                else
                {
                    // On a retry only the newly reachable (longer) jumps are new.
                    const int lo = mustJump ? minJump : std::max (minJump, searchFrom - 2);
                    double score = 0.0;
                    const int jump = bestJump (readIndex, lo, hi, +1, score);
                    const double waited = std::clamp ((delay - shortJump - margin) / (longJump - shortJump), 0.0, 1.0);
                    const double threshold = matchThresholdShort + (matchThresholdLong - matchThresholdShort) * waited;
                    if (score >= threshold || mustJump)
                    {
                        startJump (jump);
                    }
                    else
                    {
                        retryCountdown = retryInterval;
                        searchFrom = hi;
                    }
                }
            }
        }
        else if (ratio > 1.0)
        {
            // Going up: the delay shrinks, so jump back before it reaches zero.
            if (delay <= margin + (ratio - 1.0) * fadeLength + 1.0)
            {
                double score = 0.0;
                int jump = bestJump (readIndex, minJump, shortJump, -1, score);
                if (score < matchThresholdShort)
                    jump = bestJump (readIndex, minJump, longJump, -1, score);
                startJump (jump);
            }
        }
    }

    writeIndex = (writeIndex + 1) & mask;
    return out;
}

void SpliceShifter::process (const float* in, float* out, int numSamples) noexcept
{
    if (buffer.empty())
        return;

    for (int i = 0; i < numSamples; ++i)
        out[i] = processSample (in[i]);
}

} // namespace pedal

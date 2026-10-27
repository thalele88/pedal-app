#pragma once

#include <vector>

namespace pedal
{

// Low latency pitch shifter that works in the time domain.
//
// The input goes into a short delay line that is read back at a different
// speed (slower to go down in pitch, faster to go up). The read point slowly
// drifts away from the write point, so every so often it has to jump back by
// a few milliseconds. Before each jump it compares the audio around the
// candidate jump sizes and picks the one that lines up best with what was just
// played (one or more whole periods of the note), then crossfades. With a good
// match the jump is inaudible.
//
// It first tries jumps of up to `shortJumpMs`, which keeps the delay low for
// single guitar notes. Chords and low bass notes only line up over longer
// stretches, so when no short jump matches well the delay is allowed to grow,
// accepting a slightly worse match the longer it waits, up to `longJumpMs`.
//
// The delay changes all the time. Averages measured offline: about 7 to 10 ms
// on single guitar notes, 15 ms on strummed chords and 19 ms on bass.
//
// prepare() allocates; everything else is real-time safe.
class SpliceShifter
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    // Frequency ratio, for example 2^(-2/12) for two semitones down.
    void setRatio (double newRatio) noexcept { ratio = newRatio; }

    // `in` and `out` may be the same buffer.
    void process (const float* in, float* out, int numSamples) noexcept;

    // Typical delay in samples, between single notes and chords (see above).
    int getTypicalLatencySamples() const noexcept { return typicalLatency; }
    // Longest delay it can reach, in samples.
    int getMaxLatencySamples() const noexcept { return longJump + margin; }

private:
    float processSample (float x) noexcept;
    float read (double delay) const noexcept;
    float at (int index) const noexcept { return buffer[(size_t) (index & mask)]; }
    double meanSquare (int end) const noexcept;
    double similarity (int a, int b, int step) const noexcept;
    int bestJump (int readIndex, int lo, int hi, int direction, double& score) const noexcept;
    void startJump (int jump) noexcept;

    static constexpr double minJumpMs = 4.0, shortJumpMs = 13.0, longJumpMs = 30.0;
    static constexpr double fadeMs = 4.0, compareMs = 20.0, retryMs = 4.0, typicalLatencyMs = 13.0;
    // Correlation a jump needs: strict at first, relaxed as the delay grows.
    static constexpr double matchThresholdShort = 0.95, matchThresholdLong = 0.7;
    static constexpr double silenceMeanSquare = 1.0e-8; // about -80 dBFS
    static constexpr int margin = 4;                    // room for the interpolator

    std::vector<float> buffer;
    int mask = 0, writeIndex = 0;
    double sampleRate = 48000.0, ratio = 1.0;

    int minJump = 0, shortJump = 0, longJump = 0, fadeLength = 0, compareLength = 0, retryInterval = 0, typicalLatency = 0;

    // Current delay of the read point and, while crossfading, of the new one.
    double delay = 0.0, nextDelay = 0.0;
    int fadeRemaining = 0, retryCountdown = 0, searchFrom = 0;
};

} // namespace pedal

#pragma once

#include <juce_core/juce_core.h>

#include <deque>
#include <mutex>

namespace pedal::tone3000
{

// Sliding-window limiter shared by every request to the TONE3000 API.
// TONE3000 allows 100 requests per minute; we stay a little under that.
//
// acquire() blocks the calling worker thread until a slot is free, so it must
// never be called on the audio or message thread.
class RateLimiter
{
public:
    explicit RateLimiter (int requestsPerWindow = 90, juce::int64 windowLengthMs = 60 * 1000)
        : maxRequests (requestsPerWindow), windowMs (windowLengthMs) {}

    // Waits for a free slot and takes it. Returns false if none freed up
    // within `timeoutMs`.
    bool acquire (int timeoutMs = 30 * 1000);

    // Takes a slot only if one is free right now. `nowMs` is injectable for tests.
    bool tryAcquire (juce::int64 nowMs);

    // Called after a 429: nothing goes out until `retryAfterMs` has passed.
    void backOff (juce::int64 retryAfterMs);

private:
    juce::int64 millisUntilFree (juce::int64 nowMs);

    const int maxRequests;
    const juce::int64 windowMs;
    std::mutex mutex;
    std::deque<juce::int64> sent; // timestamps inside the window
    juce::int64 blockedUntilMs = 0;
};

} // namespace pedal::tone3000

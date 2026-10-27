#include "tone3000/RateLimiter.h"

namespace pedal::tone3000
{

juce::int64 RateLimiter::millisUntilFree (juce::int64 nowMs)
{
    while (! sent.empty() && sent.front() <= nowMs - windowMs)
        sent.pop_front();

    juce::int64 wait = juce::jmax ((juce::int64) 0, blockedUntilMs - nowMs);
    if ((int) sent.size() >= maxRequests)
        wait = juce::jmax (wait, sent.front() + windowMs - nowMs);
    return wait;
}

bool RateLimiter::tryAcquire (juce::int64 nowMs)
{
    std::lock_guard<std::mutex> lock (mutex);
    if (millisUntilFree (nowMs) > 0)
        return false;
    sent.push_back (nowMs);
    return true;
}

bool RateLimiter::acquire (int timeoutMs)
{
    const auto deadline = juce::Time::currentTimeMillis() + timeoutMs;
    for (;;)
    {
        const auto now = juce::Time::currentTimeMillis();
        juce::int64 wait;
        {
            std::lock_guard<std::mutex> lock (mutex);
            wait = millisUntilFree (now);
            if (wait == 0)
            {
                sent.push_back (now);
                return true;
            }
        }
        if (now + wait > deadline)
            return false;
        juce::Thread::sleep ((int) juce::jmin (wait, (juce::int64) 1000));
    }
}

void RateLimiter::backOff (juce::int64 retryAfterMs)
{
    std::lock_guard<std::mutex> lock (mutex);
    blockedUntilMs = juce::jmax (blockedUntilMs, juce::Time::currentTimeMillis() + retryAfterMs);
}

} // namespace pedal::tone3000

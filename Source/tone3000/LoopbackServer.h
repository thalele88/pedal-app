#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>

namespace pedal::tone3000
{

// Receives the OAuth redirect on the desktop (RFC 8252 loopback redirect).
//
// Listens on 127.0.0.1 on a port the OS picks, only while a sign-in is open.
// TONE3000 accepts any loopback redirect URI without registering it, so the
// developer key needs no redirect configuration for this to work.
//
// The browser lands on http://127.0.0.1:<port>/callback?code=..&state=..;
// the query string goes to `onCallback` on the message thread.
class LoopbackServer : private juce::Thread
{
public:
    LoopbackServer();
    ~LoopbackServer() override;

    bool start(); // false if no port could be opened
    void stop();
    bool isRunning() const { return port.load() != 0; }

    // e.g. "http://127.0.0.1:52713/callback". Empty while stopped.
    juce::String getRedirectUri() const;

    std::function<void (juce::String query)> onCallback;

private:
    void run() override;
    void handleConnection (juce::StreamingSocket&);

    juce::StreamingSocket listener;
    std::atomic<int> port { 0 };
};

} // namespace pedal::tone3000

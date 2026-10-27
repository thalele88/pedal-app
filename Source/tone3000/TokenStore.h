#pragma once

#include <juce_core/juce_core.h>

namespace pedal::tone3000
{

// Keeps the TONE3000 sign-in (access + refresh token) between sessions,
// in storage only the current OS user can read:
//
//   macOS / iOS -> Keychain
//   Windows     -> DPAPI-encrypted value in the app's settings folder
//   other       -> plain file in the app's settings folder (dev builds only)
class TokenStore
{
public:
    TokenStore (juce::String service, juce::String account);

    bool save (const juce::String& secret);
    juce::String load() const;
    void remove();

private:
    juce::String service, account;
};

} // namespace pedal::tone3000

#pragma once

#include <juce_core/juce_core.h>

namespace pedal::tone3000
{

// OAuth 2.0 authorization code + PKCE (S256) pieces for the TONE3000 API.
// Only the public client_id (t3k_pub_...) is ever used; the app has no secret.
namespace oauth
{
    struct Pkce
    {
        juce::String codeVerifier;  // 43 unreserved characters, single use
        juce::String codeChallenge; // base64url (SHA-256 (codeVerifier))
        juce::String state;         // CSRF token, must come back unchanged

        static Pkce generate();
    };

    // base64url without padding, as RFC 7636 asks for.
    juce::String base64Url (const void* data, size_t size);

    // Challenge for a given verifier (exposed for the tests).
    juce::String challengeFor (const juce::String& codeVerifier);

    // GET /api/v1/oauth/authorize with the PKCE fields plus `extra`
    // (prompt=select_tone, catalog filters, ...).
    juce::URL authorizeUrl (const juce::String& apiOrigin,
                            const juce::String& clientId,
                            const juce::String& redirectUri,
                            const Pkce& pkce,
                            const juce::StringPairArray& extra);

    // The redirect back to us: `?code=..&state=..[&tone_id=..]`,
    // `?error=..&state=..` or `?canceled=true&state=..`.
    struct Callback
    {
        juce::String code, state, error, toneId;
        bool canceled = false;

        static Callback parse (const juce::String& queryOrUrl);
    };

    // application/x-www-form-urlencoded body for POST /oauth/token.
    juce::String formEncode (const juce::StringPairArray& fields);
} // namespace oauth

} // namespace pedal::tone3000

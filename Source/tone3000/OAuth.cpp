#include "tone3000/OAuth.h"

#include <juce_cryptography/juce_cryptography.h>

#include <random>

namespace pedal::tone3000::oauth
{

namespace
{
// std::random_device reads the OS entropy source on macOS, Windows, iOS and
// Linux, which is what PKCE and `state` need (juce::Random is predictable).
juce::String randomUrlSafeString (int numBytes)
{
    std::random_device device;
    juce::MemoryBlock bytes ((size_t) numBytes);
    for (size_t i = 0; i < bytes.getSize(); ++i)
        bytes[i] = (char) (device() & 0xff);
    return base64Url (bytes.getData(), bytes.getSize());
}
} // namespace

juce::String base64Url (const void* data, size_t size)
{
    return juce::Base64::toBase64 (data, size)
        .replaceCharacter ('+', '-')
        .replaceCharacter ('/', '_')
        .removeCharacters ("=");
}

juce::String challengeFor (const juce::String& codeVerifier)
{
    const juce::SHA256 hash (codeVerifier.toRawUTF8(), codeVerifier.getNumBytesAsUTF8());
    const auto raw = hash.getRawData();
    return base64Url (raw.getData(), raw.getSize());
}

Pkce Pkce::generate()
{
    Pkce p;
    p.codeVerifier = randomUrlSafeString (32); // 43 characters after base64url
    p.codeChallenge = challengeFor (p.codeVerifier);
    p.state = randomUrlSafeString (16);
    return p;
}

juce::URL authorizeUrl (const juce::String& apiOrigin,
                        const juce::String& clientId,
                        const juce::String& redirectUri,
                        const Pkce& pkce,
                        const juce::StringPairArray& extra)
{
    auto url = juce::URL (apiOrigin + "/api/v1/oauth/authorize")
                   .withParameter ("client_id", clientId)
                   .withParameter ("redirect_uri", redirectUri)
                   .withParameter ("response_type", "code")
                   .withParameter ("code_challenge", pkce.codeChallenge)
                   .withParameter ("code_challenge_method", "S256")
                   .withParameter ("state", pkce.state);
    for (auto& key : extra.getAllKeys())
        url = url.withParameter (key, extra[key]);
    return url;
}

Callback Callback::parse (const juce::String& queryOrUrl)
{
    auto query = queryOrUrl.containsChar ('?') ? queryOrUrl.fromFirstOccurrenceOf ("?", false, false) : queryOrUrl;
    query = query.upToFirstOccurrenceOf ("#", false, false);

    Callback cb;
    for (auto& pair : juce::StringArray::fromTokens (query, "&", {}))
    {
        const auto key = juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false));
        const auto value = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false).replaceCharacter ('+', ' '));
        if (key == "code")
            cb.code = value;
        else if (key == "state")
            cb.state = value;
        else if (key == "error")
            cb.error = value;
        else if (key == "tone_id")
            cb.toneId = value;
        else if (key == "canceled")
            cb.canceled = value == "true";
    }
    return cb;
}

juce::String formEncode (const juce::StringPairArray& fields)
{
    juce::StringArray parts;
    for (auto& key : fields.getAllKeys())
        parts.add (juce::URL::addEscapeChars (key, true) + "=" + juce::URL::addEscapeChars (fields[key], true));
    return parts.joinIntoString ("&");
}

} // namespace pedal::tone3000::oauth

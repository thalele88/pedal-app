// Checks the TONE3000 pieces that need no network or account: PKCE, the
// redirect parsing, the rate limiter and the offline model cache.
// Exit code 0 = pass.

#include "tone3000/ModelCache.h"
#include "tone3000/OAuth.h"
#include "tone3000/RateLimiter.h"
#include "tone3000/Tone3000Service.h"

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

void testPkce()
{
    using namespace pedal::tone3000::oauth;

    // RFC 7636 appendix B
    check (challengeFor ("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
           "PKCE S256 challenge matches RFC 7636");

    const auto a = Pkce::generate();
    const auto b = Pkce::generate();
    check (a.codeVerifier.length() == 43, "verifier has 43 characters");
    check (a.codeVerifier.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"),
           "verifier is URL safe");
    check (a.codeVerifier != b.codeVerifier && a.state != b.state, "each sign-in gets fresh secrets");
    check (a.codeChallenge == challengeFor (a.codeVerifier), "challenge belongs to verifier");

    juce::StringPairArray extra;
    extra.set ("prompt", "select_tone");
    const auto url = authorizeUrl ("https://www.tone3000.com", "t3k_pub_x", "http://127.0.0.1:5000/callback", a, extra);
    const auto text = url.toString (true);
    check (text.startsWith ("https://www.tone3000.com/api/v1/oauth/authorize?"), "authorize endpoint");
    check (text.contains ("code_challenge_method=S256") && text.contains ("prompt=select_tone")
               && text.contains ("redirect_uri=http%3A%2F%2F127.0.0.1%3A5000%2Fcallback"),
           "authorize parameters");
    check (! text.contains (a.codeVerifier), "verifier never goes in the URL");
}

void testCallback()
{
    using pedal::tone3000::oauth::Callback;

    const auto ok = Callback::parse ("/callback?code=abc%2B1&state=xyz&tone_id=42");
    check (ok.code == "abc+1" && ok.state == "xyz" && ok.toneId == "42" && ! ok.canceled, "parse successful redirect");

    const auto canceled = Callback::parse ("canceled=true&state=xyz");
    check (canceled.canceled && canceled.code.isEmpty(), "parse canceled redirect");

    const auto error = Callback::parse ("?error=access_denied&state=xyz#frag");
    check (error.error == "access_denied" && error.state == "xyz", "parse error redirect");

    juce::StringPairArray form;
    form.set ("grant_type", "authorization_code");
    form.set ("redirect_uri", "http://127.0.0.1:1/callback");
    check (pedal::tone3000::oauth::formEncode (form)
               == "grant_type=authorization_code&redirect_uri=http%3A%2F%2F127.0.0.1%3A1%2Fcallback",
           "form encoding");
}

void testRateLimiter()
{
    pedal::tone3000::RateLimiter limiter (3, 1000);
    check (limiter.tryAcquire (0) && limiter.tryAcquire (10) && limiter.tryAcquire (20), "first 3 requests pass");
    check (! limiter.tryAcquire (500), "4th request in the same second waits");
    check (limiter.tryAcquire (1001), "slot frees when the oldest leaves the window");
    check (! limiter.tryAcquire (1002), "window is full again");
}

void testCache()
{
    using pedal::tone3000::ModelCache;

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("pedal-t3k-cache", "");
    {
        ModelCache cache (dir);
        check (cache.read ("1").isEmpty(), "empty cache");
        check (cache.write ("presets", "1", "{\"a\":1}"), "write model 1 for presets");
        check (cache.write ("current", "2", "{\"a\":2}"), "write model 2 for current");
        cache.write ("presets", "../evil", "{}");
        check (! dir.getSiblingFile ("evil.nam").exists(), "ids cannot escape the cache folder");
        check (cache.read ("1") == "{\"a\":1}", "read back model 1");

        cache.retain ("current", { "3" }); // switched to a tone that is not on disk yet
        check (cache.read ("2").isEmpty(), "model nobody needs is deleted");
        check (cache.read ("1").isNotEmpty(), "preset model survives");
        check (! cache.claim ("current", "3"), "cannot claim a model that is not on disk");
    }
    {
        ModelCache reopened (dir); // ownership survives a restart
        reopened.retain ("current", {});
        check (reopened.read ("1").isNotEmpty(), "preset model survives a restart");
        reopened.retain ("presets", {});
        check (reopened.getCachedModelIds().isEmpty(), "cache is empty once no preset uses TONE3000");
    }
    dir.deleteRecursively();
}

void testParsing()
{
    using namespace pedal::tone3000;

    const auto tone = Tone::fromVar (juce::JSON::parse (R"({"id":42,"title":"JCM800","gear":"amp","format":"nam",
        "images":["https://img/1.jpg","https://img/2.jpg"],
        "user":{"username":"thales","display_name":null,"avatar_url":"https://img/a.png"}})"));
    check (tone.id == "42" && tone.title == "JCM800" && tone.creator == "thales", "tone parsing falls back to username");
    check (tone.imageUrl == "https://img/1.jpg" && tone.creatorAvatarUrl == "https://img/a.png", "tone image and avatar for lists");

    Tone t;
    auto add = [&t] (const char* id, const char* arch, const char* size)
    {
        Model m;
        m.id = id;
        m.architecture = arch;
        m.size = size;
        t.models.add (m);
    };
    check (Service::defaultModelIndex (t) == -1, "no models, no default");
    add ("1", "2", "nano");
    add ("2", "2", "standard");
    check (Service::defaultModelIndex (t) == 1, "standard A2 model is the default");

    check (Service::attribution ({}) == "Powered by TONE3000", "attribution without creator");
    check (Service::attribution ("thales").endsWith ("thales"), "attribution names the creator");
}
} // namespace

int main()
{
    testPkce();
    testCallback();
    testRateLimiter();
    testCache();
    testParsing();

    if (failures == 0)
        std::printf ("All TONE3000 checks passed\n");
    else
        std::printf ("%d TONE3000 check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}

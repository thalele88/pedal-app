#pragma once

#include "tone3000/LoopbackServer.h"
#include "tone3000/ModelCache.h"
#include "tone3000/OAuth.h"
#include "tone3000/TokenStore.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace pedal::tone3000
{

inline constexpr auto apiOrigin = "https://www.tone3000.com";

// The publishable key (t3k_pub_...) given to CMake with
// -DPEDAL_TONE3000_CLIENT_ID=... Empty when the build has none.
juce::String defaultClientId();

// One capture file of a tone. A tone usually has several (one per amp
// setting or channel, and in different sizes).
struct Model
{
    juce::String id, toneId, name;
    juce::String size;         // "standard", "lite", "feather", "nano", ...
    juce::String architecture; // "1", "2", "custom"
    juce::String url;          // model_url, needs the Bearer token

    static Model fromVar (const juce::var&);
};

struct Tone
{
    juce::String id, title, creator, gear, format, pageUrl;
    juce::String imageUrl, creatorAvatarUrl; // lists must show both (TONE3000 brand rules)
    juce::Array<Model> models; // NAM A2 only

    static Tone fromVar (const juce::var&);
};

// The lists the free API tier allows apps to show (catalog search is not).
enum class ToneList { favorites, trending, latest, downloaded, created };

struct ListResult
{
    juce::String error;
    juce::Array<Tone> tones; // NAM tones only, models not filled in
};

struct ToneResult
{
    juce::String error;
    Tone tone; // with its models
};

struct SelectResult
{
    // connected: a sign-in without picking a tone finished.
    enum class Status { selected, connected, canceled, failed };
    Status status = Status::failed;
    juce::String error;
    Tone tone;
};

// Everything the app does with TONE3000, following the official API:
//
//   - Sign-in: OAuth 2.0 + PKCE with the public client_id (no secret in the
//     app). The authorize page is shown in a WebView inside the app (see
//     Tone3000Panel), or in the system browser with a loopback redirect.
//   - Picking a tone: the `select_tone` flow, where the user browses the
//     catalog on tone3000.com and we get the tone_id back.
//   - Getting a model: GET /models?tone_id=&architecture=2 for its model_url, then the file
//     itself with the Bearer token. The JSON goes to AmpModel::loadFromJson.
//   - Offline copies only for models a preset uses (see ModelCache).
//   - At most 90 requests per minute across all plugin instances (limit is 100).
//
// Threads: the authorize calls on the message thread; fetchModelJson()
// on a background thread only (it blocks on the network). Nothing here may
// ever run on the audio thread.
class Service
{
public:
    explicit Service (juce::String clientId = defaultClientId(),
                      juce::File cacheDirectory = ModelCache::defaultDirectory());
    ~Service();

    bool hasClientId() const { return clientId.isNotEmpty(); }
    bool isConnected() const;
    void disconnect();

    // Starts a sign-in (with the catalog when `selectTone` is true) and returns
    // the authorize URL for the caller to show. The caller hands the query
    // string of the redirect to `redirectUri` to completeAuthorize(). `onDone`
    // runs once, on the message thread, with the chosen tone and its models.
    juce::URL beginAuthorize (const juce::String& redirectUri, bool selectTone, bool showMenuBar,
                              std::function<void (SelectResult)> onDone);
    void completeAuthorize (const juce::String& redirectQuery);
    void cancelAuthorize();
    bool isAuthorizing() const { return authorizing; }
    juce::String getAuthorizeRedirectUri() const { return authRedirectUri; }

    // The same flow in the system browser, with the loopback receiver.
    void selectToneInBrowser (std::function<void (SelectResult)> onDone);

    // Signed-in lists for an in-app browser. `onDone` runs on the message thread.
    void listTones (ToneList, std::function<void (ListResult)> onDone);
    // A tone from a list, with its models, ready for the capture picker.
    void getTone (const juce::String& toneId, std::function<void (ToneResult)> onDone);

    // Blocking, background threads only. Puts the .nam JSON of `modelId` in
    // `namJsonOut` and returns an empty string, or returns an error message.
    // Uses the disk copy when there is one, so presets load offline.
    // With `keepOfflineFor` set ("presets", "current"), the model is stored
    // on disk for that owner.
    juce::String fetchModelJson (const juce::String& toneId, const juce::String& modelId,
                                 juce::String& namJsonOut, const juce::String& keepOfflineFor = {});

    // Tells the cache which models `owner` still needs; the rest is deleted.
    void retainOffline (const juce::String& owner, const juce::StringArray& modelIds);

    // Text TONE3000 asks us to show next to a loaded tone.
    static juce::String attribution (const juce::String& creator);

    // The model to load when the user does not pick one: the first standard
    // A2 model, else the first one.
    static int defaultModelIndex (const Tone&);

private:
    struct Tokens
    {
        juce::String accessToken, refreshToken;
        juce::int64 expiresAtMs = 0;

        bool isValid() const { return accessToken.isNotEmpty() && refreshToken.isNotEmpty(); }
        juce::String toJson() const;
        static Tokens fromJson (const juce::String&);
        static Tokens fromTokenResponse (const juce::var&);
    };

    struct HttpResult
    {
        int status = 0; // 0 = no answer (offline, DNS, timeout)
        juce::MemoryBlock body;
        juce::StringPairArray headers;

        bool ok() const { return status >= 200 && status < 300; }
        juce::String text() const { return body.toString(); }
        juce::var json() const { return juce::JSON::parse (text()); }
    };

    HttpResult send (const juce::String& method, const juce::URL& url,
                     const juce::String& extraHeaders, const juce::String& postBody = {});
    HttpResult authorizedGet (const juce::URL& url);
    juce::String accessToken (bool forceRefresh);
    bool refreshNow();
    juce::String exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirectUri);
    juce::String fetchTone (const juce::String& toneId, Tone& out);
    juce::String fetchList (ToneList, juce::Array<Tone>& out);
    juce::String listModels (const juce::String& toneId, juce::Array<Model>& out);
    juce::String findModelUrl (const juce::String& toneId, const juce::String& modelId, juce::String& urlOut);
    void finishAuthorize (SelectResult);

    Tokens currentTokens() const;
    void storeTokens (const Tokens&);

    static juce::String describe (const HttpResult&);

    const juce::String clientId;
    TokenStore tokenStore;
    ModelCache cache;

    mutable std::mutex tokenMutex;
    Tokens tokens;
    std::mutex refreshMutex; // one refresh at a time across worker threads

    // model_url by model id, from recent listings, so a setlist of models from
    // the same tone costs one listing.
    std::mutex urlMutex;
    std::map<juce::String, std::pair<juce::String, juce::int64>> knownUrls;

    LoopbackServer server;
    oauth::Pkce pkce;
    std::function<void (SelectResult)> onAuthorizeDone;
    juce::String authRedirectUri;
    bool authorizing = false, authSelectsTone = false;

    juce::ThreadPool pool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE (Service)
};

} // namespace pedal::tone3000

#include "tone3000/Tone3000Service.h"

#include "tone3000/RateLimiter.h"

#include <algorithm>
#include <utility>

namespace pedal::tone3000
{

namespace
{
constexpr juce::int64 refreshLeadMs = 60 * 1000;    // refresh a minute before expiry
constexpr juce::int64 knownUrlLifetimeMs = 10 * 60 * 1000;
constexpr int connectTimeoutMs = 15000;

// One limiter for the whole process: several plugin instances share the quota.
RateLimiter& sharedLimiter()
{
    static RateLimiter limiter;
    return limiter;
}

juce::String str (const juce::var& v, const char* key)
{
    const auto value = v.getProperty (key, {});
    return value.isVoid() ? juce::String() : value.toString();
}

juce::String u8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}
} // namespace

juce::String defaultClientId()
{
#ifdef PEDAL_TONE3000_CLIENT_ID
    return juce::String (PEDAL_TONE3000_CLIENT_ID).trim();
#else
    return {};
#endif
}

// ---- Data -------------------------------------------------------------------

Model Model::fromVar (const juce::var& v)
{
    Model m;
    m.id = str (v, "id");
    m.toneId = str (v, "tone_id");
    m.name = str (v, "name");
    m.size = str (v, "size");
    m.architecture = str (v, "architecture_version");
    m.url = str (v, "model_url");
    return m;
}

Tone Tone::fromVar (const juce::var& v)
{
    Tone t;
    t.id = str (v, "id");
    t.title = str (v, "title");
    t.gear = str (v, "gear");
    t.format = str (v, "format");
    t.pageUrl = str (v, "url");

    // display_name is only set for verified creators.
    const auto user = v.getProperty ("user", {});
    t.creator = str (user, "display_name");
    if (t.creator.isEmpty())
        t.creator = str (user, "username");
    return t;
}

// ---- Tokens -----------------------------------------------------------------

juce::String Service::Tokens::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("access_token", accessToken);
    obj->setProperty ("refresh_token", refreshToken);
    obj->setProperty ("expires_at", expiresAtMs);
    return juce::JSON::toString (juce::var (obj), true);
}

Service::Tokens Service::Tokens::fromJson (const juce::String& json)
{
    const auto v = juce::JSON::parse (json);
    Tokens t;
    t.accessToken = str (v, "access_token");
    t.refreshToken = str (v, "refresh_token");
    t.expiresAtMs = (juce::int64) v.getProperty ("expires_at", 0);
    return t;
}

Service::Tokens Service::Tokens::fromTokenResponse (const juce::var& body)
{
    Tokens t;
    t.accessToken = str (body, "access_token");
    t.refreshToken = str (body, "refresh_token");
    const auto expiresIn = (double) body.getProperty ("expires_in", 3600);
    t.expiresAtMs = juce::Time::currentTimeMillis() + (juce::int64) (expiresIn * 1000.0);
    return t;
}

// ---- Lifecycle --------------------------------------------------------------

Service::Service (juce::String id, juce::File cacheDirectory)
    : clientId (std::move (id)),
      tokenStore ("com.thalele88.pedalapp", "tone3000"),
      cache (std::move (cacheDirectory))
{
    tokens = Tokens::fromJson (tokenStore.load());
    server.onCallback = [this, flag = alive] (juce::String query)
    {
        if (flag->load())
            handleRedirect (query, server.getRedirectUri());
    };
}

Service::~Service()
{
    *alive = false;
    server.stop();
    pool.removeAllJobs (true, 20000);
}

Service::Tokens Service::currentTokens() const
{
    std::lock_guard<std::mutex> lock (tokenMutex);
    return tokens;
}

void Service::storeTokens (const Tokens& t)
{
    {
        std::lock_guard<std::mutex> lock (tokenMutex);
        tokens = t;
    }
    if (t.isValid())
        tokenStore.save (t.toJson());
    else
        tokenStore.remove();
}

bool Service::isConnected() const
{
    return currentTokens().isValid();
}

void Service::disconnect()
{
    storeTokens ({});
}

juce::String Service::attribution (const juce::String& creator)
{
    return creator.isEmpty() ? juce::String ("Powered by TONE3000")
                             : "Powered by TONE3000" + u8 (" · ") + creator;
}

int Service::defaultModelIndex (const Tone& tone)
{
    for (int i = 0; i < tone.models.size(); ++i)
        if (tone.models[i].architecture == "2" && tone.models[i].size == "standard")
            return i;
    return tone.models.isEmpty() ? -1 : 0;
}

// ---- HTTP (worker threads) --------------------------------------------------

Service::HttpResult Service::send (const juce::String& method, const juce::URL& url,
                                   const juce::String& extraHeaders, const juce::String& postBody)
{
    HttpResult result;

    // One retry after a 429, once TONE3000 says we may send again.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (! sharedLimiter().acquire())
        {
            result = {};
            result.status = 429;
            return result;
        }

        const auto target = postBody.isNotEmpty() ? url.withPOSTData (postBody) : url;
        int status = 0;
        juce::StringPairArray responseHeaders;
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withHttpRequestCmd (method)
                                 .withExtraHeaders (extraHeaders)
                                 .withConnectionTimeoutMs (connectTimeoutMs)
                                 .withNumRedirectsToFollow (5)
                                 .withStatusCode (&status)
                                 .withResponseHeaders (&responseHeaders);

        result = {};
        if (auto stream = target.createInputStream (options))
            stream->readIntoMemoryBlock (result.body);
        result.status = status;
        result.headers = responseHeaders;

        if (status != 429)
            break;

        const int retryAfter = responseHeaders["Retry-After"].getIntValue();
        sharedLimiter().backOff ((juce::int64) juce::jlimit (1, 60, retryAfter > 0 ? retryAfter : 10) * 1000);
    }

    return result;
}

bool Service::refreshNow()
{
    std::lock_guard<std::mutex> single (refreshMutex);

    const auto current = currentTokens();
    if (! current.isValid())
        return false;
    // Another thread may have refreshed while we waited for the lock.
    if (juce::Time::currentTimeMillis() < current.expiresAtMs - refreshLeadMs)
        return true;

    juce::StringPairArray form;
    form.set ("grant_type", "refresh_token");
    form.set ("refresh_token", current.refreshToken);
    form.set ("client_id", clientId);

    const auto r = send ("POST", juce::URL (juce::String (apiOrigin) + "/api/v1/oauth/token"),
                         "Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json",
                         oauth::formEncode (form));
    if (r.ok())
    {
        const auto fresh = Tokens::fromTokenResponse (r.json());
        if (fresh.isValid())
        {
            storeTokens (fresh);
            return true;
        }
    }

    if (r.status == 400 || r.status == 401)
    {
        storeTokens ({}); // invalid_grant: the sign-in is over, the user must sign in again
        return false;
    }

    return true; // offline or server error: keep the tokens, this request fails
}

juce::String Service::accessToken (bool forceRefresh)
{
    auto current = currentTokens();
    if (! current.isValid())
        return {};

    if (forceRefresh)
    {
        std::lock_guard<std::mutex> lock (tokenMutex);
        if (tokens.accessToken == current.accessToken)
            tokens.expiresAtMs = 0; // makes refreshNow() actually refresh
    }

    if (forceRefresh || juce::Time::currentTimeMillis() > current.expiresAtMs - refreshLeadMs)
    {
        if (! refreshNow())
            return {};
        current = currentTokens();
    }
    return current.accessToken;
}

Service::HttpResult Service::authorizedGet (const juce::URL& url)
{
    auto token = accessToken (false);
    if (token.isEmpty())
    {
        HttpResult r;
        r.status = 401;
        return r;
    }

    auto r = send ("GET", url, "Authorization: Bearer " + token);

    // The token can expire between the check and the request: refresh once.
    if (r.status == 401)
    {
        token = accessToken (true);
        if (token.isNotEmpty())
            r = send ("GET", url, "Authorization: Bearer " + token);
    }
    return r;
}

juce::String Service::describe (const HttpResult& r)
{
    switch (r.status)
    {
        case 0:   return u8 ("Sem conexão com o TONE3000. Confira a internet.");
        case 401: return u8 ("Entre de novo no TONE3000 para baixar este tom.");
        case 403: return u8 ("O TONE3000 não liberou este tom para a sua conta.");
        case 404: return u8 ("Este tom não existe mais no TONE3000.");
        case 429: return u8 ("Muitos pedidos ao TONE3000. Espere um minuto e tente de novo.");
        default:  break;
    }

    const auto body = r.json();
    auto detail = str (body, "error_description");
    if (detail.isEmpty())
        detail = str (body, "error");
    return "TONE3000 respondeu " + juce::String (r.status) + (detail.isNotEmpty() ? ": " + detail : juce::String());
}

juce::String Service::exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirectUri)
{
    juce::StringPairArray form;
    form.set ("grant_type", "authorization_code");
    form.set ("code", code);
    form.set ("code_verifier", verifier);
    form.set ("redirect_uri", redirectUri);
    form.set ("client_id", clientId);

    const auto r = send ("POST", juce::URL (juce::String (apiOrigin) + "/api/v1/oauth/token"),
                         "Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json",
                         oauth::formEncode (form));
    if (! r.ok())
        return r.status == 401 ? u8 ("O TONE3000 recusou o login. Tente de novo.") : describe (r);

    const auto fresh = Tokens::fromTokenResponse (r.json());
    if (! fresh.isValid())
        return u8 ("Resposta de login inválida do TONE3000.");

    storeTokens (fresh);
    return {};
}

juce::String Service::listModels (const juce::String& toneId, juce::Array<Model>& out)
{
    out.clear();

    // Without `architecture` the API returns A1 + custom models only, so ask
    // for A2 first (cheapest to run) and then for the rest.
    for (const char* architecture : { "2", "" })
    {
        auto url = juce::URL (juce::String (apiOrigin) + "/api/v1/models")
                       .withParameter ("tone_id", toneId)
                       .withParameter ("page_size", "100");
        if (*architecture != 0)
            url = url.withParameter ("architecture", architecture);

        const auto r = authorizedGet (url);
        if (! r.ok())
            return describe (r);

        if (auto* data = r.json().getProperty ("data", {}).getArray())
        {
            for (const auto& item : *data)
            {
                auto model = Model::fromVar (item);
                const bool seen = std::any_of (out.begin(), out.end(), [&] (const Model& m) { return m.id == model.id; });
                if (model.id.isNotEmpty() && model.url.isNotEmpty() && ! seen)
                    out.add (model);
            }
        }
    }

    const auto now = juce::Time::currentTimeMillis();
    std::lock_guard<std::mutex> lock (urlMutex);
    for (const auto& m : out)
        knownUrls[m.id] = { m.url, now };
    return {};
}

juce::String Service::findModelUrl (const juce::String& toneId, const juce::String& modelId, juce::String& urlOut)
{
    {
        std::lock_guard<std::mutex> lock (urlMutex);
        const auto it = knownUrls.find (modelId);
        if (it != knownUrls.end() && juce::Time::currentTimeMillis() - it->second.second < knownUrlLifetimeMs)
        {
            urlOut = it->second.first;
            return {};
        }
    }

    juce::Array<Model> models;
    if (auto error = listModels (toneId, models); error.isNotEmpty())
        return error;

    for (const auto& m : models)
    {
        if (m.id == modelId)
        {
            urlOut = m.url;
            return {};
        }
    }
    return u8 ("Esta captura não está mais disponível no TONE3000.");
}

juce::String Service::fetchTone (const juce::String& toneId, Tone& out)
{
    const auto r = authorizedGet (juce::URL (juce::String (apiOrigin) + "/api/v1/tones/" + juce::URL::addEscapeChars (toneId, false)));
    if (! r.ok())
        return describe (r);

    out = Tone::fromVar (r.json());
    if (out.id.isEmpty())
        out.id = toneId;
    return listModels (toneId, out.models);
}

juce::String Service::fetchModelJson (const juce::String& toneId, const juce::String& modelId,
                                      juce::String& namJsonOut, const juce::String& keepOfflineFor)
{
    jassert (! juce::MessageManager::existsAndIsCurrentThread()); // blocks on the network

    namJsonOut.clear();

    // The disk copy first: on stage it is faster and works without internet.
    if (auto cached = cache.read (modelId); cached.isNotEmpty())
    {
        if (keepOfflineFor.isNotEmpty())
            cache.claim (keepOfflineFor, modelId);
        namJsonOut = cached;
        return {};
    }

    if (! hasClientId())
        return u8 ("Esta versão foi compilada sem o client_id do TONE3000 (veja o README).");
    if (! isConnected())
        return u8 ("Entre no TONE3000 para baixar este tom.");

    juce::String modelUrl;
    if (auto error = findModelUrl (toneId, modelId, modelUrl); error.isNotEmpty())
        return error;

    const auto r = authorizedGet (juce::URL (modelUrl));
    if (! r.ok())
        return describe (r);

    auto json = r.text();
    if (! json.trimStart().startsWithChar ('{'))
        return u8 ("O arquivo baixado do TONE3000 não é uma captura NAM.");

    if (keepOfflineFor.isNotEmpty())
        cache.write (keepOfflineFor, modelId, json);

    namJsonOut = std::move (json);
    return {};
}

void Service::retainOffline (const juce::String& owner, const juce::StringArray& modelIds)
{
    cache.retain (owner, modelIds);
}

// ---- select_tone flow (message thread) --------------------------------------

void Service::selectTone (std::function<void (SelectResult)> onDone)
{
    JUCE_ASSERT_MESSAGE_THREAD
    cancelSelect();

    onSelectDone = std::move (onDone);
    selecting = true;

    if (! hasClientId())
    {
        finishSelect ({ SelectResult::Status::failed, u8 ("Esta versão foi compilada sem o client_id do TONE3000 (veja o README)."), {} });
        return;
    }

    if (! server.start())
    {
        finishSelect ({ SelectResult::Status::failed, u8 ("Não consegui abrir uma porta local para o login."), {} });
        return;
    }

    pkce = oauth::Pkce::generate();

    juce::StringPairArray extra;
    extra.set ("prompt", "select_tone");
    extra.set ("format", "nam");     // amp captures; the app loads IRs from files for now
    extra.set ("architecture", "2"); // A2 catalog: the lightest models to run live
    extra.set ("preview", "true");   // lets the user hear tones before picking
    extra.set ("menubar", "true");

    const auto url = oauth::authorizeUrl (apiOrigin, clientId, server.getRedirectUri(), pkce, extra);
    if (! url.launchInDefaultBrowser())
    {
        server.stop();
        finishSelect ({ SelectResult::Status::failed, u8 ("Não consegui abrir o navegador."), {} });
    }
}

void Service::cancelSelect()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! selecting)
        return;
    server.stop();
    finishSelect ({ SelectResult::Status::canceled, {}, {} });
}

void Service::handleRedirect (const juce::String& query, const juce::String& redirectUri)
{
    if (! selecting)
        return;

    server.stop(); // one redirect per sign-in

    const auto cb = oauth::Callback::parse (query);
    if (pkce.state.isEmpty() || cb.state != pkce.state)
        return finishSelect ({ SelectResult::Status::failed, u8 ("Login inválido (state diferente). Tente de novo."), {} });
    if (cb.canceled && cb.code.isEmpty())
        return finishSelect ({ SelectResult::Status::canceled, {}, {} });
    if (cb.error.isNotEmpty())
        return finishSelect ({ SelectResult::Status::failed, "TONE3000: " + cb.error, {} });
    if (cb.code.isEmpty())
        return finishSelect ({ SelectResult::Status::failed, u8 ("O TONE3000 não devolveu o código de login."), {} });

    // The verifier is single use: it leaves with this exchange.
    const auto verifier = std::exchange (pkce, {}).codeVerifier;

    pool.addJob ([this, flag = alive, code = cb.code, toneId = cb.toneId, verifier, redirectUri]
    {
        if (! flag->load())
            return;

        SelectResult result;
        auto error = exchangeCode (code, verifier, redirectUri);
        if (error.isEmpty() && toneId.isEmpty())
            error = u8 ("Nenhum tom foi escolhido.");
        if (error.isEmpty())
            error = fetchTone (toneId, result.tone);
        if (error.isEmpty() && result.tone.models.isEmpty())
            error = u8 ("Este tom não tem capturas NAM que o app consiga abrir.");

        result.status = error.isEmpty() ? SelectResult::Status::selected : SelectResult::Status::failed;
        result.error = error;

        juce::MessageManager::callAsync ([this, flag, result]
        {
            if (flag->load())
                finishSelect (result);
        });
    });
}

void Service::finishSelect (SelectResult result)
{
    selecting = false;
    pkce = {};
    if (auto done = std::exchange (onSelectDone, nullptr))
        done (std::move (result));
}

} // namespace pedal::tone3000

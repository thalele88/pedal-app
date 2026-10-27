#include "presets/ModelBank.h"

namespace pedal
{

ModelBank::ModelBank (AmpModel& ampToFeed) : amp (ampToFeed) {}

ModelBank::~ModelBank()
{
    *alive = false;
    selectPool.removeAllJobs (true, 10000);
    pool.removeAllJobs (true, 10000);
}

void ModelBank::setToneFetcher (ToneFetcher newFetcher)
{
    std::lock_guard<std::mutex> lock (mutex);
    fetcher = std::move (newFetcher);
}

juce::String ModelBank::fetch (const ToneRef& ref, juce::String& json)
{
    if (ref.source == ToneRef::Source::localFile)
    {
        const juce::File file (ref.localPath);
        if (! file.existsAsFile())
            return juce::String::fromUTF8 ("Arquivo não encontrado: ") + ref.localPath;
        json = file.loadFileAsString();
        return json.isEmpty() ? juce::String::fromUTF8 ("Arquivo vazio: ") + ref.localPath : juce::String();
    }

    ToneFetcher f;
    {
        std::lock_guard<std::mutex> lock (mutex);
        f = fetcher;
    }
    if (! f)
        return juce::String::fromUTF8 ("TONE3000 não conectado");
    return f (ref, json);
}

void ModelBank::preload (const std::vector<ToneRef>& tones)
{
    juce::StringArray keys;
    {
        std::lock_guard<std::mutex> lock (mutex);
        for (auto& [key, entry] : entries)
            entry.wanted = false;

        for (const auto& tone : tones)
        {
            if (tone.isEmpty())
                continue;
            auto& entry = entries[tone.getKey()];
            entry.ref = tone;
            entry.wanted = true;
            keys.addIfNotAlreadyThere (tone.getKey());
        }

        for (auto it = entries.begin(); it != entries.end();)
            it = it->second.wanted ? std::next (it) : entries.erase (it);
    }

    for (const auto& key : keys)
        schedulePrepare (key);
}

void ModelBank::schedulePrepare (const juce::String& key)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        const auto it = entries.find (key);
        if (it == entries.end() || ! it->second.wanted || it->second.spare != nullptr || it->second.building)
            return;
        it->second.building = true;
    }
    pool.addJob ([this, key] { prepareEntry (key); });
}

void ModelBank::prepareEntry (const juce::String& key)
{
    ToneRef ref;
    juce::String json;
    {
        std::lock_guard<std::mutex> lock (mutex);
        const auto it = entries.find (key);
        if (it == entries.end())
            return;
        if (! it->second.wanted || it->second.spare != nullptr)
        {
            it->second.building = false;
            return;
        }
        ref = it->second.ref;
        json = it->second.json;
    }

    juce::String error;
    if (json.isEmpty())
        error = fetch (ref, json);

    std::unique_ptr<LoadedModel> model;
    if (error.isEmpty())
        model = amp.buildFromJson (json, ref.getName(), error);

    std::lock_guard<std::mutex> lock (mutex);
    const auto it = entries.find (key);
    if (it == entries.end())
        return; // dropped from the setlist while we were building
    auto& entry = it->second;
    entry.building = false;
    entry.error = error;
    if (error.isEmpty())
        entry.json = json;
    if (entry.wanted)
        entry.spare = std::move (model);
}

void ModelBank::select (const ToneRef& tone, std::function<void (juce::String)> onDone)
{
    if (tone.isEmpty())
    {
        {
            std::lock_guard<std::mutex> lock (installMutex);
            ++selectGeneration;
            amp.unload();
        }
        if (onDone)
            onDone ({});
        return;
    }

    const auto key = tone.getKey();
    std::unique_ptr<LoadedModel> ready;
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (const auto it = entries.find (key); it != entries.end())
            ready = std::move (it->second.spare);
    }

    if (ready != nullptr)
    {
        juce::String error;
        {
            std::lock_guard<std::mutex> lock (installMutex);
            ++selectGeneration;
            error = amp.install (std::move (ready));
        }
        schedulePrepare (key); // the spare was used; build the next one
        if (onDone)
            onDone (error);
        return;
    }

    // Not preloaded: load it now, off the message thread.
    const int generation = ++selectGeneration;
    selectPool.addJob ([this, tone, key, generation, onDone, aliveFlag = alive]
    {
        if (generation != selectGeneration.load())
            return; // another preset was picked before this one started

        juce::String json, error;
        {
            std::lock_guard<std::mutex> lock (mutex);
            if (const auto it = entries.find (key); it != entries.end())
                json = it->second.json;
        }
        if (json.isEmpty())
            error = fetch (tone, json);

        std::unique_ptr<LoadedModel> model;
        if (error.isEmpty())
            model = amp.buildFromJson (json, tone.getName(), error);

        {
            std::lock_guard<std::mutex> lock (installMutex);
            if (generation != selectGeneration.load())
                return;
            if (model != nullptr)
                error = amp.install (std::move (model));
        }

        juce::MessageManager::callAsync ([onDone, error, aliveFlag]
        {
            if (aliveFlag->load() && onDone)
                onDone (error);
        });
    });
}

ModelBank::Status ModelBank::getStatus() const
{
    Status status;
    std::lock_guard<std::mutex> lock (mutex);
    for (const auto& [key, entry] : entries)
    {
        ++status.total;
        if (entry.error.isNotEmpty())
            ++status.failed;
        else if (entry.json.isNotEmpty())
            ++status.ready;
        status.busy = status.busy || entry.building;
    }
    return status;
}

bool ModelBank::isReady (const ToneRef& tone) const
{
    std::lock_guard<std::mutex> lock (mutex);
    const auto it = entries.find (tone.getKey());
    return it != entries.end() && it->second.json.isNotEmpty() && it->second.error.isEmpty();
}

juce::String ModelBank::getError (const ToneRef& tone) const
{
    std::lock_guard<std::mutex> lock (mutex);
    const auto it = entries.find (tone.getKey());
    return it != entries.end() ? it->second.error : juce::String();
}

} // namespace pedal

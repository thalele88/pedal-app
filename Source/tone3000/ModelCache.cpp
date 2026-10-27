#include "tone3000/ModelCache.h"

namespace pedal::tone3000
{

namespace
{
constexpr auto indexFileName = "owners.json";

// Model ids are numbers today; anything else is stripped so an id can never
// point outside the cache folder.
juce::String safeId (const juce::String& modelId)
{
    return modelId.retainCharacters ("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_");
}
} // namespace

ModelCache::ModelCache (juce::File dir) : directory (std::move (dir)) {}

juce::File ModelCache::defaultDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Pedal App")
        .getChildFile ("TONE3000")
        .getChildFile ("models");
}

juce::File ModelCache::fileFor (const juce::String& modelId) const
{
    return directory.getChildFile (safeId (modelId) + ".nam");
}

juce::String ModelCache::read (const juce::String& modelId) const
{
    if (safeId (modelId).isEmpty())
        return {};

    std::lock_guard<std::mutex> lock (mutex);
    const auto file = fileFor (modelId);
    return file.existsAsFile() ? file.loadFileAsString() : juce::String();
}

bool ModelCache::write (const juce::String& owner, const juce::String& modelId, const juce::String& namJson)
{
    if (safeId (modelId).isEmpty() || owner.isEmpty() || namJson.isEmpty())
        return false;

    std::lock_guard<std::mutex> lock (mutex);
    loadIndex(); // another plugin instance may have changed it
    if (! directory.createDirectory())
        return false;

    // Write next to the target first so a crash never leaves half a model.
    const auto target = fileFor (modelId);
    juce::TemporaryFile temp (target);
    if (! temp.getFile().replaceWithText (namJson) || ! temp.overwriteTargetFileWithTemporary())
        return false;

    owners[owner].addIfNotAlreadyThere (modelId);
    saveIndex();
    return true;
}

bool ModelCache::claim (const juce::String& owner, const juce::String& modelId)
{
    if (safeId (modelId).isEmpty() || owner.isEmpty())
        return false;

    std::lock_guard<std::mutex> lock (mutex);
    loadIndex();
    if (! fileFor (modelId).existsAsFile())
        return false;

    if (! owners[owner].contains (modelId))
    {
        owners[owner].add (modelId);
        saveIndex();
    }
    return true;
}

void ModelCache::retain (const juce::String& owner, const juce::StringArray& modelIds)
{
    std::lock_guard<std::mutex> lock (mutex);
    loadIndex();
    if (modelIds.isEmpty())
        owners.erase (owner);
    else
        owners[owner] = modelIds;
    deleteUnowned();
    saveIndex();
}

juce::StringArray ModelCache::getCachedModelIds() const
{
    std::lock_guard<std::mutex> lock (mutex);
    juce::StringArray ids;
    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, "*.nam"))
        ids.add (file.getFileNameWithoutExtension());
    ids.sort (true);
    return ids;
}

void ModelCache::deleteUnowned()
{
    juce::StringArray needed;
    for (const auto& [owner, ids] : owners)
        for (const auto& id : ids)
            needed.addIfNotAlreadyThere (safeId (id));

    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, "*.nam"))
        if (! needed.contains (file.getFileNameWithoutExtension()))
            file.deleteFile();
}

void ModelCache::loadIndex()
{
    owners.clear();
    const auto json = juce::JSON::parse (directory.getChildFile (indexFileName));
    if (auto* obj = json.getDynamicObject())
    {
        for (const auto& prop : obj->getProperties())
        {
            juce::StringArray ids;
            if (auto* arr = prop.value.getArray())
                for (const auto& id : *arr)
                    ids.add (id.toString());
            owners[prop.name.toString()] = ids;
        }
    }
}

void ModelCache::saveIndex() const
{
    auto* obj = new juce::DynamicObject();
    for (const auto& [owner, ids] : owners)
    {
        juce::Array<juce::var> arr;
        for (const auto& id : ids)
            arr.add (id);
        obj->setProperty (owner, arr);
    }

    if (directory.createDirectory())
        directory.getChildFile (indexFileName).replaceWithText (juce::JSON::toString (juce::var (obj)));
}

} // namespace pedal::tone3000

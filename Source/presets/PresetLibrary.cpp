#include "presets/PresetLibrary.h"

namespace pedal
{
namespace
{
template <typename Item>
void sortByName (std::vector<Item>& items)
{
    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b) {
        return a.name.compareNatural (b.name) < 0;
    });
}

template <typename Item>
auto findById (std::vector<Item>& items, const juce::String& id)
{
    return std::find_if (items.begin(), items.end(), [&] (const Item& i) { return i.id == id; });
}

juce::String writeFileSafely (const juce::File& file, const juce::String& text)
{
    if (! file.getParentDirectory().createDirectory())
        return juce::String::fromUTF8 ("Não foi possível criar a pasta ") + file.getParentDirectory().getFullPathName();

    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (text, false, false, "\n") || ! temp.overwriteTargetFileWithTemporary())
        return juce::String::fromUTF8 ("Não foi possível salvar ") + file.getFullPathName();
    return {};
}
} // namespace

PresetLibrary::PresetLibrary (const juce::File& rootFolder) : root (rootFolder)
{
    rescan();
}

juce::File PresetLibrary::getDefaultFolder()
{
   #if JUCE_IOS
    // Documents shows up in the Files app, so presets can be copied in and out.
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
   #elif JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/PedalApp");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("PedalApp");
   #endif
}

void PresetLibrary::rescan()
{
    presets.clear();
    setlists.clear();
    presetFiles.clear();
    setlistFiles.clear();
    scanErrors.clear();

    auto scan = [this] (const juce::File& folder, auto& items, auto& files, auto parse)
    {
        for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            juce::String error;
            auto item = parse (file.loadFileAsString(), error);
            if (! item)
            {
                scanErrors.add (file.getFileName() + ": " + error);
                continue;
            }
            // Two files with the same id (e.g. a copied file): keep the first.
            if (files.count (item->id) > 0)
                continue;
            files[item->id] = file;
            items.push_back (std::move (*item));
        }
        sortByName (items);
    };

    scan (getPresetFolder(), presets, presetFiles, [] (const juce::String& j, juce::String& e) { return Preset::fromJson (j, e); });
    scan (getSetlistFolder(), setlists, setlistFiles, [] (const juce::String& j, juce::String& e) { return Setlist::fromJson (j, e); });
}

template <typename Item>
juce::String PresetLibrary::writeItem (std::vector<Item>& items, std::map<juce::String, juce::File>& files,
                                       Item& item, const juce::File& folder)
{
    if (item.id.isEmpty())
        item.id = createPresetId();
    item.name = item.name.trim();
    if (item.name.isEmpty())
        return juce::String::fromUTF8 ("Dê um nome antes de salvar");

    const auto existing = files.find (item.id);
    auto baseName = juce::File::createLegalFileName (item.name);
    if (baseName.isEmpty())
        baseName = "preset";

    // Keep the current file when the name did not change; otherwise pick a
    // free file name so two presets called "Limpo" do not overwrite each other.
    auto keepsName = [&baseName] (const juce::File& f)
    {
        const auto stem = f.getFileNameWithoutExtension();
        return stem == baseName || (stem.startsWith (baseName + " (") && stem.endsWith (")"));
    };
    juce::File target;
    if (existing != files.end() && keepsName (existing->second))
        target = existing->second;
    else
        target = folder.getChildFile (baseName + ".json").getNonexistentSibling (true);

    if (auto error = writeFileSafely (target, item.toJson()); error.isNotEmpty())
        return error;

    if (existing != files.end() && existing->second != target)
        existing->second.deleteFile();
    files[item.id] = target;

    if (auto it = findById (items, item.id); it != items.end())
        *it = item;
    else
        items.push_back (item);
    sortByName (items);
    return {};
}

template <typename Item>
juce::String PresetLibrary::removeItem (std::vector<Item>& items, std::map<juce::String, juce::File>& files,
                                        const juce::String& id)
{
    const auto file = files.find (id);
    if (file == files.end())
        return juce::String::fromUTF8 ("Não encontrado");
    if (! file->second.deleteFile())
        return juce::String::fromUTF8 ("Não foi possível apagar ") + file->second.getFullPathName();
    files.erase (file);
    items.erase (findById (items, id));
    return {};
}

const Preset* PresetLibrary::findPreset (const juce::String& id) const
{
    for (const auto& p : presets)
        if (p.id == id)
            return &p;
    return nullptr;
}

std::vector<const Preset*> PresetLibrary::getFavourites() const
{
    std::vector<const Preset*> result;
    for (const auto& p : presets)
        if (p.favourite)
            result.push_back (&p);
    return result;
}

juce::String PresetLibrary::savePreset (Preset& preset)
{
    return writeItem (presets, presetFiles, preset, getPresetFolder());
}

juce::String PresetLibrary::removePreset (const juce::String& id)
{
    return removeItem (presets, presetFiles, id);
}

juce::String PresetLibrary::setFavourite (const juce::String& id, bool favourite)
{
    const auto* found = findPreset (id);
    if (found == nullptr)
        return juce::String::fromUTF8 ("Preset não encontrado");
    if (found->favourite == favourite)
        return {};
    auto copy = *found;
    copy.favourite = favourite;
    return savePreset (copy);
}

const Setlist* PresetLibrary::findSetlist (const juce::String& id) const
{
    for (const auto& s : setlists)
        if (s.id == id)
            return &s;
    return nullptr;
}

std::vector<const Preset*> PresetLibrary::resolve (const Setlist& setlist) const
{
    std::vector<const Preset*> result;
    for (const auto& entry : setlist.entries)
        if (const auto* p = findPreset (entry.presetId))
            result.push_back (p);
    return result;
}

juce::String PresetLibrary::saveSetlist (Setlist& setlist)
{
    return writeItem (setlists, setlistFiles, setlist, getSetlistFolder());
}

juce::String PresetLibrary::removeSetlist (const juce::String& id)
{
    return removeItem (setlists, setlistFiles, id);
}

juce::File PresetLibrary::findIr (const juce::String& path) const
{
    if (path.isEmpty())
        return {};
    // A path saved on another OS may not even be a valid absolute path here.
    if (juce::File::isAbsolutePath (path))
        if (juce::File saved (path); saved.existsAsFile())
            return saved;

    const auto name = path.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
    if (auto local = getIrFolder().getChildFile (name); name.isNotEmpty() && local.existsAsFile())
        return local;
    return {};
}

juce::StringArray PresetLibrary::getTone3000ModelIds() const
{
    juce::StringArray ids;
    for (const auto& p : presets)
        if (p.amp.source == ToneRef::Source::tone3000 && p.amp.modelId.isNotEmpty())
            ids.addIfNotAlreadyThere (p.amp.modelId);
    return ids;
}

} // namespace pedal

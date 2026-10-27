#include "presets/Preset.h"

namespace pedal
{
namespace
{
constexpr auto presetFormat = "pedal-app-preset";
constexpr auto setlistFormat = "pedal-app-setlist";
constexpr int currentVersion = 1;

// Checks the "format" and "version" fields shared by presets and setlists.
bool checkHeader (const juce::var& v, const char* format, juce::String& error)
{
    if (! v.isObject())
    {
        error = juce::String::fromUTF8 ("Arquivo não é JSON válido");
        return false;
    }
    if (v.getProperty ("format", {}).toString() != format)
    {
        error = juce::String::fromUTF8 ("Formato desconhecido");
        return false;
    }
    if ((int) v.getProperty ("version", 0) > currentVersion)
    {
        error = juce::String::fromUTF8 ("Feito por uma versão mais nova do app");
        return false;
    }
    return true;
}

juce::DynamicObject* makeHeader (const char* format, const juce::String& id, const juce::String& name)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("format", format);
    obj->setProperty ("version", currentVersion);
    obj->setProperty ("id", id);
    obj->setProperty ("name", name);
    return obj;
}

std::optional<juce::var> parse (const juce::String& json, juce::String& error)
{
    juce::var v;
    const auto result = juce::JSON::parse (json, v);
    if (result.failed())
    {
        error = juce::String::fromUTF8 ("JSON inválido: ") + result.getErrorMessage();
        return std::nullopt;
    }
    return v;
}
} // namespace

juce::String createPresetId()
{
    return juce::Uuid().toDashedString();
}

juce::var Preset::toVar() const
{
    auto* obj = makeHeader (presetFormat, id, name);
    if (category.isNotEmpty())
        obj->setProperty ("category", category);
    obj->setProperty ("favourite", favourite);

    auto* paramObj = new juce::DynamicObject();
    for (const auto& [paramId, value] : params)
        paramObj->setProperty (juce::Identifier (paramId), value);
    obj->setProperty ("params", juce::var (paramObj));

    obj->setProperty ("amp", amp.toVar());
    obj->setProperty ("ir", irPath);
    return juce::var (obj);
}

juce::String Preset::toJson() const
{
    return juce::JSON::toString (toVar());
}

std::optional<Preset> Preset::fromVar (const juce::var& v, juce::String& error)
{
    if (! checkHeader (v, presetFormat, error))
        return std::nullopt;

    Preset p;
    p.id = v.getProperty ("id", {}).toString();
    p.name = v.getProperty ("name", {}).toString();
    p.category = v.getProperty ("category", {}).toString();
    p.favourite = (bool) v.getProperty ("favourite", false);

    if (auto* paramObj = v.getProperty ("params", {}).getDynamicObject())
        for (const auto& prop : paramObj->getProperties())
            if (prop.value.isDouble() || prop.value.isInt() || prop.value.isInt64() || prop.value.isBool())
                p.params[prop.name.toString()] = (float) (double) prop.value;

    p.amp = ToneRef::fromVar (v.getProperty ("amp", {}));
    p.irPath = v.getProperty ("ir", {}).toString();

    if (p.id.isEmpty())
        p.id = createPresetId();
    if (p.name.isEmpty())
        p.name = "Sem nome";
    return p;
}

std::optional<Preset> Preset::fromJson (const juce::String& json, juce::String& error)
{
    if (auto v = parse (json, error))
        return fromVar (*v, error);
    return std::nullopt;
}

juce::var Setlist::toVar() const
{
    auto* obj = makeHeader (setlistFormat, id, name);
    juce::Array<juce::var> list;
    for (const auto& entry : entries)
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("preset", entry.presetId);
        if (entry.note.isNotEmpty())
            e->setProperty ("note", entry.note);
        list.add (juce::var (e));
    }
    obj->setProperty ("entries", list);
    return juce::var (obj);
}

juce::String Setlist::toJson() const
{
    return juce::JSON::toString (toVar());
}

std::optional<Setlist> Setlist::fromVar (const juce::var& v, juce::String& error)
{
    if (! checkHeader (v, setlistFormat, error))
        return std::nullopt;

    Setlist s;
    s.id = v.getProperty ("id", {}).toString();
    s.name = v.getProperty ("name", {}).toString();
    if (auto* list = v.getProperty ("entries", {}).getArray())
        for (const auto& e : *list)
        {
            const auto presetId = e.getProperty ("preset", {}).toString();
            if (presetId.isNotEmpty())
                s.entries.push_back ({ presetId, e.getProperty ("note", {}).toString() });
        }

    if (s.id.isEmpty())
        s.id = createPresetId();
    if (s.name.isEmpty())
        s.name = "Setlist";
    return s;
}

std::optional<Setlist> Setlist::fromJson (const juce::String& json, juce::String& error)
{
    if (auto v = parse (json, error))
        return fromVar (*v, error);
    return std::nullopt;
}

} // namespace pedal

#include "presets/ToneRef.h"

namespace pedal
{

ToneRef ToneRef::fromFile (const juce::File& namFile)
{
    ToneRef ref;
    ref.source = Source::localFile;
    ref.localPath = namFile.getFullPathName();
    ref.displayName = namFile.getFileNameWithoutExtension();
    return ref;
}

ToneRef ToneRef::fromTone3000 (const juce::String& toneId, const juce::String& modelId,
                               const juce::String& displayName, const juce::String& creator)
{
    ToneRef ref;
    ref.source = Source::tone3000;
    ref.toneId = toneId;
    ref.modelId = modelId;
    ref.displayName = displayName;
    ref.creator = creator;
    return ref;
}

juce::String ToneRef::getKey() const
{
    switch (source)
    {
        case Source::localFile: return "file:" + localPath;
        case Source::tone3000:  return "t3k:" + toneId + "/" + modelId;
        case Source::none:      break;
    }
    return {};
}

juce::String ToneRef::getName() const
{
    if (displayName.isNotEmpty())
        return displayName;
    if (source == Source::localFile)
        return juce::File (localPath).getFileNameWithoutExtension();
    return {};
}

juce::var ToneRef::toVar() const
{
    auto* obj = new juce::DynamicObject();
    switch (source)
    {
        case Source::localFile:
            obj->setProperty ("source", "file");
            obj->setProperty ("path", localPath);
            break;
        case Source::tone3000:
            obj->setProperty ("source", "tone3000");
            obj->setProperty ("toneId", toneId);
            obj->setProperty ("modelId", modelId);
            break;
        case Source::none:
            obj->setProperty ("source", "none");
            break;
    }
    if (displayName.isNotEmpty())
        obj->setProperty ("name", displayName);
    if (creator.isNotEmpty())
        obj->setProperty ("creator", creator);
    return juce::var (obj);
}

ToneRef ToneRef::fromVar (const juce::var& v)
{
    ToneRef ref;
    const auto source = v.getProperty ("source", {}).toString();
    if (source == "file")
    {
        ref.source = Source::localFile;
        ref.localPath = v.getProperty ("path", {}).toString();
    }
    else if (source == "tone3000")
    {
        ref.source = Source::tone3000;
        ref.toneId = v.getProperty ("toneId", {}).toString();
        ref.modelId = v.getProperty ("modelId", {}).toString();
    }
    ref.displayName = v.getProperty ("name", {}).toString();
    ref.creator = v.getProperty ("creator", {}).toString();

    // A ref with nothing to load is the same as no amp.
    if ((ref.source == Source::localFile && ref.localPath.isEmpty())
        || (ref.source == Source::tone3000 && ref.toneId.isEmpty() && ref.modelId.isEmpty()))
        ref = {};
    return ref;
}

} // namespace pedal

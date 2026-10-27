#include "presets/PresetManager.h"

namespace pedal
{
namespace
{
constexpr float paramTolerance = 1.0e-3f;

// Every parameter's value as the preset should store it: real units.
std::map<juce::String, float> readParameters (juce::AudioProcessorValueTreeState& apvts)
{
    std::map<juce::String, float> values;
    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            values[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
    return values;
}

float presetValueOrDefault (const Preset& preset, const juce::RangedAudioParameter& param)
{
    const auto it = preset.params.find (param.getParameterID());
    return it != preset.params.end() ? it->second : param.convertFrom0to1 (param.getDefaultValue());
}
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& apvts, SignalChain& signalChain,
                              const juce::File& libraryFolder)
    : parameters (apvts), chain (signalChain), library (libraryFolder), bank (signalChain.getAmp())
{
    rebuildList();
    updatePreload();
    startTimerHz (30);
}

PresetManager::~PresetManager()
{
    *alive = false;
    stopTimer();
}

//==============================================================================
int PresetManager::getNumPresets() const
{
    return (int) listIds.size();
}

const Preset* PresetManager::getPreset (int index) const
{
    if (! juce::isPositiveAndBelow (index, (int) listIds.size()))
        return nullptr;
    return library.findPreset (listIds[(size_t) index]);
}

const Preset* PresetManager::getCurrentPreset() const
{
    return library.findPreset (currentPresetId);
}

ui::PresetInfo PresetManager::getPresetInfo (int index) const
{
    ui::PresetInfo info;
    if (const auto* p = getPreset (index))
    {
        info.name = p->name;
        info.ampName = p->amp.getName();
        info.category = p->category;
        info.favourite = p->favourite;
    }
    return info;
}

juce::String PresetManager::getListName() const
{
    if (const auto* s = getActiveSetlist())
        return s->name;
    return "Todos";
}

void PresetManager::selectPreset (int index)
{
    const auto* p = getPreset (index);
    if (p == nullptr)
        return;

    currentIndex = index;
    currentPresetId = p->id;
    applyPreset (*p);
    lastModified = false;
    sendChangeMessage();
}

void PresetManager::next()
{
    if (currentIndex + 1 < getNumPresets())
        selectPreset (currentIndex + 1);
}

void PresetManager::previous()
{
    if (currentIndex > 0)
        selectPreset (currentIndex - 1);
}

void PresetManager::applyPreset (const Preset& preset)
{
    lastError = {};

    for (auto* p : parameters.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const float target = ranged->convertTo0to1 (presetValueOrDefault (preset, *ranged));
            if (std::abs (ranged->getValue() - target) > 1.0e-6f)
                ranged->setValueNotifyingHost (target);
        }

    // Same capture as now: keep it playing instead of crossfading into itself.
    const bool ampMissing = ! preset.amp.isEmpty() && ! chain.getAmp().hasModel();
    if (preset.amp != currentTone || ampMissing)
        loadTone (preset.amp);

    if (preset.irPath.isEmpty())
        clearIr();
    else if (const auto ir = library.findIr (preset.irPath); ir.existsAsFile())
    {
        if (ir != currentIr || ! chain.hasCabinet())
            loadIr (ir);
    }
    else
    {
        clearIr();
        setError (juce::String::fromUTF8 ("IR não encontrado: ") + preset.irPath);
    }

    rememberLoaded (preset);
}

void PresetManager::rememberLoaded (const Preset& preset)
{
    // Store the IR as it resolves here, so a preset whose IR was found in the
    // IRs folder (or is missing) does not show as modified straight away.
    loadedSnapshot = preset;
    const auto ir = library.findIr (preset.irPath);
    loadedSnapshot->irPath = ir != juce::File() ? ir.getFullPathName() : juce::String();
}

//==============================================================================
Preset PresetManager::captureCurrent() const
{
    Preset p;
    if (const auto* current = getCurrentPreset())
    {
        p.id = current->id;
        p.name = current->name;
        p.category = current->category;
        p.favourite = current->favourite;
    }
    p.params = readParameters (parameters);
    p.amp = currentTone;
    p.irPath = currentIr != juce::File() ? currentIr.getFullPathName() : juce::String();
    return p;
}

void PresetManager::saveCurrent()
{
    if (getCurrentPreset() == nullptr)
        return setError (juce::String::fromUTF8 ("Nenhum preset escolhido para salvar"));

    auto p = captureCurrent();
    if (auto error = library.savePreset (p); error.isNotEmpty())
        return setError (error);

    loadedSnapshot = p;
    lastError = {};
    libraryChanged();
}

void PresetManager::saveCurrentAs (const juce::String& name)
{
    auto p = captureCurrent();
    p.id = {};
    p.name = name;
    p.favourite = false;
    if (auto error = library.savePreset (p); error.isNotEmpty())
        return setError (error);

    // Saving during a setlist puts the new preset right after the current song.
    if (const auto* active = library.findSetlist (activeSetlistId))
    {
        auto setlist = *active;
        auto& entries = setlist.entries;
        const auto pos = juce::isPositiveAndBelow (currentIndex, (int) entries.size())
                             ? entries.begin() + currentIndex + 1 : entries.end();
        const auto inserted = entries.insert (pos, Setlist::Entry { p.id, {} });
        currentIndex = (int) std::distance (entries.begin(), inserted);
        if (auto error = library.saveSetlist (setlist); error.isNotEmpty())
            setError (error);
    }

    currentPresetId = p.id;
    loadedSnapshot = p;
    libraryChanged();

    if (activeSetlistId.isEmpty())
        currentIndex = (int) std::distance (listIds.begin(), std::find (listIds.begin(), listIds.end(), p.id));
}

void PresetManager::setFavourite (int index, bool favourite)
{
    const auto* p = getPreset (index);
    if (p == nullptr)
        return;
    if (auto error = library.setFavourite (p->id, favourite); error.isNotEmpty())
        return setError (error);
    if (loadedSnapshot && loadedSnapshot->id == p->id)
        loadedSnapshot->favourite = favourite;
    libraryChanged();
}

bool PresetManager::isModified() const
{
    if (! loadedSnapshot)
        return false;

    for (auto* p : parameters.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (std::abs (ranged->convertFrom0to1 (ranged->getValue()) - presetValueOrDefault (*loadedSnapshot, *ranged))
                > paramTolerance * std::max (1.0f, ranged->getNormalisableRange().getRange().getLength()))
                return true;

    if (loadedSnapshot->amp != currentTone)
        return true;
    return loadedSnapshot->irPath != (currentIr != juce::File() ? currentIr.getFullPathName() : juce::String());
}

//==============================================================================
void PresetManager::setActiveSetlist (const juce::String& setlistId)
{
    activeSetlistId = library.findSetlist (setlistId) != nullptr ? setlistId : juce::String();
    libraryChanged();
}

const Setlist* PresetManager::getActiveSetlist() const
{
    return library.findSetlist (activeSetlistId);
}

void PresetManager::libraryChanged()
{
    rebuildList();
    updatePreload();

    if (offlineCacheHandler)
        offlineCacheHandler (library.getTone3000ModelIds());

    sendChangeMessage();
}

void PresetManager::rebuildList()
{
    listIds.clear();
    if (const auto* setlist = library.findSetlist (activeSetlistId))
    {
        for (const auto* p : library.resolve (*setlist))
            listIds.push_back (p->id);
    }
    else
    {
        activeSetlistId = {};
        for (const auto& p : library.getPresets())
            listIds.push_back (p.id);
    }

    // Keep the same row when it still holds the current preset; otherwise
    // follow the preset to wherever it is now.
    if (juce::isPositiveAndBelow (currentIndex, (int) listIds.size()) && listIds[(size_t) currentIndex] == currentPresetId)
        return;
    const auto it = std::find (listIds.begin(), listIds.end(), currentPresetId);
    currentIndex = it != listIds.end() ? (int) std::distance (listIds.begin(), it) : -1;
}

void PresetManager::updatePreload()
{
    std::vector<ToneRef> tones;
    auto add = [&tones] (const ToneRef& t)
    {
        if (! t.isEmpty() && (int) tones.size() < maxPreloadedTones
            && std::find (tones.begin(), tones.end(), t) == tones.end())
            tones.push_back (t);
    };

    // The current tone first so it is never the one left out by the limit.
    add (currentTone);
    if (const auto* setlist = getActiveSetlist())
        for (const auto* p : library.resolve (*setlist))
            add (p->amp);
    else
        for (const auto* p : library.getFavourites())
            add (p->amp);

    bank.preload (tones);
}

//==============================================================================
void PresetManager::setToneFetcher (ToneFetcher fetcher)
{
    bank.setToneFetcher (std::move (fetcher));
    updatePreload(); // tone3000 tones that failed without a fetcher get another try
}

void PresetManager::setOfflineCacheHandler (std::function<void (const juce::StringArray&)> handler)
{
    offlineCacheHandler = std::move (handler);
}

void PresetManager::loadTone (const ToneRef& tone, std::function<void (juce::String)> onDone)
{
    currentTone = tone;
    bank.select (tone, [this, onDone, aliveFlag = alive] (juce::String error)
    {
        if (! aliveFlag->load())
            return;
        if (error.isNotEmpty())
            setError (error);
        else
            sendChangeMessage();
        if (onDone)
            onDone (error);
    });
}

void PresetManager::loadIr (const juce::File& file)
{
    currentIr = file;
    chain.loadCabinet (file);
}

void PresetManager::clearIr()
{
    currentIr = juce::File();
    chain.clearCabinet();
}

void PresetManager::setError (const juce::String& error)
{
    lastError = error;
    sendChangeMessage();
}

//==============================================================================
void PresetManager::handleMidi (const juce::MidiBuffer& midi) noexcept
{
    // Raw bytes only: building a juce::MidiMessage can allocate for SysEx.
    for (const auto metadata : midi)
    {
        if (metadata.numBytes < 2 || (metadata.data[0] & 0xf0) != 0xc0)
            continue;
        const int channel = (metadata.data[0] & 0x0f) + 1;
        const int wanted = midiChannel.load (std::memory_order_relaxed);
        if (wanted == 0 || wanted == channel)
            pendingProgram.store (metadata.data[1] & 0x7f, std::memory_order_relaxed);
    }
}

void PresetManager::timerCallback()
{
    if (const int program = pendingProgram.exchange (-1); program >= 0)
        selectPreset (program);

    // Let the screen follow knob changes and preload progress.
    const bool modified = isModified();
    const auto status = bank.getStatus();
    if (modified != lastModified || status.ready != lastStatus.ready || status.failed != lastStatus.failed
        || status.total != lastStatus.total)
    {
        lastModified = modified;
        lastStatus = status;
        sendChangeMessage();
    }
}

//==============================================================================
juce::ValueTree PresetManager::getState() const
{
    juce::ValueTree state ("Presets");
    state.setProperty ("presetId", currentPresetId, nullptr);
    state.setProperty ("index", currentIndex, nullptr);
    state.setProperty ("setlistId", activeSetlistId, nullptr);
    state.setProperty ("tone", juce::JSON::toString (currentTone.toVar(), true), nullptr);
    state.setProperty ("ir", currentIr != juce::File() ? currentIr.getFullPathName() : juce::String(), nullptr);
    state.setProperty ("midiChannel", midiChannel.load(), nullptr);
    return state;
}

void PresetManager::setState (const juce::ValueTree& state)
{
    library.rescan();
    activeSetlistId = state.getProperty ("setlistId").toString();
    currentPresetId = state.getProperty ("presetId").toString();
    currentIndex = state.getProperty ("index", -1);
    setMidiChannel (state.getProperty ("midiChannel", 0));

    if (const auto* p = getCurrentPreset())
        rememberLoaded (*p);
    else
        loadedSnapshot.reset();

    loadTone (ToneRef::fromVar (juce::JSON::parse (state.getProperty ("tone").toString())));

    if (const auto ir = library.findIr (state.getProperty ("ir").toString()); ir.existsAsFile())
        loadIr (ir);
    else
        clearIr();

    lastModified = isModified();
    libraryChanged();
}

} // namespace pedal

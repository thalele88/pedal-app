// Checks the preset system without a screen: JSON files, the library folder,
// setlists, preloading, MIDI Program Change and the plugin state.
// Exit code 0 = pass.

#include "presets/PresetManager.h"

#include <chrono>
#include <cstdio>

namespace
{
int failures = 0;

void check (bool condition, const juce::String& what)
{
    std::printf ("%s %s\n", condition ? "[ ok ]" : "[FAIL]", what.toRawUTF8());
    if (! condition)
        ++failures;
}

// Runs the message loop (timers, async callbacks) until `done` or the timeout.
template <typename Fn>
bool pumpUntil (Fn done, int timeoutMs = 10000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! done())
    {
        if (juce::Time::getMillisecondCounter() > end)
            return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
    return true;
}

// Minimal processor with a few parameters shaped like the real ones.
class TestProcessor : public juce::AudioProcessor
{
public:
    TestProcessor() : parameters (*this, nullptr, "PedalState", createLayout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "inputGain", 1 }, "Entrada", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "drop", 1 }, "Drop", -12, 12, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "cabEnabled", 1 }, "Caixa", true));
        return layout;
    }

    float get (const char* id) { return parameters.getRawParameterValue (id)->load(); }
    void set (const char* id, float value)
    {
        auto* p = parameters.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    const juce::String getName() const override { return "Test"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState parameters;
};

// Plays a few blocks so the "audio thread" picks up the model that was installed.
bool playBlocks (pedal::SignalChain& chain, int numBlocks = 4)
{
    juce::AudioBuffer<float> buffer (1, 128);
    bool finite = true;
    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, 0.1f * std::sin (0.05f * (float) i));
        chain.process (buffer, {});
        chain.getAmp().collectGarbage();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            finite = finite && std::isfinite (buffer.getSample (0, i));
    }
    return finite;
}

void testJson()
{
    pedal::Preset p;
    p.id = "abc";
    p.name = juce::String::fromUTF8 ("Pesado Afinação C");
    p.category = "Guitarra";
    p.favourite = true;
    p.params = { { "inputGain", 3.5f }, { "drop", -2.0f } };
    p.amp = pedal::ToneRef::fromTone3000 ("123", "456", "Mesa", "fulano");
    p.irPath = "/tmp/cab.wav";

    juce::String error;
    const auto back = pedal::Preset::fromJson (p.toJson(), error);
    check (back.has_value(), "preset JSON round trip " + error);
    if (back)
    {
        check (back->id == p.id && back->name == p.name && back->category == p.category && back->favourite,
               "preset fields survive");
        check (back->params == p.params, "preset params survive");
        check (back->amp == p.amp && back->amp.creator == "fulano" && back->amp.displayName == "Mesa",
               "ToneRef survives");
        check (back->irPath == p.irPath, "IR path survives");
    }

    check (! pedal::Preset::fromJson ("{ not json", error), "broken JSON is rejected");
    check (! pedal::Preset::fromJson (R"({"format":"other"})", error), "other formats are rejected");
    check (! pedal::Preset::fromJson (R"({"format":"pedal-app-preset","version":99})", error),
           "newer versions are rejected");

    const auto minimal = pedal::Preset::fromJson (R"({"format":"pedal-app-preset","version":1})", error);
    check (minimal && minimal->id.isNotEmpty() && minimal->amp.isEmpty(), "minimal preset gets an id and no amp");

    pedal::Setlist s;
    s.id = "set";
    s.name = "Show";
    s.entries = { { "a", "Abertura" }, { "b", {} }, { "a", "Bis" } };
    const auto sBack = pedal::Setlist::fromJson (s.toJson(), error);
    check (sBack && sBack->entries.size() == 3 && sBack->entries[2].presetId == "a" && sBack->entries[0].note == "Abertura",
           "setlist round trip keeps order and repeats");
}

void testLibrary (const juce::File& root)
{
    pedal::PresetLibrary lib (root);
    check (lib.getPresets().empty(), "empty folder = empty library");

    pedal::Preset a, b;
    a.name = "Limpo";
    b.name = "Limpo";
    check (lib.savePreset (a).isEmpty() && lib.savePreset (b).isEmpty(), "save two presets with the same name");
    check (a.id.isNotEmpty() && a.id != b.id, "presets get different ids");
    check (lib.getPresetFolder().findChildFiles (juce::File::findFiles, false, "*.json").size() == 2,
           "same name does not overwrite the other file");

    a.name = "Drive";
    lib.savePreset (a);
    check (lib.getPresetFolder().getChildFile ("Drive.json").existsAsFile()
               && lib.getPresetFolder().findChildFiles (juce::File::findFiles, false, "*.json").size() == 2,
           "rename moves the file");

    lib.getPresetFolder().getChildFile ("quebrado.json").replaceWithText ("{ oops");
    lib.setFavourite (b.id, true);

    pedal::Setlist s;
    s.name = "Show";
    s.entries = { { b.id, {} }, { "deleted", {} }, { a.id, {} } };
    check (lib.saveSetlist (s).isEmpty(), "save setlist");

    pedal::PresetLibrary again (root);
    check (again.getPresets().size() == 2 && again.getPresets()[0].name == "Drive", "rescan finds presets sorted by name");
    check (again.getScanErrors().size() == 1, "broken file is reported, not fatal");
    check (again.getFavourites().size() == 1 && again.getFavourites()[0]->id == b.id, "favourite is saved");
    check (again.getSetlists().size() == 1 && again.resolve (again.getSetlists()[0]).size() == 2,
           "setlist skips deleted presets");

    again.getIrFolder().createDirectory();
    again.getIrFolder().getChildFile ("cab.wav").replaceWithText ("x");
    check (again.findIr ("C:\\Users\\thales\\IRs\\cab.wav") == again.getIrFolder().getChildFile ("cab.wav"),
           "IR from another computer is found in the IRs folder");

    check (again.removePreset (a.id).isEmpty() && again.getPresets().size() == 1, "remove preset");
    lib.getPresetFolder().getChildFile ("quebrado.json").deleteFile();
}

void testManager (const juce::File& root)
{
    const juce::File models (PEDAL_EXAMPLE_MODELS_DIR);
    // A copy with another name, so it is a different tone from A2.
    const auto other = root.getChildFile ("Other.nam");
    models.getChildFile ("A2.nam").copyFileTo (other);

    TestProcessor proc;
    pedal::SignalChain chain;
    chain.prepare (48000.0, 128);
    pedal::PresetManager manager (proc.parameters, chain, root);

    // Presets: B (wavenet, clean), A (A2, drop -2), C (no amp), D (Other, not in the setlist).
    auto& lib = manager.getLibrary();
    pedal::Preset a, b, c, d;
    a.name = "A";
    a.params = { { "inputGain", 3.0f }, { "drop", -2.0f } };
    a.amp = pedal::ToneRef::fromFile (models.getChildFile ("A2.nam"));
    b.name = "B";
    b.params = { { "inputGain", -6.0f } };
    b.amp = pedal::ToneRef::fromFile (models.getChildFile ("wavenet.nam"));
    c.name = "C";
    d.name = "D";
    d.amp = pedal::ToneRef::fromFile (other);
    for (auto* p : { &a, &b, &c, &d })
        lib.savePreset (*p);

    pedal::Setlist show;
    show.name = "Show";
    show.entries = { { b.id, {} }, { a.id, {} }, { c.id, {} }, { a.id, "Bis" } };
    lib.saveSetlist (show);
    manager.libraryChanged();
    check (manager.getNumPresets() == 4 && manager.getListName() == "Todos", "whole library is listed");

    manager.setActiveSetlist (show.id);
    check (manager.getNumPresets() == 4 && manager.getListName() == "Show"
               && manager.getPresetInfo (1).name == "A" && manager.getPresetInfo (1).ampName == "A2",
           "setlist order is the list");

    check (pumpUntil ([&] { return manager.getPreloadStatus().ready == 2; }), "both setlist captures preload");
    check (pumpUntil ([&] { return ! manager.getPreloadStatus().busy; }), "preload finishes");

    auto start = std::chrono::steady_clock::now();
    manager.selectPreset (1);
    auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    std::printf ("preloaded preset switch took %.2f ms\n", ms);
    check (chain.getAmp().getModelName() == "A2", "preloaded capture is installed at once");
    check (ms < 20.0, "preloaded switch is fast");
    check (proc.get ("inputGain") == 3.0f && proc.get ("drop") == -2.0f && proc.get ("cabEnabled") > 0.5f,
           "knobs follow the preset, missing ones go to default");
    check (playBlocks (chain), "audio is fine after switch");
    check (manager.getCurrentIndex() == 1 && ! manager.isModified(), "current preset, not modified");

    proc.set ("inputGain", 10.0f);
    check (manager.isModified(), "moving a knob marks it modified");

    // MIDI Program Change 1 on channel 1 -> list entry 0 (B).
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::programChange (1, 0), 0);
    manager.handleMidi (midi);
    check (pumpUntil ([&] { return manager.getCurrentIndex() == 0; }, 2000), "Program Change selects the preset");
    check (chain.getAmp().getModelName() == "wavenet" && proc.get ("inputGain") == -6.0f && proc.get ("drop") == 0.0f,
           "Program Change loads amp and knobs");

    manager.setMidiChannel (2);
    midi.clear();
    midi.addEvent (juce::MidiMessage::programChange (1, 1), 0);
    manager.handleMidi (midi);
    pumpUntil ([] { return false; }, 200);
    check (manager.getCurrentIndex() == 0, "Program Change on another channel is ignored");
    manager.setMidiChannel (0);

    manager.selectPreset (2);
    check (! chain.getAmp().hasModel() && playBlocks (chain), "preset without amp unloads the capture");

    pumpUntil ([] { return false; }, 300); // let the used spare rebuild
    manager.next();
    check (manager.getCurrentIndex() == 3 && chain.getAmp().getModelName() == "A2", "next() goes to the repeated preset");
    manager.next();
    check (manager.getCurrentIndex() == 3, "next() stops at the end");

    // A slow load must not override a later instant one.
    manager.loadTone (d.amp);
    manager.selectPreset (0);
    pumpUntil ([] { return false; }, 1000);
    playBlocks (chain);
    check (chain.getAmp().getModelName() == "wavenet", "last selection wins");

    manager.saveCurrentAs ("B2");
    check (manager.getNumPresets() == 5 && manager.getPresetInfo (1).name == "B2" && manager.getCurrentIndex() == 1,
           "save as during a setlist inserts after the current song");

    manager.setFavourite (1, true);
    check (manager.getPresetInfo (1).favourite, "favourite from the list");

    // Plugin state round trip into a fresh manager (like reopening the DAW project).
    const auto state = manager.getState();
    pedal::SignalChain chain2;
    chain2.prepare (48000.0, 128);
    TestProcessor proc2;
    pedal::PresetManager manager2 (proc2.parameters, chain2, root);
    proc2.set ("inputGain", -6.0f);
    manager2.setState (state);
    check (manager2.getActiveSetlist() != nullptr && manager2.getCurrentIndex() == 1
               && manager2.getCurrentPreset()->name == "B2",
           "state restores setlist and preset");
    check (pumpUntil ([&] { return chain2.getAmp().getModelName() == "wavenet"; }, 5000), "state restores the amp");
    check (! manager2.isModified(), "restored preset is not modified");

    // TONE3000 refs go through the fetcher.
    juce::StringArray cached;
    manager.setOfflineCacheHandler ([&cached] (const juce::StringArray& ids) { cached = ids; });
    manager.setToneFetcher ([models] (const pedal::ToneRef& ref, juce::String& json) {
        if (ref.modelId != "m1")
            return juce::String ("sem modelo");
        json = models.getChildFile ("wavenet.nam").loadFileAsString();
        return juce::String();
    });
    pedal::Preset t;
    t.name = "TONE3000";
    t.amp = pedal::ToneRef::fromTone3000 ("t1", "m1", "Captura online", "alguem");
    lib.savePreset (t);
    manager.setActiveSetlist ({});
    check (cached.contains ("m1"), "offline cache keeps preset tones");
    const auto idx = [&] { for (int i = 0; i < manager.getNumPresets(); ++i) if (manager.getPresetInfo (i).name == "TONE3000") return i; return -1; }();
    manager.selectPreset (idx);
    check (pumpUntil ([&] { return chain.getAmp().getModelName() == "Captura online"; }, 5000), "TONE3000 tone loads through the fetcher");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("PresetTest-" + juce::Uuid().toString());

    testJson();
    testLibrary (root.getChildFile ("lib"));
    testManager (root.getChildFile ("manager"));

    root.deleteRecursively();
    return failures == 0 ? 0 : 1;
}

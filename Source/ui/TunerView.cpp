#include "ui/TunerView.h"

namespace pedal::ui
{

namespace
{
constexpr float inTuneCents = 3.0f;
constexpr float minRms = 0.002f; // about -54 dBFS: quieter than this is not a note

juce::Colour colourForCents (float cents)
{
    const float a = std::abs (cents);
    return a <= inTuneCents ? Colours::ok : (a <= 15.0f ? Colours::warn : Colours::accent);
}
} // namespace

TunerView::TunerView (PedalAudioProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);

    muteButton.setClickingTogglesState (true);
    muteButton.setToggleState (true, juce::dontSendNotification);
    muteButton.onClick = [this] { processor.setOutputMuted (isShowing() && muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
}

TunerView::~TunerView()
{
    // Closing the plugin window with the tuner open must not leave the output muted.
    processor.getTuner().setActive (false);
    processor.setOutputMuted (false);
}

void TunerView::visibilityChanged()
{
    auto& tuner = processor.getTuner();
    if (isVisible())
    {
        // Drop whatever was left from the last time the tuner was open.
        while (tuner.pull (incoming.data(), (int) incoming.size()) > 0) {}
        history.fill (0.0f);
        reading = {};
        framesWithoutPitch = 0;
        recentCount = 0;
        tuner.setActive (true);
        processor.setOutputMuted (muteButton.getToggleState());
        startTimerHz (30);
        grabKeyboardFocus();
    }
    else
    {
        stopTimer();
        tuner.setActive (false);
        processor.setOutputMuted (false);
    }
}

bool TunerView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')
    {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}

void TunerView::timerCallback()
{
    auto& tuner = processor.getTuner();
    for (;;)
    {
        const int n = tuner.pull (incoming.data(), (int) incoming.size());
        if (n <= 0)
            break;
        std::move (history.begin() + n, history.end(), history.begin());
        std::copy (incoming.begin(), incoming.begin() + n, history.end() - n);
    }

    analyse();
    repaint();
}

void TunerView::analyse()
{
    float sumSquares = 0.0f;
    for (float v : history)
        sumSquares += v * v;
    const float rms = std::sqrt (sumSquares / (float) historySize);

    float hz = 0.0f;
    if (rms > minRms)
        hz = PitchDetector::detect (history.data(), historySize, processor.getTuner().getOutputSampleRate(), work.data());

    if (hz < 25.0f || hz > 1500.0f)
    {
        // Keep the last note on screen for a moment so it does not flicker.
        if (++framesWithoutPitch > 10)
        {
            reading = {};
            recentCount = 0;
        }
        return;
    }

    framesWithoutPitch = 0;

    // Median of the last few estimates removes the odd octave jump.
    std::move (recentFreqs + 1, recentFreqs + 5, recentFreqs);
    recentFreqs[4] = hz;
    recentCount = juce::jmin (5, recentCount + 1);
    float sorted[5];
    std::copy (recentFreqs + 5 - recentCount, recentFreqs + 5, sorted);
    std::sort (sorted, sorted + recentCount);
    const auto next = NoteReading::fromFrequency (sorted[recentCount / 2]);

    if (next.midiNote != reading.midiNote)
        smoothedCents = next.cents;
    else
        smoothedCents += 0.35f * (next.cents - smoothedCents);
    reading = next;
}

void TunerView::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (60);
    area.removeFromBottom (60);

    const bool valid = reading.isValid();
    const auto colour = valid ? colourForCents (smoothedCents) : Colours::textDim;

    // Note name
    auto noteArea = area.removeFromTop (area.getHeight() * 6 / 10);
    const float noteHeight = juce::jmin ((float) noteArea.getHeight() * 0.9f, (float) noteArea.getWidth() * 0.35f);
    g.setColour (colour);
    g.setFont (juce::FontOptions (noteHeight, juce::Font::bold));
    g.drawText (valid ? reading.getName() : juce::String ("-"), noteArea, juce::Justification::centred);

    if (valid)
    {
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (noteHeight * 0.25f));
        g.drawText (juce::String (reading.getOctave()), noteArea.withTrimmedLeft (noteArea.getWidth() / 2 + (int) (noteHeight * 0.45f)),
                    juce::Justification::centredLeft);
    }

    // Cents meter: ticks every 10 cents, a marker for the current offset.
    auto meter = area.removeFromTop (juce::jmin (90, area.getHeight() / 2)).reduced (juce::jmax (0, area.getWidth() / 10), 10).toFloat();
    const float centreX = meter.getCentreX();
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = centreX + (float) c / 50.0f * meter.getWidth() * 0.5f;
        const float h = c == 0 ? meter.getHeight() : meter.getHeight() * 0.45f;
        g.setColour (c == 0 ? Colours::text : Colours::textDim.withAlpha (0.5f));
        g.fillRect (x - 1.5f, meter.getCentreY() - h * 0.5f, 3.0f, h);
    }

    if (valid)
    {
        const float x = centreX + juce::jlimit (-50.0f, 50.0f, smoothedCents) / 50.0f * meter.getWidth() * 0.5f;
        g.setColour (colour);
        g.fillRoundedRectangle (x - 7.0f, meter.getY() - 6.0f, 14.0f, meter.getHeight() + 12.0f, 7.0f);
    }

    // Flat / sharp arrows, lit on the side the string has to move away from.
    const float arrow = juce::jmin (36.0f, meter.getHeight() * 0.5f);
    auto drawArrow = [&] (float x, bool pointsRight, bool lit)
    {
        juce::Path p;
        const float y = meter.getCentreY();
        if (pointsRight) p.addTriangle (x, y - arrow * 0.5f, x, y + arrow * 0.5f, x + arrow * 0.8f, y);
        else             p.addTriangle (x, y - arrow * 0.5f, x, y + arrow * 0.5f, x - arrow * 0.8f, y);
        g.setColour (lit ? colour : Colours::panelRaised);
        g.fillPath (p);
    };
    drawArrow (meter.getX() - 24.0f - arrow * 0.8f, true, valid && smoothedCents < -inTuneCents);
    drawArrow (meter.getRight() + 24.0f + arrow * 0.8f, false, valid && smoothedCents > inTuneCents);

    // Details
    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (20.0f));
    juce::String detail = valid ? juce::String (reading.frequency, 1) + " Hz   " + (smoothedCents >= 0.0f ? "+" : "")
                                      + juce::String (juce::roundToInt (smoothedCents)) + " cents"
                                : tr ("Toque uma corda");
    g.drawText (detail, area.removeFromTop (40), juce::Justification::centred);

    const int drop = juce::roundToInt (getParameter (processor.parameters, ParamIDs::drop));
    if (drop != 0)
    {
        g.setFont (juce::FontOptions (16.0f));
        g.drawText (tr ("Afine normal: o drop de ") + juce::String (drop) + tr (" semitons é aplicado depois do afinador."),
                    area.removeFromTop (30), juce::Justification::centred);
    }

    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("AFINADOR", getLocalBounds().reduced (24).removeFromTop (48), juce::Justification::centredLeft);
}

void TunerView::resized()
{
    auto top = getLocalBounds().reduced (24).removeFromTop (48);
    closeButton.setBounds (top.removeFromRight (140));
    top.removeFromRight (12);
    muteButton.setBounds (top.removeFromRight (220));
}

} // namespace pedal::ui

#include "ui/StageView.h"

namespace
{
constexpr int dropShortcuts[] = { 0, -1, -2, -3, -4, -5, -7, -12 };

juce::String dropName (float semitones)
{
    const int st = juce::roundToInt (semitones);
    return st == 0 ? pedal::ui::tr ("Padrão") : (st > 0 ? "+" : "") + juce::String (st);
}
} // namespace

namespace pedal::ui
{

class StageView::Slot : public juce::Component
{
public:
    Slot (StageView& ownerView, int slotIndex) : owner (ownerView), slot (slotIndex) {}

    int getPresetIndex() const { return owner.bank * owner.slotsPerBank + slot; }

    void paint (juce::Graphics& g) override
    {
        const int index = getPresetIndex();
        const bool exists = index < owner.presets.getNumPresets();
        const bool isCurrent = exists && index == owner.presets.getCurrentIndex();
        auto r = getLocalBounds().toFloat().reduced (5.0f);

        g.setColour (isCurrent ? Colours::accent : (exists ? Colours::panelRaised : Colours::panel));
        g.fillRoundedRectangle (r, 14.0f);
        if (isMouseButtonDown() && exists)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (r, 14.0f);
        }

        const auto textColour = isCurrent ? juce::Colours::black : Colours::text;
        auto content = getLocalBounds().reduced (18, 14);

        g.setColour (isCurrent ? juce::Colours::black.withAlpha (0.6f) : Colours::textDim);
        g.setFont (juce::FontOptions (juce::jlimit (16.0f, 30.0f, (float) getHeight() * 0.16f), juce::Font::bold));
        g.drawText (juce::String (slot + 1), content.removeFromTop ((int) (getHeight() * 0.2f)), juce::Justification::topLeft);

        if (! exists)
            return;

        const auto info = owner.presets.getPresetInfo (index);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (juce::jlimit (18.0f, 40.0f, (float) getHeight() * 0.2f), juce::Font::bold));
        g.drawFittedText (info.name, content, juce::Justification::centred, 2, 0.8f);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        owner.selectSlot (slot);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { repaint(); }

private:
    StageView& owner;
    const int slot;
};

class StageView::NavButton : public juce::Component
{
public:
    explicit NavButton (bool pointsRight) : right (pointsRight) {}
    std::function<void()> onPress;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (5.0f);
        g.setColour (isEnabled() ? Colours::panelRaised : Colours::panel);
        g.fillRoundedRectangle (r, 14.0f);

        const auto c = r.getCentre();
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.22f;
        juce::Path p;
        if (right) p.addTriangle (c.x - s * 0.6f, c.y - s, c.x - s * 0.6f, c.y + s, c.x + s * 0.8f, c.y);
        else       p.addTriangle (c.x + s * 0.6f, c.y - s, c.x + s * 0.6f, c.y + s, c.x - s * 0.8f, c.y);
        g.setColour (isEnabled() ? Colours::text : Colours::textDim.withAlpha (0.3f));
        g.fillPath (p);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onPress)
            onPress();
    }

private:
    const bool right;
};

//==============================================================================
StageView::StageView (PresetSource& source, PitchParams& pitchParams)
    : presets (source), pitch (pitchParams)
{
    setWantsKeyboardFocus (true);

    for (int i = 0; i < 8; ++i)
        addChildComponent (slots.add (new Slot (*this, i)));

    prevBank = std::make_unique<NavButton> (false);
    nextBank = std::make_unique<NavButton> (true);
    prevBank->onPress = [this] { setBank (bank - 1); };
    nextBank->onPress = [this] { setBank (bank + 1); };
    addAndMakeVisible (*prevBank);
    addAndMakeVisible (*nextBank);

    for (int st : dropShortcuts)
    {
        auto* b = dropButtons.add (new juce::TextButton (dropName (st)));
        b->onClick = [this, st] { pitch.selectShortcut (st); };
        b->setTriggeredOnMouseDown (true);
        addAndMakeVisible (b);
    }

    tunerButton.onClick = [this] { if (onTuner) onTuner(); };
    tunerButton.setTriggeredOnMouseDown (true);
    addAndMakeVisible (tunerButton);

    layoutButton.onClick = [this] { setSlotsPerBank (slotsPerBank == 4 ? 8 : 4); };
    addAndMakeVisible (layoutButton);

    exitButton.onHold = [this] { if (onExit) onExit(); };
    addAndMakeVisible (exitButton);

    presets.addChangeListener (this);
    setSlotsPerBank (4);
}

StageView::~StageView()
{
    presets.removeChangeListener (this);
}

void StageView::visibilityChanged()
{
    if (isVisible())
    {
        followCurrentPreset();
        grabKeyboardFocus();
    }
}

void StageView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    followCurrentPreset();
    repaint();
}

int StageView::getNumBanks() const
{
    return juce::jmax (1, (presets.getNumPresets() + slotsPerBank - 1) / slotsPerBank);
}

void StageView::setBank (int newBank)
{
    bank = juce::jlimit (0, getNumBanks() - 1, newBank);
    prevBank->setEnabled (bank > 0);
    nextBank->setEnabled (bank < getNumBanks() - 1);
    repaint();
}

void StageView::followCurrentPreset()
{
    const int current = presets.getCurrentIndex();
    setBank (current >= 0 ? current / slotsPerBank : bank);
}

void StageView::stepPreset (int delta)
{
    const int count = presets.getNumPresets();
    if (count == 0)
        return;
    const int current = presets.getCurrentIndex();
    const int next = current < 0 ? 0 : juce::jlimit (0, count - 1, current + delta);
    if (next != current)
        presets.selectPreset (next);
}

void StageView::selectSlot (int slot)
{
    const int index = bank * slotsPerBank + slot;
    if (index < presets.getNumPresets() && index != presets.getCurrentIndex())
        presets.selectPreset (index);
}

void StageView::setSlotsPerBank (int count)
{
    slotsPerBank = count;
    for (int i = 0; i < slots.size(); ++i)
        slots[i]->setVisible (i < slotsPerBank);
    followCurrentPreset();
    resized();
}

void StageView::refresh()
{
    const int drop = juce::roundToInt (pitch.getEffectiveSemitones());
    for (int i = 0; i < dropButtons.size(); ++i)
        dropButtons[i]->setToggleState (dropShortcuts[i] == drop, juce::dontSendNotification);

    if (drop != lastDrop)
    {
        lastDrop = drop;
        repaint (nameArea);
    }
}

bool StageView::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::pageDownKey || code == juce::KeyPress::spaceKey)
        stepPreset (1);
    else if (code == juce::KeyPress::leftKey || code == juce::KeyPress::pageUpKey)
        stepPreset (-1);
    else if (code == juce::KeyPress::upKey)
        setBank (bank - 1);
    else if (code == juce::KeyPress::downKey)
        setBank (bank + 1);
    else if (key.getTextCharacter() >= '1' && key.getTextCharacter() < '1' + slotsPerBank)
        selectSlot (key.getTextCharacter() - '1');
    else if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')
    {
        if (onTuner)
            onTuner();
    }
    else if (code == juce::KeyPress::escapeKey)
    {
        if (onExit)
            onExit();
    }
    else
        return false;

    return true;
}

void StageView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const int current = presets.getCurrentIndex();
    const auto info = presets.getPresetInfo (current);
    auto area = nameArea;

    if (presets.getNumPresets() == 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (26.0f));
        g.drawFittedText (tr ("Nenhum preset ainda.\nSalve seus sons na tela normal e volte aqui."), area,
                          juce::Justification::centred, 3);
        return;
    }

    // Number + name, as big as the area allows.
    auto top = area.removeFromTop (area.getHeight() * 7 / 10);
    const float nameHeight = juce::jmin ((float) top.getHeight() * 0.85f, 110.0f);
    if (current >= 0)
    {
        g.setColour (Colours::accent);
        g.setFont (juce::FontOptions (nameHeight * 0.6f, juce::Font::bold));
        const auto number = juce::String (current + 1).paddedLeft ('0', 2);
        g.drawText (number, top.removeFromLeft ((int) (nameHeight * 1.1f)), juce::Justification::centredLeft);
    }
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (nameHeight, juce::Font::bold));
    g.drawFittedText (current >= 0 ? info.name : tr ("Escolha um preset"), top, juce::Justification::centredLeft, 1, 0.6f);

    // Second line: capture, drop state, what comes next.
    g.setFont (juce::FontOptions (juce::jlimit (16.0f, 26.0f, (float) area.getHeight() * 0.5f)));
    g.setColour (Colours::textDim);
    juce::String line = info.ampName;
    if (lastDrop != 0)
        line << (line.isNotEmpty() ? "   |   " : "") << "Drop " << dropName (lastDrop);
    g.drawText (line, area, juce::Justification::centredLeft, true);

    if (current >= 0 && current + 1 < presets.getNumPresets())
        g.drawText (tr ("Próximo: ") + presets.getPresetInfo (current + 1).name, area, juce::Justification::centredRight, true);

    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (presets.getListName().toUpperCase() + "   |   BANCO " + juce::String (bank + 1) + "/" + juce::String (getNumBanks()),
                bankLabelArea, juce::Justification::centredLeft, true);
}

void StageView::resized()
{
    auto area = getLocalBounds().reduced (16);
    const float scale = juce::jlimit (0.7f, 1.6f, (float) getHeight() / 768.0f);

    auto header = area.removeFromTop ((int) (64 * scale));
    exitButton.setBounds (header.removeFromRight ((int) (150 * scale)).reduced (4));
    layoutButton.setBounds (header.removeFromRight ((int) (90 * scale)).reduced (4));
    tunerButton.setBounds (header.removeFromRight ((int) (200 * scale)).reduced (4));
    bankLabelArea = header.reduced (8, 0);

    nameArea = area.removeFromTop (area.getHeight() / 4).reduced (8, 4);

    auto dropRow = area.removeFromBottom ((int) (80 * scale));
    {
        const int w = dropRow.getWidth() / dropButtons.size();
        for (auto* b : dropButtons)
            b->setBounds (dropRow.removeFromLeft (w).reduced (4));
    }
    area.removeFromBottom (10);

    const int navWidth = (int) (90 * scale);
    prevBank->setBounds (area.removeFromLeft (navWidth));
    nextBank->setBounds (area.removeFromRight (navWidth));

    const int rows = slotsPerBank / 4;
    const int rowHeight = area.getHeight() / rows;
    for (int r = 0; r < rows; ++r)
    {
        auto row = area.removeFromTop (rowHeight);
        const int w = row.getWidth() / 4;
        for (int c = 0; c < 4; ++c)
            slots[r * 4 + c]->setBounds (row.removeFromLeft (w));
    }
}

} // namespace pedal::ui

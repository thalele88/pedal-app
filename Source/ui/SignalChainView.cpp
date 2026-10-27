#include "ui/SignalChainView.h"

namespace pedal::ui
{

class SignalChainView::Block : public juce::Component
{
public:
    Block (SignalChainView& ownerView, BlockId blockId) : owner (ownerView), id (blockId)
    {
        setRepaintsOnMouseActivity (true);
    }

    void setState (const BlockState& newState)
    {
        if (newState.title == state.title && newState.detail == state.detail
            && newState.canBypass == state.canBypass && newState.enabled == state.enabled)
            return;
        state = newState;
        repaint();
    }

    juce::Rectangle<int> getPowerArea() const
    {
        const int size = juce::jlimit (28, 44, getHeight() / 3);
        return getLocalBounds().removeFromTop (size + 8).removeFromRight (size + 8).reduced (4);
    }

    void paint (juce::Graphics& g) override
    {
        const bool isSelected = owner.selected == id;
        auto r = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (isSelected ? Colours::panelRaised.brighter (0.12f)
                                : (isMouseOver() ? Colours::panelRaised.brighter (0.05f) : Colours::panelRaised));
        g.fillRoundedRectangle (r, 10.0f);

        if (isSelected)
        {
            g.setColour (Colours::accent);
            g.drawRoundedRectangle (r.reduced (1.0f), 10.0f, 2.5f);
        }

        const auto textColour = state.enabled ? Colours::text : Colours::textDim.withAlpha (0.6f);
        auto content = getLocalBounds().reduced (10);

        g.setColour (textColour);
        g.setFont (juce::FontOptions (juce::jlimit (13.0f, 19.0f, (float) getHeight() * 0.16f), juce::Font::bold));
        g.drawFittedText (state.title, content.removeFromTop (content.getHeight() / 2), juce::Justification::centredBottom, 1);

        g.setColour (state.enabled ? Colours::textDim : Colours::textDim.withAlpha (0.4f));
        g.setFont (juce::FontOptions (juce::jlimit (11.0f, 15.0f, (float) getHeight() * 0.12f)));
        g.drawFittedText (state.enabled ? state.detail : juce::String ("bypass"), content.withTrimmedTop (4),
                          juce::Justification::centredTop, 2);

        if (state.canBypass)
        {
            // Power switch: a lit LED when the block is on.
            const auto led = getPowerArea().toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            g.setColour (state.enabled ? Colours::ok : Colours::background);
            g.fillEllipse (led);
            if (state.enabled)
            {
                g.setColour (Colours::ok.withAlpha (0.25f));
                g.fillEllipse (led.expanded (5.0f));
            }
            g.setColour (Colours::textDim.withAlpha (0.6f));
            g.drawEllipse (led, 1.2f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;

        if (state.canBypass && getPowerArea().expanded (6).contains (e.getPosition()))
        {
            if (owner.onTogglePower)
                owner.onTogglePower (id);
            return;
        }

        owner.setSelected (id);
        if (owner.onSelect)
            owner.onSelect (id);
    }

    SignalChainView& owner;
    const BlockId id;
    BlockState state;
};

SignalChainView::SignalChainView()
{
    for (auto id : allBlocks)
        addAndMakeVisible (blocks.add (new Block (*this, id)));
}

SignalChainView::~SignalChainView() = default;

void SignalChainView::setBlockState (BlockId id, const BlockState& state)
{
    blocks[(int) id]->setState (state);
}

void SignalChainView::setSelected (BlockId id)
{
    if (selected == id)
        return;
    selected = id;
    for (auto* b : blocks)
        b->repaint();
}

void SignalChainView::paint (juce::Graphics& g)
{
    // The cable runs behind the blocks, from the input jack to the output jack.
    if (blocks.isEmpty())
        return;

    const float y = (float) blocks.getFirst()->getBounds().getCentreY();
    const float x1 = 4.0f, x2 = (float) getWidth() - 4.0f;
    g.setColour (Colours::textDim.withAlpha (0.35f));
    g.drawLine (x1, y, x2, y, 3.0f);
    g.setColour (Colours::textDim.withAlpha (0.6f));
    g.fillEllipse (x1 - 4.0f, y - 6.0f, 12.0f, 12.0f);
    g.fillEllipse (x2 - 8.0f, y - 6.0f, 12.0f, 12.0f);
}

void SignalChainView::resized()
{
    auto area = getLocalBounds().reduced (16, 4);
    const int gap = 14;
    const int w = (area.getWidth() - gap * (blocks.size() - 1)) / blocks.size();
    for (auto* b : blocks)
    {
        b->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (gap);
    }
}

} // namespace pedal::ui

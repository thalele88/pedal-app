#include "ui/ChainView.h"

namespace pedal::ui
{

namespace
{
constexpr float cardCorner = 16.0f;
}

class ChainView::Card : public juce::Component
{
public:
    Card (ChainView& ownerView, Kind cardKind) : owner (ownerView), kind (cardKind)
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        updateHint();
    }

    void setState (const CardState& s)
    {
        if (s.name == state.name && s.subtitle == state.subtitle && s.canBypass == state.canBypass && s.enabled == state.enabled)
            return;
        state = s;
        updateHint();
        repaint();
    }

    bool isEmpty() const { return state.name.isEmpty(); }

    juce::Rectangle<float> getPowerArea() const { return { 12.0f, 12.0f, 34.0f, 34.0f }; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool over = isMouseOver (true);

        if (isEmpty())
        {
            g.setColour (over ? Colours::panelRaised : Colours::panel);
            g.fillRoundedRectangle (r, cardCorner);

            juce::Path outline, dashed;
            outline.addRoundedRectangle (r, cardCorner);
            const float dash[] = { 7.0f, 5.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dash, 2);
            g.setColour (Colours::textDim.withAlpha (over ? 0.9f : 0.5f));
            g.fillPath (dashed);

            // "+" in a circle, sitting on the cable
            const auto circle = juce::Rectangle<float> (48.0f, 48.0f).withCentre (r.getCentre());
            g.setColour (Colours::background);
            g.fillEllipse (circle);
            g.setColour (over ? Colours::accent : Colours::text);
            g.drawEllipse (circle, 2.0f);
            g.strokePath (Icons::plus (circle), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (kind == Kind::amp ? tr ("Adicionar amp") : tr ("Adicionar caixa (IR)"),
                        r.withTrimmedTop (r.getHeight() * 0.62f).withHeight (24.0f), juce::Justification::centred);
            g.setColour (Colours::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (kind == Kind::amp ? tr ("Abra um .nam ou busque no TONE3000")
                                                : tr ("Abra um .wav de IR de caixa"),
                              r.withTrimmedTop (r.getHeight() * 0.62f + 26.0f).reduced (14.0f, 0.0f).withHeight (36.0f).toNearestInt(),
                              juce::Justification::centredTop, 2);
            return;
        }

        CardArt::draw (g, r, kind, state.name, cardCorner);

        // Name strip at the bottom
        auto strip = r.withTrimmedTop (r.getHeight() * 0.72f);
        juce::ColourGradient fade (juce::Colours::transparentBlack, strip.getX(), strip.getY(),
                                   juce::Colours::black.withAlpha (0.85f), strip.getX(), strip.getBottom(), false);
        g.setGradientFill (fade);
        g.fillRoundedRectangle (strip, cardCorner);

        auto text = strip.reduced (14.0f, 8.0f);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (state.name, text.removeFromTop (text.getHeight() * 0.55f), juce::Justification::bottomLeft, true);
        g.setColour (Colours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (state.subtitle, text, juce::Justification::topLeft, true);

        if (! state.enabled)
        {
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (r, cardCorner);
            g.setColour (Colours::textDim);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText ("DESLIGADO", r, juce::Justification::centred);
        }

        if (state.canBypass)
        {
            const auto p = getPowerArea();
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (p);
            g.setColour (state.enabled ? Colours::accent : Colours::textDim);
            g.strokePath (Icons::power (p.reduced (6.0f)), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (over ? Colours::accent : juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r, cardCorner, over ? 2.0f : 1.0f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()))
            return;

        if (isEmpty())
        {
            if (owner.onAdd)
                owner.onAdd (kind);
        }
        else if (state.canBypass && getPowerArea().expanded (6.0f).contains (e.position))
        {
            if (owner.onTogglePower)
                owner.onTogglePower (kind);
        }
        else if (owner.onOpen)
        {
            owner.onOpen (kind);
        }
    }

private:
    void updateHint()
    {
        if (isEmpty())
            setHint (*this, kind == Kind::amp
                                ? tr ("Adicionar amp: toque para abrir um .nam ou buscar no TONE3000. Também dá para arrastar o arquivo para cá.")
                                : tr ("Adicionar caixa: toque para abrir um IR (.wav). Também dá para arrastar o arquivo para cá."));
        else
            setHint (*this, tr ("Toque para ver e trocar ") + (kind == Kind::amp ? tr ("a captura") : tr ("o IR"))
                                + (state.canBypass ? tr (". O botão de power no canto liga e desliga.") : juce::String ("."))
                                + tr (" Arraste outro arquivo por cima para substituir."));
    }

    ChainView& owner;
    const Kind kind;
    CardState state;
};

//==============================================================================
ChainView::ChainView()
{
    addAndMakeVisible (cards.add (new Card (*this, Kind::amp)));
    addAndMakeVisible (cards.add (new Card (*this, Kind::cab)));
}

ChainView::~ChainView() = default;

void ChainView::setCard (Kind kind, const CardState& state)
{
    cards[kind == Kind::amp ? 0 : 1]->setState (state);
}

void ChainView::paint (juce::Graphics& g)
{
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("CADEIA DE SINAL", getLocalBounds().reduced (8, 4).removeFromTop (28), juce::Justification::centredLeft);

    // The cable runs behind the cards, from the input jack to the output jack.
    const float y = (float) lane.getCentreY();
    g.setColour (Colours::text.withAlpha (0.8f));
    g.drawLine ((float) lane.getX(), y, (float) lane.getRight(), y, 2.0f);
    for (float x : { (float) lane.getX(), (float) lane.getRight() })
    {
        g.setColour (Colours::background);
        g.fillEllipse (x - 7.0f, y - 7.0f, 14.0f, 14.0f);
        g.setColour (Colours::text.withAlpha (0.8f));
        g.drawEllipse (x - 7.0f, y - 7.0f, 14.0f, 14.0f, 2.0f);
    }

    g.setColour (Colours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("ENTRADA", juce::Rectangle<float> ((float) lane.getX() - 7.0f, y + 12.0f, 70.0f, 16.0f), juce::Justification::centredLeft);
    g.drawText (tr ("SAÍDA"), juce::Rectangle<float> ((float) lane.getRight() - 63.0f, y + 12.0f, 70.0f, 16.0f), juce::Justification::centredRight);

    if (dragOver)
    {
        g.setColour (Colours::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
        g.setColour (Colours::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 12.0f, 2.0f);
    }
}

void ChainView::resized()
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (36);
    lane = area.reduced (16, 0);

    const int gap = 28;
    const int side = juce::jmin (area.getHeight() - 20, (lane.getWidth() - 80 - gap) / 2, 300);
    const int total = side * 2 + gap;
    auto row = juce::Rectangle<int> (total, side).withCentre (area.getCentre());
    lane = lane.withSizeKeepingCentre (lane.getWidth(), side);
    for (auto* c : cards)
    {
        c->setBounds (row.removeFromLeft (side));
        row.removeFromLeft (gap);
    }
}

bool ChainView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isCaptureFile (juce::File (f)) || isImpulseFile (juce::File (f)))
            return true;
    return false;
}

void ChainView::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void ChainView::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void ChainView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();

    bool ampDone = false, cabDone = false;
    for (const auto& path : files)
    {
        const juce::File f (path);
        if (! ampDone && isCaptureFile (f) && onAmpFileDropped)
        {
            onAmpFileDropped (f);
            ampDone = true;
        }
        else if (! cabDone && isImpulseFile (f) && onCabFileDropped)
        {
            onCabFileDropped (f);
            cabDone = true;
        }
    }
}

} // namespace pedal::ui

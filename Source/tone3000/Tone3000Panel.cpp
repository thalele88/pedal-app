#include "tone3000/Tone3000Panel.h"

#include "ui/PedalLookAndFeel.h"

namespace pedal::tone3000
{

namespace
{
using pedal::ui::Colours::accent;
using pedal::ui::Colours::background;
using pedal::ui::Colours::panel;
using pedal::ui::Colours::panelRaised;
using pedal::ui::Colours::text;
using pedal::ui::Colours::textDim;

// Nothing listens here: the WebView stops the navigation before any request
// and hands the query string to the Service. TONE3000 accepts any loopback
// redirect without registering it.
constexpr auto webRedirectUri = "http://127.0.0.1/callback";

constexpr int rowHeight = 64;
constexpr int thumbSize = 52;

juce::String u8 (const char* s)
{
    return juce::String::fromUTF8 (s);
}

#if JUCE_WEB_BROWSER
juce::WebBrowserComponent::Options webViewOptions()
{
    auto options = juce::WebBrowserComponent::Options {}.withKeepPageLoadedWhenBrowserIsHidden();
   #if JUCE_WINDOWS
    // WebView2 needs a writable data folder (not next to the .exe); it also
    // keeps the tone3000.com sign-in between sessions.
    options = options.withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                  .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                                               .withUserDataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                                                        .getChildFile ("Pedal App")
                                                                        .getChildFile ("WebView2"))
                                               .withStatusBarDisabled()
                                               .withBuiltInErrorPageDisabled()
                                               .withBackgroundColour (juce::Colours::black));
   #endif
    return options;
}
#endif
} // namespace

// ---- WebView ------------------------------------------------------------------

#if JUCE_WEB_BROWSER
class Tone3000Panel::CatalogView : public juce::WebBrowserComponent
{
public:
    CatalogView() : juce::WebBrowserComponent (webViewOptions()) {}

    std::function<void (juce::String query)> onRedirect;
    std::function<void (juce::String error)> onNetworkError;

    void show (const juce::URL& url)
    {
       #if JUCE_MAC
        // JUCE's WKWebView backend percent-encodes the string again, so give it
        // the decoded form. The OAuth values never contain '&', '=' or '+'.
        goToURL (juce::URL::removeEscapeChars (url.toString (true)));
       #else
        goToURL (url.toString (true));
       #endif
    }

    bool pageAboutToLoad (const juce::String& url) override
    {
        if (! url.startsWith (webRedirectUri))
            return true;

        // Called from inside the WebView: finish on the next message loop turn.
        juce::Component::SafePointer<CatalogView> safeThis (this);
        const auto query = url.fromFirstOccurrenceOf ("?", false, false);
        juce::MessageManager::callAsync ([safeThis, query]
        {
            if (safeThis != nullptr && safeThis->onRedirect)
                safeThis->onRedirect (query);
        });
        return false;
    }

    // Terms, help and other target=_blank links go to the real browser.
    void newWindowAttemptingToLoad (const juce::String& url) override
    {
        juce::URL (url).launchInDefaultBrowser();
    }

    bool pageLoadHadNetworkError (const juce::String& errorInfo) override
    {
        if (onNetworkError)
            onNetworkError (errorInfo);
        return false; // our own message instead of the browser's error page
    }
};
#else
class Tone3000Panel::CatalogView : public juce::Component
{
};
#endif

// ---- Thumbnails -----------------------------------------------------------------

// Tone covers and creator avatars, downloaded on worker threads and shrunk
// there (the originals are ~1000 px JPEGs) before the list paints them.
class Tone3000Panel::Thumbnails
{
public:
    explicit Thumbnails (std::function<void()> changed) : onChanged (std::move (changed)) {}

    ~Thumbnails()
    {
        *alive = false;
        pool.removeAllJobs (true, 3000);
    }

    juce::Image get (const juce::String& url, int size)
    {
        if (url.isEmpty())
            return {};
        if (auto it = images.find (url); it != images.end())
            return it->second;
        if (pending.insert (url).second)
        {
            pool.addJob ([this, url, size, flag = alive]
            {
                juce::Image image;
                const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                         .withConnectionTimeoutMs (10000);
                if (auto stream = juce::URL (url).createInputStream (options))
                    image = juce::ImageFileFormat::loadFrom (*stream);

                if (image.isValid())
                {
                    // Centre crop to a square, then shrink.
                    const int side = juce::jmin (image.getWidth(), image.getHeight());
                    image = image.getClippedImage ({ (image.getWidth() - side) / 2, (image.getHeight() - side) / 2, side, side })
                                .rescaled (size * 2, size * 2, juce::Graphics::mediumResamplingQuality);
                }

                juce::MessageManager::callAsync ([this, url, image, flag]
                {
                    if (! *flag)
                        return;
                    images[url] = image; // an invalid image means "tried": no retries
                    if (onChanged)
                        onChanged();
                });
            });
        }
        return {};
    }

private:
    std::function<void()> onChanged;
    std::map<juce::String, juce::Image> images;
    std::set<juce::String> pending;
    juce::ThreadPool pool { 2 };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

// ---- Panel ----------------------------------------------------------------------

Tone3000Panel::Tone3000Panel (Service& s) : service (s)
{
    brand.setText ("TONE3000", juce::dontSendNotification);
    brand.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    addAndMakeVisible (brand);

    filterBadge.setText (u8 ("NAM · A2"), juce::dontSendNotification);
    filterBadge.setJustificationType (juce::Justification::centred);
    filterBadge.setColour (juce::Label::textColourId, accent);
    filterBadge.setColour (juce::Label::outlineColourId, accent);
    filterBadge.setTooltip (u8 ("Só capturas NAM A2"));
    addAndMakeVisible (filterBadge);

    backButton.onClick = [this]
    {
       #if JUCE_WEB_BROWSER
        if (catalog != nullptr)
            catalog->goBack();
       #endif
    };
    forwardButton.onClick = [this]
    {
       #if JUCE_WEB_BROWSER
        if (catalog != nullptr)
            catalog->goForward();
       #endif
    };
    reloadButton.setButtonText ("Recarregar");
    reloadButton.onClick = [this] { page == Page::catalog ? loadCatalog() : loadList (page); };
    for (auto* b : { (juce::Button*) &backButton, (juce::Button*) &forwardButton, (juce::Button*) &reloadButton })
        addAndMakeVisible (b);

    auto setupTab = [this] (juce::TextButton& tab, const juce::String& name, Page target)
    {
        tab.setButtonText (name);
        tab.setRadioGroupId (3000);
        tab.setClickingTogglesState (false);
        tab.onClick = [this, target] { showPage (target); };
        addAndMakeVisible (tab);
    };
    setupTab (catalogTab, u8 ("Catálogo"), Page::catalog);
    setupTab (favoritesTab, "Favoritos", Page::favorites);
    setupTab (trendingTab, "Em alta", Page::trending);
    setupTab (latestTab, "Novidades", Page::latest);

    signOutButton.setButtonText ("Sair");
    signOutButton.setTooltip ("Desconectar o app da sua conta do TONE3000");
    signOutButton.onClick = [this]
    {
        service.cancelAuthorize();
        service.disconnect();
        tones.clear();
        list.updateContent();
        showPage (page);
    };
    addAndMakeVisible (signOutButton);

    closeButton.setButtonText ("Fechar");
    closeButton.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    addAndMakeVisible (closeButton);

    // Partnership screen, shown once before the first sign-in.
    splashText.setText (u8 ("O Pedal App se conecta ao TONE3000 para dar acesso a uma enorme biblioteca de capturas "
                            "Neural Amp Modeler de equipamentos reais, feitas por uma comunidade de músicos do mundo todo.\n\n"
                            "Entre com a sua conta do TONE3000 (grátis), escolha o tom e ele toca direto aqui no app."),
                        juce::dontSendNotification);
    splashText.setJustificationType (juce::Justification::centred);
    splashText.setFont (juce::FontOptions (17.0f));
    addChildComponent (splashText);

    signInButton.setButtonText ("Entrar no TONE3000");
    signInButton.onClick = [this]
    {
        splashSeen = true;
        showPage (Page::catalog);
    };
    addChildComponent (signInButton);

    browserButton.setButtonText (u8 ("Abrir o catálogo no navegador"));
    browserButton.onClick = [this]
    {
        setMessage (u8 ("Escolha o tom na página do TONE3000 que abriu no navegador..."));
        juce::Component::SafePointer<Tone3000Panel> safeThis (this);
        service.selectToneInBrowser ([safeThis] (SelectResult r)
        {
            if (safeThis != nullptr)
                safeThis->handleAuthorizeResult (r);
        });
    };
    addChildComponent (browserButton);

    message.setJustificationType (juce::Justification::centred);
    message.setColour (juce::Label::textColourId, textDim);
    addChildComponent (message);

    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, background);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addChildComponent (list);

    juce::Component::SafePointer<Tone3000Panel> safeThis (this);
    thumbnails = std::make_unique<Thumbnails> ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->list.repaint();
    });

   #if JUCE_WEB_BROWSER
    if (juce::WebBrowserComponent::areOptionsSupported (webViewOptions()))
    {
        catalog = std::make_unique<CatalogView>();
        catalog->onRedirect = [this] (juce::String query) { service.completeAuthorize (query); };
        catalog->onNetworkError = [this] (juce::String)
        {
            setMessage (u8 ("Sem conexão com o TONE3000. Confira a internet e clique em Recarregar."));
        };
        addChildComponent (*catalog);
    }
   #endif
}

Tone3000Panel::~Tone3000Panel()
{
    if (service.isAuthorizing())
        service.cancelAuthorize();
}

void Tone3000Panel::open()
{
    if (! service.isConnected() && ! splashSeen)
        showPage (Page::splash);
    else
        showPage (page == Page::splash ? Page::catalog : page);
}

void Tone3000Panel::showPage (Page newPage)
{
    page = newPage;

    splashText.setVisible (page == Page::splash);
    signInButton.setVisible (page == Page::splash);
    list.setVisible (page != Page::splash && page != Page::catalog);
    browserButton.setVisible (page == Page::catalog && catalog == nullptr);
    if (catalog != nullptr)
        catalog->setVisible (page == Page::catalog);
    setMessage ({});

    catalogTab.setToggleState (page == Page::catalog, juce::dontSendNotification);
    favoritesTab.setToggleState (page == Page::favorites, juce::dontSendNotification);
    trendingTab.setToggleState (page == Page::trending, juce::dontSendNotification);
    latestTab.setToggleState (page == Page::latest, juce::dontSendNotification);

    if (page == Page::catalog)
    {
        if (catalog != nullptr)
            loadCatalog();
        else
            setMessage (u8 ("Este computador não tem o WebView2 da Microsoft, então o catálogo abre no navegador."));
    }
    else if (page != Page::splash)
    {
        loadList (page);
    }

    updateButtons();
    resized();
}

void Tone3000Panel::updateButtons()
{
    const bool onCatalog = page == Page::catalog && catalog != nullptr;
    backButton.setVisible (onCatalog);
    forwardButton.setVisible (onCatalog);
    reloadButton.setEnabled (page != Page::splash);
    signOutButton.setVisible (service.isConnected());
}

void Tone3000Panel::loadCatalog()
{
   #if JUCE_WEB_BROWSER
    if (catalog == nullptr)
        return;

    // Every visit is a fresh select_tone sign-in (new PKCE secrets). It also
    // signs the user in when needed, all inside the WebView.
    juce::Component::SafePointer<Tone3000Panel> safeThis (this);
    const auto url = service.beginAuthorize (webRedirectUri, true, false, [safeThis] (SelectResult r)
    {
        if (safeThis != nullptr)
            safeThis->handleAuthorizeResult (r);
    });

    if (! url.isEmpty())
        catalog->show (url);
   #endif
}

void Tone3000Panel::handleAuthorizeResult (const SelectResult& result)
{
    updateButtons();

    switch (result.status)
    {
        case SelectResult::Status::selected:
            setMessage ({});
            chooseModel (result.tone);
            break;

        case SelectResult::Status::connected:
            break;

        case SelectResult::Status::failed:
            setMessage (result.error);
            break;

        case SelectResult::Status::canceled:
            break;
    }

    // Ready for the next pick. When a new sign-in replaced this one (tab
    // switch, reload) it is already running and there is nothing to do.
    juce::Component::SafePointer<Tone3000Panel> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr && safeThis->page == Page::catalog && safeThis->isShowing()
            && ! safeThis->service.isAuthorizing())
            safeThis->loadCatalog();
    });
}

void Tone3000Panel::loadList (Page listPage)
{
    tones.clear();
    list.updateContent();
    list.repaint();

    if (! service.isConnected())
    {
        setMessage (u8 ("Entre no TONE3000 pela aba Catálogo para ver suas listas."));
        return;
    }

    setMessage ("Carregando...");
    const auto kind = listPage == Page::favorites ? ToneList::favorites
                    : listPage == Page::trending  ? ToneList::trending
                                                  : ToneList::latest;
    const int request = ++listRequest;
    juce::Component::SafePointer<Tone3000Panel> safeThis (this);
    service.listTones (kind, [safeThis, request] (ListResult r)
    {
        if (safeThis == nullptr || request != safeThis->listRequest)
            return;
        safeThis->tones = r.tones;
        safeThis->list.updateContent();
        safeThis->list.repaint();
        if (r.error.isNotEmpty())
            safeThis->setMessage (r.error);
        else if (r.tones.isEmpty())
            safeThis->setMessage (safeThis->page == Page::favorites
                                      ? u8 ("Nenhum favorito ainda. Marque tons com o coração no catálogo.")
                                      : u8 ("Nada por aqui agora."));
        else
            safeThis->setMessage ({});
        safeThis->updateButtons();
    });
}

void Tone3000Panel::openTone (const juce::String& toneId)
{
    setMessage ("Abrindo o tom...");
    juce::Component::SafePointer<Tone3000Panel> safeThis (this);
    service.getTone (toneId, [safeThis] (ToneResult r)
    {
        if (safeThis == nullptr)
            return;
        safeThis->setMessage (r.error);
        if (r.error.isEmpty())
            safeThis->chooseModel (r.tone);
    });
}

void Tone3000Panel::chooseModel (const Tone& tone)
{
    if (tone.models.size() == 1)
    {
        if (onModelChosen)
            onModelChosen (tone, tone.models.getReference (0));
        return;
    }

    // A tone usually has one capture per amp setting or channel.
    juce::PopupMenu menu;
    menu.addSectionHeader (tone.title + (tone.creator.isNotEmpty() ? " - " + tone.creator : juce::String()));
    const int suggested = Service::defaultModelIndex (tone);
    for (int i = 0; i < tone.models.size(); ++i)
    {
        const auto& m = tone.models.getReference (i);
        menu.addItem (i + 1, m.name + (m.size.isNotEmpty() ? "  (" + m.size + ")" : juce::String()), true, i == suggested);
    }

    juce::Component::SafePointer<Tone3000Panel> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [safeThis, tone] (int chosen)
    {
        if (safeThis != nullptr && chosen > 0 && safeThis->onModelChosen)
            safeThis->onModelChosen (tone, tone.models.getReference (chosen - 1));
    });
}

void Tone3000Panel::setMessage (const juce::String& text)
{
    message.setText (text, juce::dontSendNotification);
    message.setVisible (text.isNotEmpty());
}

// ---- List rows ------------------------------------------------------------------

void Tone3000Panel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, tones.size()))
        return;

    const auto& tone = tones.getReference (row);
    auto area = juce::Rectangle<int> (width, height).reduced (8, 4);

    g.setColour (selected ? panelRaised : panel);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    area.reduce (6, 0);

    // Tone cover
    auto cover = area.removeFromLeft (thumbSize).withSizeKeepingCentre (thumbSize, thumbSize);
    if (auto image = thumbnails->get (tone.imageUrl, thumbSize); image.isValid())
        g.drawImage (image, cover.toFloat(), juce::RectanglePlacement::fillDestination);
    else
    {
        g.setColour (panelRaised);
        g.fillRoundedRectangle (cover.toFloat(), 4.0f);
    }
    area.removeFromLeft (12);

    // "T3K" mark on the right: the full TONE3000 name is already in the header.
    auto mark = area.removeFromRight (40);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("T3K", mark, juce::Justification::centred);

    auto titleRow = area.removeFromTop (area.getHeight() / 2);
    g.setColour (text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (tone.title, titleRow, juce::Justification::bottomLeft, true);

    // Creator (avatar + name), gear and format, as TONE3000 asks for lists.
    auto details = area.reduced (0, 4);
    auto avatar = details.removeFromLeft (18).withSizeKeepingCentre (18, 18);
    if (auto image = thumbnails->get (tone.creatorAvatarUrl, 18); image.isValid())
    {
        juce::Path clip;
        clip.addEllipse (avatar.toFloat());
        g.saveState();
        g.reduceClipRegion (clip);
        g.drawImage (image, avatar.toFloat(), juce::RectanglePlacement::fillDestination);
        g.restoreState();
    }
    details.removeFromLeft (6);

    juce::StringArray parts;
    parts.add (tone.creator);
    if (tone.gear.isNotEmpty())
        parts.add (tone.gear);
    parts.add (tone.format.isNotEmpty() ? tone.format.toUpperCase() : juce::String ("NAM"));
    g.setColour (textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (parts.joinIntoString (u8 (" · ")), details, juce::Justification::centredLeft, true);
}

void Tone3000Panel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    returnKeyPressed (row);
}

void Tone3000Panel::returnKeyPressed (int row)
{
    if (juce::isPositiveAndBelow (row, tones.size()))
        openTone (tones.getReference (row).id);
}

// ---- Layout ---------------------------------------------------------------------

void Tone3000Panel::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (panel);
    g.fillRect (getLocalBounds().removeFromTop (48));
}

void Tone3000Panel::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (48).reduced (8, 8);

    backButton.setBounds (bar.removeFromLeft (28).reduced (4));
    forwardButton.setBounds (bar.removeFromLeft (28).reduced (4));
    reloadButton.setBounds (bar.removeFromLeft (100).reduced (2, 0));
    bar.removeFromLeft (8);
    brand.setBounds (bar.removeFromLeft (120));

    closeButton.setBounds (bar.removeFromRight (80).reduced (2, 0));
    signOutButton.setBounds (bar.removeFromRight (64).reduced (2, 0));
    filterBadge.setBounds (bar.removeFromRight (90).reduced (6, 2));

    const int tabWidth = juce::jmin (110, bar.getWidth() / 4);
    for (auto* tab : { &catalogTab, &favoritesTab, &trendingTab, &latestTab })
        tab->setBounds (bar.removeFromLeft (tabWidth).reduced (2, 0));

    if (catalog != nullptr)
        catalog->setBounds (area);
    list.setBounds (area.reduced (0, 6));

    auto centre = area.withSizeKeepingCentre (juce::jmin (560, area.getWidth() - 32), 220);
    splashText.setBounds (centre.removeFromTop (150));
    signInButton.setBounds (centre.removeFromTop (44).withSizeKeepingCentre (220, 40));
    browserButton.setBounds (area.withSizeKeepingCentre (280, 44));

    // Messages sit over the content: centred for the lists, at the bottom over the catalog.
    message.setBounds (page == Page::catalog ? area.removeFromBottom (36) : area.withSizeKeepingCentre (area.getWidth(), 60));
    message.toFront (false);
}

} // namespace pedal::tone3000

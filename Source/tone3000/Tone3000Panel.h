#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "tone3000/Tone3000Service.h"

#include <map>
#include <set>

namespace pedal::tone3000
{

// The TONE3000 screen inside the app (the idea of the M-VAVE MK300 editor):
//
//   [<] [>] [Recarregar]  Catálogo | Favoritos | Em alta | Novidades   NAM · A2  [Sair] [Fechar]
//   ------------------------------------------------------------------------------------------
//   the TONE3000 catalog page in a WebView, or a list of tones
//
// Picking a tone in either place asks which capture of the tone to use and
// hands it to `onModelChosen`; the panel stays open so the next tone is one
// click away. Before the first sign-in it shows the short "partnership"
// screen TONE3000 asks integrations to show.
//
// Self-contained so any layout can mount it (a sidebar tab, a full screen...).
class Tone3000Panel : public juce::Component,
                      private juce::ListBoxModel
{
public:
    explicit Tone3000Panel (Service&);
    ~Tone3000Panel() override;

    std::function<void (const Tone&, const Model&)> onModelChosen;
    std::function<void()> onClose;

    // Call when the panel becomes visible.
    void open();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Page { splash, catalog, favorites, trending, latest };

    class CatalogView;
    class Thumbnails;

    void showPage (Page);
    void loadCatalog();
    void handleAuthorizeResult (const SelectResult&);
    void loadList (Page);
    void openTone (const juce::String& toneId);
    void chooseModel (const Tone&);
    void setMessage (const juce::String&);
    void updateButtons();

    // ListBoxModel
    int getNumRows() override { return tones.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

    Service& service;
    Page page = Page::splash;
    bool splashSeen = false;

    juce::Label brand, filterBadge, message, splashText;
    juce::ArrowButton backButton { "voltar", 0.5f, juce::Colours::white };
    juce::ArrowButton forwardButton { "avancar", 0.0f, juce::Colours::white };
    juce::TextButton reloadButton, catalogTab, favoritesTab, trendingTab, latestTab;
    juce::TextButton signOutButton, closeButton, signInButton, browserButton;

    std::unique_ptr<CatalogView> catalog; // null when this build or machine has no WebView
    juce::ListBox list { "tones", this };
    juce::Array<Tone> tones;
    std::unique_ptr<Thumbnails> thumbnails;
    int listRequest = 0; // drops answers for a list the user already left

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Tone3000Panel)
};

} // namespace pedal::tone3000

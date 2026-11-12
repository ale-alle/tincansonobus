// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanHome.h"
#include "TinCanJoin.h"
#include "TinCanTheme.h"
#include "TinCanVideo.h"

namespace TinCan {

static bool isLocalhostHttp(const String & url)
{
    if (!url.startsWithIgnoreCase("http://")) return false;
    auto host = url.substring(7).upToFirstOccurrenceOf("/", false, false).upToFirstOccurrenceOf(":", false, false);
    return host == "localhost" || host == "127.0.0.1";
}

String getHomePage()
{
    auto page = SystemStats::getEnvironmentVariable("TINCAN_HOME_PAGE", {}).trim();
    if (page.startsWithIgnoreCase("https://") || isLocalhostHttp(page))
        return page.upToFirstOccurrenceOf("#", false, false);
    return getApiBase() + "/my-lessons";
}

// "scheme://host[:port]/" of an address, lower case
static String originOf(const String & url)
{
    auto schemeEnd = url.indexOf("://");
    if (schemeEnd < 0) return {};
    auto pathStart = url.indexOfChar(schemeEnd + 3, '/');
    return (pathStart < 0 ? url : url.substring(0, pathStart)).toLowerCase() + "/";
}

bool isAllowedPaymentURL(const String & url)
{
    if (url.length() > 2048 || url.containsAnyOf(" \"'<>\\")) return false;
    auto origin = originOf(url);
    return origin == "https://checkout.stripe.com/" || origin == "https://billing.stripe.com/";
}

static bool isWebAddress(const String & url)
{
    return url.startsWithIgnoreCase("https://") || url.startsWithIgnoreCase("http://");
}

#if JUCE_WEB_BROWSER && JUCE_USE_WIN_WEBVIEW2

class HomeView::Browser : public WebBrowserComponent
{
public:
    Browser(const Options & options, HomeView & owner_, const String & origin_)
        : WebBrowserComponent(options), owner(owner_), origin(origin_) {}

    bool pageAboutToLoad(const String & url) override
    {
        if (isTinCanURL(url)) {
            // a canduet:// link (the website's Join, if it ever navigates): join here instead
            join(url);
            return false;
        }
        if (url.toLowerCase().startsWith(origin) || url == "about:blank") return true;
        // anything off the site (Stripe, other sites) opens in the normal browser
        if (isWebAddress(url)) URL(url).launchInDefaultBrowser();
        return false;
    }

    void newWindowAttemptingToLoad(const String & url) override
    {
        if (isTinCanURL(url)) join(url);
        else if (isWebAddress(url)) URL(url).launchInDefaultBrowser();
    }

    bool pageLoadHadNetworkError(const String &) override
    {
        SafePointer<HomeView> safe(&owner);
        MessageManager::callAsync([safe]() { if (safe) safe->showOffline(true); });
        return false;   // no browser error page
    }

    void webMessageReceived(const String & sourceURL, const String & message) override
    {
        // only the site's own pages, and only small messages
        if (!sourceURL.toLowerCase().startsWith(origin) || message.length() > 4096) return;
        var m;
        if (JSON::parse(message, m).failed() || !m.isObject()) return;
        auto type = m.getProperty("t", var()).toString();
        auto url = m.getProperty("url", var()).toString();

        if (type == "join" && isTinCanURL(url)) join(url);
        else if (type == "setup") {
            SafePointer<HomeView> safe(&owner);
            MessageManager::callAsync([safe]() { if (safe && safe->hooks.setup) safe->hooks.setup(); });
        }
        else if (type == "open" && isAllowedPaymentURL(url)) URL(url).launchInDefaultBrowser();
    }

private:
    void join(const String & url)
    {
        // after the web view's own callback returns: joining may hide or replace this view
        SafePointer<HomeView> safe(&owner);
        MessageManager::callAsync([safe, url]() { if (safe && safe->hooks.join) safe->hooks.join(url); });
    }

    HomeView & owner;
    String origin;
};

// Shown when the site can't be reached: the app still works for setting up sound.
class HomeView::Offline : public Component
{
public:
    Offline(std::function<void()> retry, std::function<void()> setup)
    {
        title.setText(TRANS("You're offline"), dontSendNotification);
        title.setFont(Font(28.0f, Font::bold));
        title.setJustificationType(Justification::centred);
        title.setColour(Label::textColourId, TinCanTheme::text);
        addAndMakeVisible(title);

        text.setText(TRANS("CanDuet couldn't reach tincanstudios.ca to show your lessons. Check that this computer "
                           "is online, then try again. You can set up your sound without the internet."),
                     dontSendNotification);
        text.setFont(Font(16.0f));
        text.setJustificationType(Justification::centred);
        text.setColour(Label::textColourId, TinCanTheme::muted);
        addAndMakeVisible(text);

        retryButton.onClick = std::move(retry);
        setupButton.onClick = std::move(setup);
        addAndMakeVisible(retryButton);
        addAndMakeVisible(setupButton);
    }

    void paint(Graphics & g) override { g.fillAll(TinCanTheme::night); }

    void resized() override
    {
        auto r = getLocalBounds().withSizeKeepingCentre(jmin(520, getWidth() - 40), 240);
        title.setBounds(r.removeFromTop(48));
        r.removeFromTop(8);
        text.setBounds(r.removeFromTop(80));
        r.removeFromTop(16);
        auto row = r.removeFromTop(40).withSizeKeepingCentre(380, 40);
        retryButton.setBounds(row.removeFromLeft(180));
        setupButton.setBounds(row.removeFromRight(180));
    }

private:
    Label title, text;
    TextButton retryButton { TRANS("Try again") }, setupButton { TRANS("Set up your sound") };
};

std::unique_ptr<HomeView> HomeView::create(Hooks hooks)
{
    if (!WebBrowserComponent::areOptionsSupported(makeWebViewOptions())) return nullptr;
    return std::unique_ptr<HomeView>(new HomeView(std::move(hooks)));
}

HomeView::HomeView(Hooks hooks_) : hooks(std::move(hooks_)), page(getHomePage())
{
    setOpaque(true);
    browser = std::make_unique<Browser>(makeWebViewOptions(), *this, originOf(page));
    addAndMakeVisible(browser.get());

    SafePointer<HomeView> safe(this);
    offline = std::make_unique<Offline>([safe]() { if (safe) safe->reload(); },
                                        [safe]() { if (safe && safe->hooks.setup) safe->hooks.setup(); });
    addChildComponent(offline.get());

    browser->goToURL(page);
}

HomeView::~HomeView() = default;

void HomeView::reload()
{
    showOffline(false);
    browser->goToURL(page);
}

void HomeView::showOffline(bool isOffline)
{
    offline->setVisible(isOffline);
    browser->setVisible(!isOffline);
}

void HomeView::paint(Graphics & g)
{
    g.fillAll(TinCanTheme::night);
}

void HomeView::resized()
{
    browser->setBounds(getLocalBounds());
    offline->setBounds(getLocalBounds());
}

#else

class HomeView::Browser : public Component {};
class HomeView::Offline : public Component {};
std::unique_ptr<HomeView> HomeView::create(Hooks) { return nullptr; }
HomeView::~HomeView() = default;
void HomeView::reload() {}
void HomeView::resized() {}
void HomeView::paint(Graphics &) {}

#endif

}

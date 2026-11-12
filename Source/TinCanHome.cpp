// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanHome.h"
#include "TinCanJoin.h"
#include "TinCanTheme.h"
#include "TinCanVideo.h"

namespace TinCan {

static bool isLocalhostHttp(const String & url)
{
    if (!url.startsWithIgnoreCase("http://") || url.containsChar('@')) return false;
    auto host = url.substring(7).upToFirstOccurrenceOf("/", false, false).upToFirstOccurrenceOf(":", false, false);
    return host == "localhost" || host == "127.0.0.1";
}

String getHomePage()
{
    auto page = SystemStats::getEnvironmentVariable("TINCAN_HOME_PAGE", {}).trim();
    if (!(page.startsWithIgnoreCase("https://") || isLocalhostHttp(page))) page = getApiBase() + "/my-lessons";
    page = page.upToFirstOccurrenceOf("#", false, false);
    // the site switches to its app mode only with this mark (src/lib/app.ts): other apps that embed
    // WebView2 then keep the normal site
    return page + (page.containsChar('?') ? "&" : "?") + "app=canduet";
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
            // a canduet:// link in the page itself (the website's Join, if it ever navigates): join here
            join(url);
            return false;
        }
        if (url.toLowerCase().startsWith(origin) || url == "about:blank") return true;

        if (isWebAddress(url) || url.startsWithIgnoreCase("mailto:")) URL(url).launchInDefaultBrowser();
        // sent elsewhere before any page of the site showed (a redirect to another host): nothing
        // would ever appear here, so say so instead of staying blank (QA)
        if (!loadedOnce) offlineLater();
        return false;
    }

    void newWindowAttemptingToLoad(const String & url) override
    {
        // never joins from here: frames can ask for new windows too (QA). Web links go to the browser.
        if (isWebAddress(url) || url.startsWithIgnoreCase("mailto:")) URL(url).launchInDefaultBrowser();
    }

    void pageFinishedLoading(const String & url) override
    {
        if (url.toLowerCase().startsWith(origin)) loadedOnce = true;
    }

    bool pageLoadHadNetworkError(const String &) override
    {
        offlineLater();
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
        else if (type == "practice") {
            SafePointer<HomeView> safe(&owner);
            MessageManager::callAsync([safe]() { if (safe && safe->hooks.practice) safe->hooks.practice(); });
        }
        else if (type == "open" && url.length() <= 2048
                 && (isAllowedPaymentURL(url) || (url.toLowerCase().startsWith(origin) && !url.containsAnyOf(" \"'<>\\")))) {
            // a Stripe page, or a page of the site that isn't part of the start menu (contact, policies)
            URL(url).launchInDefaultBrowser();
        }
    }

    void forgetLoaded() { loadedOnce = false; }

private:
    void join(const String & url)
    {
        // after the web view's own callback returns: joining may hide or replace this view
        SafePointer<HomeView> safe(&owner);
        MessageManager::callAsync([safe, url]() { if (safe && safe->hooks.join) safe->hooks.join(url); });
    }

    void offlineLater()
    {
        SafePointer<HomeView> safe(&owner);
        MessageManager::callAsync([safe]() { if (safe) safe->showOffline(true); });
    }

    HomeView & owner;
    String origin;
    bool loadedOnce = false;
};

// Shown when the site can't be shown: the app still works for setting up sound. Tries again by
// itself every 30 seconds while it is showing.
class HomeView::Offline : public Component, private Timer
{
public:
    Offline(std::function<void()> retry_, std::function<void()> setup, std::function<void()> practice)
        : retry(std::move(retry_))
    {
        title.setText(TRANS("Your lessons can't be shown right now"), dontSendNotification);
        title.setFont(Font(26.0f, Font::bold));
        title.setJustificationType(Justification::centred);
        title.setColour(Label::textColourId, TinCanTheme::text);
        addAndMakeVisible(title);

        text.setText(TRANS("CanDuet couldn't load tincanstudios.ca. Check that this computer is online; "
                           "CanDuet tries again by itself every 30 seconds. You can practise or set up your sound in the meantime."),
                     dontSendNotification);
        text.setFont(Font(16.0f));
        text.setJustificationType(Justification::centred);
        text.setColour(Label::textColourId, TinCanTheme::muted);
        addAndMakeVisible(text);

        retryButton.onClick = [this]() { if (retry) retry(); };
        setupButton.onClick = std::move(setup);
        practiceButton.onClick = std::move(practice);
        addAndMakeVisible(retryButton);
        addAndMakeVisible(setupButton);
        addAndMakeVisible(practiceButton);
    }

    void visibilityChanged() override
    {
        if (isVisible()) startTimer(30000);
        else stopTimer();
    }

    void paint(Graphics & g) override { g.fillAll(TinCanTheme::night); }

    void resized() override
    {
        auto r = getLocalBounds().withSizeKeepingCentre(jmin(560, getWidth() - 40), 240);
        title.setBounds(r.removeFromTop(48));
        r.removeFromTop(8);
        text.setBounds(r.removeFromTop(80));
        r.removeFromTop(16);
        auto row = r.removeFromTop(40).withSizeKeepingCentre(560, 40);
        retryButton.setBounds(row.removeFromLeft(180));
        row.removeFromLeft(10);
        practiceButton.setBounds(row.removeFromLeft(180));
        setupButton.setBounds(row.removeFromRight(180));
    }

    void focusFirst() { retryButton.grabKeyboardFocus(); }

private:
    void timerCallback() override { if (retry) retry(); }

    std::function<void()> retry;
    Label title, text;
    TextButton retryButton { TRANS("Try again") }, setupButton { TRANS("Set up your sound") },
               practiceButton { TRANS("Practise") };
};

std::unique_ptr<HomeView> HomeView::create(Hooks hooks)
{
    if (!WebBrowserComponent::areOptionsSupported(makeWebViewOptions())) return nullptr;
    return std::unique_ptr<HomeView>(new HomeView(std::move(hooks)));
}

HomeView::HomeView(Hooks hooks_) : hooks(std::move(hooks_)), page(getHomePage())
{
    setOpaque(true);
    // Tab stays inside the start menu, never in SonoBus's hidden controls underneath (QA)
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    browser = std::make_unique<Browser>(makeWebViewOptions(), *this, originOf(page));
    addAndMakeVisible(browser.get());

    SafePointer<HomeView> safe(this);
    offline = std::make_unique<Offline>([safe]() { if (safe) safe->reload(); },
                                        [safe]() { if (safe && safe->hooks.setup) safe->hooks.setup(); },
                                        [safe]() { if (safe && safe->hooks.practice) safe->hooks.practice(); });
    addChildComponent(offline.get());

    browser->goToURL(page);
}

HomeView::~HomeView() = default;

void HomeView::reload()
{
    showOffline(false);
    browser->forgetLoaded();
    browser->goToURL(page);
}

void HomeView::showOffline(bool isOffline)
{
    offline->setVisible(isOffline);
    browser->setVisible(!isOffline);
    if (isShowing()) {
        if (isOffline) offline->focusFirst();
        else browser->grabKeyboardFocus();
    }
}

void HomeView::visibilityChanged()
{
    // shown again after a lesson: keyboard focus in the page, not in the hidden controls
    if (isShowing() && browser->isVisible()) browser->grabKeyboardFocus();
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
void HomeView::visibilityChanged() {}
void HomeView::resized() {}
void HomeView::paint(Graphics &) {}

#endif

}

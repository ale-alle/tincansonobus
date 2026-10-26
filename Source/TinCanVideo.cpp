// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanVideo.h"
#include "TinCanJoin.h"

namespace TinCan {

static bool isTokenChar(juce_wchar c)
{
    // JWT: base64url segments separated by dots
    return CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_' || c == '.';
}

static bool isLocalhostURL(const String & url, const String & scheme)
{
    if (!url.startsWithIgnoreCase(scheme)) return false;
    auto host = url.substring(scheme.length()).upToFirstOccurrenceOf("/", false, false)
                   .upToFirstOccurrenceOf(":", false, false);
    return host == "localhost" || host == "127.0.0.1";
}

VideoInfo parseVideoInfo(const var & redeemJson)
{
    VideoInfo info;
    auto video = redeemJson.getProperty("video", var());
    if (!video.isObject()) return info;

    String url = video.getProperty("url", var()).toString().trim();
    String token = video.getProperty("token", var()).toString().trim();

    bool urlok = (url.startsWithIgnoreCase("wss://") && url.length() > 6) || isLocalhostURL(url, "ws://");
    if (!urlok || url.length() > 512 || url.containsAnyOf(" #&\"'<>")) return info;

    if (token.isEmpty() || token.length() > 4096) return info;
    for (auto c : token) {
        if (!isTokenChar(c)) return info;
    }

    info.url = url;
    info.token = token;
    return info;
}

static bool isAllowedVideoPage(const String & page)
{
    if (page.startsWithIgnoreCase("https://")) return page.length() > 8;
    return isLocalhostURL(page, "http://");
}

String getVideoPage()
{
    String page = SystemStats::getEnvironmentVariable("TINCAN_VIDEO_PAGE", {}).trim();
   #ifdef TINCAN_VIDEO_PAGE
    if (page.isEmpty() || !isAllowedVideoPage(page)) page = TINCAN_VIDEO_PAGE;
   #endif
    if (page.isEmpty() || !isAllowedVideoPage(page)) page = getApiBase() + "/app/video/";
    return page.upToFirstOccurrenceOf("#", false, false);
}

String buildVideoPageURL(const String & page, const VideoInfo & video, const String & displayName)
{
    return page + "#url=" + URL::addEscapeChars(video.url, true)
                + "&token=" + URL::addEscapeChars(video.token, true)
                + "&name=" + URL::addEscapeChars(displayName.substring(0, 80), true);
}

// "https://host:port/" part of a page address
static String getOrigin(const String & page)
{
    auto schemeEnd = page.indexOf("://");
    if (schemeEnd < 0) return {};
    auto pathStart = page.indexOfChar(schemeEnd + 3, '/');
    return (pathStart < 0 ? page : page.substring(0, pathStart)) + "/";
}

#if JUCE_WEB_BROWSER && JUCE_USE_WIN_WEBVIEW2

bool isVideoBuild() { return true; }

namespace {

// Keeps the web view on the video page's own site; links elsewhere never open.
class VideoBrowser : public WebBrowserComponent
{
public:
    VideoBrowser(const Options & options, const String & origin_) : WebBrowserComponent(options), origin(origin_) {}

    bool pageAboutToLoad(const String & newURL) override
    {
        return newURL.startsWith(origin) || newURL == "about:blank";
    }

    void newWindowAttemptingToLoad(const String &) override {}

private:
    String origin;
};

// Shown when the WebView2 runtime is missing (rare on Windows 10/11).
class NoVideoComponent : public Component
{
public:
    NoVideoComponent()
    {
        message.setText(TRANS("Video needs Microsoft Edge WebView2, which isn't installed on this computer. "
                              "Your lesson audio works without it. Install WebView2, then rejoin the lesson to see video."),
                        dontSendNotification);
        message.setJustificationType(Justification::centred);
        addAndMakeVisible(message);

        installLink.setButtonText(TRANS("Get WebView2 from Microsoft"));
        installLink.setURL(URL("https://developer.microsoft.com/microsoft-edge/webview2/"));
        addAndMakeVisible(installLink);
    }

    void paint(Graphics & g) override { g.fillAll(Colour(0xff111317)); }

    void resized() override
    {
        auto r = getLocalBounds().reduced(24);
        installLink.setBounds(r.removeFromBottom(32).withSizeKeepingCentre(260, 32));
        message.setBounds(r);
    }

private:
    Label message;
    HyperlinkButton installLink;
};

}

WebBrowserComponent::Options makeWebViewOptions()
{
    auto dataFolder = File::getSpecialLocation(File::userApplicationDataDirectory)
                          .getChildFile("CanDuet").getChildFile("WebView2");
    auto loader = File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("WebView2Loader.dll");

    return WebBrowserComponent::Options{}
        .withBackend(WebBrowserComponent::Options::Backend::webview2)
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withWinWebView2Options(WebBrowserComponent::Options::WinWebView2{}
                                    .withDLLLocation(loader)
                                    .withUserDataFolder(dataFolder)
                                    .withStatusBarDisabled());
}

VideoWindow::VideoWindow(const String & title, const VideoInfo & video, const String & displayName,
                         std::function<void()> onClose_)
    : DocumentWindow(title.isNotEmpty() ? title + " - " + TRANS("Video") : TRANS("Lesson video"),
                     Colour(0xff111317), DocumentWindow::allButtons),
      onClose(std::move(onClose_))
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(320, 240, 10000, 10000);

    auto options = makeWebViewOptions();

    // JUCE falls back to Internet Explorer when WebView2 can't start, which has no WebRTC
    if (WebBrowserComponent::areOptionsSupported(options)) {
        auto page = getVideoPage();
        auto browser = std::make_unique<VideoBrowser>(options, getOrigin(page));
        browser->goToURL(buildVideoPageURL(page, video, displayName));
        setContentOwned(browser.release(), false);
    }
    else {
        setContentOwned(new NoVideoComponent(), false);
    }

    centreWithSize(960, 600);
    setVisible(true);
}

#else

bool isVideoBuild() { return false; }

VideoWindow::VideoWindow(const String & title, const VideoInfo &, const String &, std::function<void()> onClose_)
    : DocumentWindow(title, Colours::black, DocumentWindow::closeButton), onClose(std::move(onClose_))
{
}

#endif

VideoWindow::~VideoWindow() = default;

void VideoWindow::closeButtonPressed()
{
    // the owner deletes this window; don't touch members afterwards
    if (auto cb = onClose) cb();
}

}

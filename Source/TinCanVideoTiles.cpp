// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanVideoTiles.h"

namespace TinCan {

static bool isLocalhostHttp(const String & url)
{
    if (!url.startsWithIgnoreCase("http://")) return false;
    auto host = url.substring(7).upToFirstOccurrenceOf("/", false, false).upToFirstOccurrenceOf(":", false, false);
    return host == "localhost" || host == "127.0.0.1";
}

String getVideoTilesPage()
{
    auto page = SystemStats::getEnvironmentVariable("TINCAN_VIDEO_TILES_PAGE", {}).trim();
    if (page.startsWithIgnoreCase("https://") || isLocalhostHttp(page))
        return page.upToFirstOccurrenceOf("#", false, false);

    // next to the video page: …/app/video/ -> …/app/video/tiles.html
    auto video = getVideoPage();
    return video.upToLastOccurrenceOf("/", true, false) + "tiles.html";
}

#if JUCE_WEB_BROWSER && JUCE_USE_WIN_WEBVIEW2

// "scheme://host:port/" part of an address
static String originOf(const String & url)
{
    auto schemeEnd = url.indexOf("://");
    if (schemeEnd < 0) return {};
    auto pathStart = url.indexOfChar(schemeEnd + 3, '/');
    return (pathStart < 0 ? url : url.substring(0, pathStart)) + "/";
}

VideoTilesView::VideoTilesView(const Options & options, const String & origin_)
    : WebBrowserComponent(options), origin(origin_)
{
}

std::unique_ptr<VideoTilesView> VideoTilesView::create(const VideoInfo & video, const String & displayName)
{
    auto options = makeWebViewOptions();
    if (!WebBrowserComponent::areOptionsSupported(options)) return nullptr;

    auto page = getVideoTilesPage();
    std::unique_ptr<VideoTilesView> view(new VideoTilesView(options, originOf(page)));
    view->goToURL(buildVideoPageURL(page, video, displayName));
    return view;
}

bool VideoTilesView::pageAboutToLoad(const String & newURL)
{
    return newURL.startsWith(origin) || newURL == "about:blank";
}

void VideoTilesView::send(const var & message)
{
    postWebMessage(JSON::toString(message, true));
}

void VideoTilesView::webMessageReceived(const String & sourceURL, const String & message)
{
    // only our own page may move sliders
    if (!sourceURL.startsWith(origin) || message.length() > 16384) return;

    var parsed;
    if (JSON::parse(message, parsed).failed() || !parsed.isObject()) return;
    if (onMessage) onMessage(parsed);
}

#endif

}

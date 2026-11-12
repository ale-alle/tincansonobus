// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#include "TinCanVideoTiles.h"

namespace TinCan {

const char * const kVideoIdTag = "canduet-video-1";

bool isValidVideoIdentity(const String & identity)
{
    if (identity.isEmpty() || identity.length() > 64) return false;
    for (auto c : identity) {
        if (!(CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_')) return false;
    }
    return true;
}

String getVideoIdentity(const String & token)
{
    auto payload = token.fromFirstOccurrenceOf(".", false, false).upToFirstOccurrenceOf(".", false, false);
    if (payload.isEmpty() || payload.length() > 4096) return {};

    // base64url -> base64 with padding
    auto b64 = payload.replaceCharacter('-', '+').replaceCharacter('_', '/');
    while (b64.length() % 4 != 0) b64 += "=";

    MemoryOutputStream decoded;
    if (!Base64::convertFromBase64(decoded, b64)) return {};

    var claims;
    if (JSON::parse(decoded.toString(), claims).failed()) return {};
    auto sub = claims.getProperty("sub", var()).toString();
    return isValidVideoIdentity(sub) ? sub : String();
}

String makeVideoIdMessage(const String & identity)
{
    DynamicObject::Ptr o = new DynamicObject();
    o->setProperty("vid", identity);
    return JSON::toString(var(o.get()), true);
}

String parseVideoIdMessage(const String & json)
{
    if (json.length() > 256) return {};
    var parsed;
    if (JSON::parse(json, parsed).failed()) return {};
    auto vid = parsed.getProperty("vid", var()).toString();
    return isValidVideoIdentity(vid) ? vid : String();
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

std::unique_ptr<VideoTilesView> VideoTilesView::create(const String & page, const VideoInfo & video, const String & displayName)
{
    auto options = makeWebViewOptions();
    if (!WebBrowserComponent::areOptionsSupported(options)) return nullptr;

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

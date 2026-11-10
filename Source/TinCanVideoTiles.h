// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
#include "TinCanVideo.h"

#include <functional>

// Video tiles (prototype, branch t-061-video-tiles).
//
// Instead of a separate video window, the video page fills the people area of the main window and
// draws one tile per person (video, name, volume and pan sliders on hover). The page and the app
// talk through WebView2 messages (Tin Can patch in deps/juce, WebBrowserComponent::postWebMessage):
//
//   app  -> page  {"t":"state","peers":[{"id","gain","pan","stereo","muted","level"}],"flipped":id}
//   page -> app   {"t":"ready"}
//                 {"t":"gain","id":<name>,"v":<linear 0..2>}
//                 {"t":"pan","id":<name>,"v":<-1..1>}
//                 {"t":"flip","id":<name>}                 show that person's full audio options
//                 {"t":"rects","rects":{<name>:[x,y,w,h]}} tile positions as fractions of the page
//
// People are matched by display name: the audio room and the video room both get it from the
// lesson ticket. See doc/TINCAN-VIDEO.md.
namespace TinCan {

// tiles page: TINCAN_VIDEO_TILES_PAGE (https, or http to localhost), else <video page>tiles.html
String getVideoTilesPage();

// Matching people to their video. Display names can repeat (the audio room renames a second
// "Sam" to "Sam 2", the video room doesn't), so each app tells the others its video identity
// (the random per-lesson "sub" of its own LiveKit token) in a peer message with this tag:
//   {"vid":"<identity>"}
// The page then matches audio people to video by identity, and by name only as a fallback.
extern const char * const kVideoIdTag;

// the identity inside a LiveKit access token (not verified: only used to match tiles); empty if none
String getVideoIdentity(const String & token);

// identities are short and plain (LiveKit allows more, ours are "p-" + hex); anything else is ignored
bool isValidVideoIdentity(const String & identity);

String makeVideoIdMessage(const String & identity);
String parseVideoIdMessage(const String & json);  // empty unless valid

#if JUCE_WEB_BROWSER && JUCE_USE_WIN_WEBVIEW2

class VideoTilesView : public WebBrowserComponent
{
public:
    // returns nullptr when WebView2 can't start on this computer
    static std::unique_ptr<VideoTilesView> create(const VideoInfo & video, const String & displayName);

    // called on the message thread with each parsed message from our own page
    std::function<void(const var &)> onMessage;

    void send(const var & message);

    bool pageAboutToLoad(const String & newURL) override;
    void newWindowAttemptingToLoad(const String &) override {}
    void webMessageReceived(const String & sourceURL, const String & message) override;

private:
    VideoTilesView(const Options & options, const String & origin);

    String origin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoTilesView)
};

#else

class VideoTilesView : public Component
{
public:
    static std::unique_ptr<VideoTilesView> create(const VideoInfo &, const String &) { return nullptr; }
    std::function<void(const var &)> onMessage;
    void send(const var &) {}
};

#endif

// Volume slider position (0..1, as on the page) <-> gain, matching the app's own level sliders
// (range 0..2, skew 0.5). Kept here so the tests and the page agree.
inline float tileSliderToGain(float pos) { pos = jlimit(0.0f, 1.0f, pos); return 2.0f * pos * pos; }
inline float gainToTileSlider(float gain) { return std::sqrt(jlimit(0.0f, 2.0f, gain) / 2.0f); }

}

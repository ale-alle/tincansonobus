// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"
#include "TinCanVideo.h"

#include <functional>

// The web view that hosts the lesson page (TinCanLesson.h, T-070), plus matching people to their
// video. It stays on its page's own site, and passes on messages from that site only, as parsed JSON
// (Tin Can patch in deps/juce, WebBrowserComponent::postWebMessage). See doc/TINCAN-VIDEO.md.
namespace TinCan {

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
    // page: an allowed https page, or http on localhost (the lesson page, TinCanLesson.h); video may be empty
    // extraHash: more "&key=value" after the page's own hash values (Practice mode: "&practice=1")
    static std::unique_ptr<VideoTilesView> create(const String & page, const VideoInfo & video, const String & displayName,
                                                  const String & extraHash = {});

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
    static std::unique_ptr<VideoTilesView> create(const String &, const VideoInfo &, const String &, const String & = {}) { return nullptr; }
    std::function<void(const var &)> onMessage;
    void send(const var &) {}
};

#endif

// Volume slider position (0..1, as on the page) <-> gain, matching the app's own level sliders
// (range 0..2, skew 0.5). Kept here so the tests and the page agree.
inline float tileSliderToGain(float pos) { pos = jlimit(0.0f, 1.0f, pos); return 2.0f * pos * pos; }
inline float gainToTileSlider(float gain) { return std::sqrt(jlimit(0.0f, 2.0f, gain) / 2.0f); }

}

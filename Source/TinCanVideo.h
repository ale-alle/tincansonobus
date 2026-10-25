// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2026 Tin Can Studios

#pragma once

#include "JuceHeader.h"

#include <functional>

// Tin Can in-lesson video (T-009, D-002).
//
// The redeem response may carry  "video": {"url": "wss://…", "token": "<LiveKit access token>"}.
// The app shows the video page (part of the website) in a window of its own, using Microsoft
// Edge WebView2, and hands it the url and token in the URL fragment, which never leaves the
// computer. The page only uses the camera; lesson audio stays on the low-latency path.
// See doc/TINCAN-VIDEO.md.
namespace TinCan {

struct VideoInfo
{
    String url;    // LiveKit server, wss:// (ws:// only to localhost)
    String token;  // LiveKit access token for this lesson's video room

    bool isValid() const { return url.isNotEmpty() && token.isNotEmpty(); }
};

// reads the optional "video" member of the redeem response; empty if missing or malformed
VideoInfo parseVideoInfo(const var & redeemJson);

// video page address: TINCAN_VIDEO_PAGE environment variable (https, or http to localhost) for
// development, else the TINCAN_VIDEO_PAGE build define, else <api base>/app/video/
String getVideoPage();

// full address handed to the web view: page#url=…&token=…&name=…
String buildVideoPageURL(const String & page, const VideoInfo & video, const String & displayName);

// true when this build can show video (Windows with WebView2 support compiled in)
bool isVideoBuild();

// A window showing the lesson video. Closing it only hides the video; audio carries on.
class VideoWindow : public DocumentWindow
{
public:
    VideoWindow(const String & title, const VideoInfo & video, const String & displayName,
                std::function<void()> onClose);
    ~VideoWindow() override;

    void closeButtonPressed() override;

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoWindow)
};

}
